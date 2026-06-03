#include <stdio.h>


#include "measure.h"
#include "communicate.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define HISTORY_SIZE 100
#define NORMAL_SEND_INTERVAL 100

QueueHandle_t data_queue; 

void measure_task(void *pvParameters)
{
    global_data_t history_buffer[HISTORY_SIZE];
    int hist_index = 0;
    
    int normal_counter = 0;
    int post_impact_countdown = 0; // Ile pomiarów po uderzeniu zostało do wysłania

    while (1) {
        // 1. Zawsze pobieraj dane na bieżąco
        global_data_t current_data = convert_to_global_frame();

        // 2. Wykrywanie uderzenia (np. powyżej 5 G na osi Z)
        bool is_impact = (current_data.accel_h3lis.z > 5.0f);

        if (is_impact && post_impact_countdown == 0) {
            // Właśnie nastąpiło uderzenie! 
            // Najpierw wrzucamy do kolejki 100 starych pomiarów (przed uderzeniem)
            for (int i = 0; i < HISTORY_SIZE; i++) {
                // Czytamy od najstarszego do najnowszego przy użyciu modulo
                int read_idx = (hist_index + i) % HISTORY_SIZE;
                xQueueSend(data_queue, &history_buffer[read_idx], 0);
            }
            // Ustawiamy licznik, aby wysłać 100 kolejnych pomiarów
            post_impact_countdown = 100; 
        }

        // 3. Obsługa wysyłania (kolejkowania)
        if (post_impact_countdown > 0) {
            // Jesteśmy po uderzeniu - wysyłamy na bieżąco!
            xQueueSend(data_queue, &current_data, 0);
            post_impact_countdown--;
        } 
        else {
            // Normalny tryb - wysyłamy co 100-tny pomiar
            if (normal_counter % NORMAL_SEND_INTERVAL == 0) {
                xQueueSend(data_queue, &current_data, 0);
            }
            normal_counter++;
        }

        // 4. Aktualizacja bufora historycznego na sam koniec
        history_buffer[hist_index] = current_data;
        hist_index = (hist_index + 1) % HISTORY_SIZE; // Przewijanie bufora (Ring Buffer)

        // Odpoczynek np. 1ms (1000 Hz)
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
}

void app_main(void)
{
    // Inicjalizacja pamięci NVS (wymagane przez Wi-Fi w ESP32)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 1. Inicjalizacja magistrali SPI i konfiguracja czujników
    sensors_set();

    // 2. Uruchomienie punktu dostępowego Wi-Fi
    wifi_init_softap();

    // Tworzenie kolejki na 150 elementow
    data_queue = xQueueCreate(150, sizeof(global_data_t));

    // Zadania FreeRTOS.
    // Argumenty: Funkcja, Nazwa, Rozmiar Stosu, Parametry, Priorytet, Uchwyt
    
    xTaskCreate(measure_task, "MEASURE", 4096, NULL, 5, NULL);
    
    // 3. Uruchomienie serwera TCP (który tworzy zadanie wysyłkowe w tle)
    tcp_server_start();
}






