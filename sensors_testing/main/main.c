#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h" 
#include "nvs.h"
#include "gsm.h"
#include "communicate.h"
#include "measure.h"

static const char *TAG = "main";

QueueHandle_t data_queue = NULL; 
QueueHandle_t gps_queue = NULL;  

// Zmienne konfiguracyjne zdefiniowane w measure.c
extern float config_wake_ths_g;
extern float config_sleep_ths_g;
extern int config_idle_time_s;
extern float CRASH_THRESHOLD_G;

// Funkcja wczytująca zapisane parametry z NVS podczas startu urządzenia
void load_config_from_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        size_t required_size = sizeof(float);
        nvs_get_blob(my_handle, "wake_ths", &config_wake_ths_g, &required_size);
        nvs_get_blob(my_handle, "sleep_ths", &config_sleep_ths_g, &required_size);
        
        required_size = sizeof(int);
        nvs_get_blob(my_handle, "idle_time", &config_idle_time_s, &required_size);
        
        required_size = sizeof(float);
        nvs_get_blob(my_handle, "crash_ths", &CRASH_THRESHOLD_G, &required_size);
        
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Wczytano konfigurację z NVS: Wake=%.2fG, Sleep=%.3fG, Idle=%ds, Crash=%.1fG",
                 config_wake_ths_g, config_sleep_ths_g, config_idle_time_s, CRASH_THRESHOLD_G);
    } else {
        ESP_LOGW(TAG, "Brak zapisanego profilu w NVS. Używanie wartości domyślnych.");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Uruchamianie aplikacji bez SPIFFS i Audio...");

    // Inicjalizacja pamięci NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Wczytanie konfiguracji z pamięci nieulotnej
    load_config_from_nvs();

    // Globalne ustawienia sieci i pętli zdarzeń
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Kolejki danych
    data_queue = xQueueCreate(250, sizeof(global_data_t));
    if (data_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create data_queue");
        return;
    }

    gps_queue = xQueueCreate(5, sizeof(gps_data));
    if (gps_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create gps_queue");
        return;
    }

    // Usunięto całkowicie: init_spiffs() oraz list_spiffs_files()

    // START PODSYSTEMÓW
    sensors_set(false); // Automatycznie skonfiguruje IMU z uwzględnieniem wczytanego config_wake_ths_g
    wifi_init_softap();

    vTaskDelay(pdMS_TO_TICKS(300));

    //tcp_server_start();
    //tcp_config_server_start();
    
    sensors_task_start();
}