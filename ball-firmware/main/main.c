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
        
        global_data_t current_data = convert_to_global_frame();

       
        bool is_impact = (current_data.accel_h3lis.z > 5.0f);

        if (is_impact && post_impact_countdown == 0) {
            //jest uderzenie
            //stare uderzenia
            for (int i = 0; i < HISTORY_SIZE; i++) {
            
                int read_idx = (hist_index + i) % HISTORY_SIZE;
                xQueueSend(data_queue, &history_buffer[read_idx], 0);
            }
            //pomiary po uderzeniu
            post_impact_countdown = 100; 
        }

    
        if (post_impact_countdown > 0) {
            // wysylamy na biezaco
            xQueueSend(data_queue, &current_data, 0);
            post_impact_countdown--;
        } 
        else {
            // normalny tryb
            if (normal_counter % NORMAL_SEND_INTERVAL == 0) {
                xQueueSend(data_queue, &current_data, 0);
            }
            normal_counter++;
        }

        // aktualizacja historycznego bufora
        history_buffer[hist_index] = current_data;
        hist_index = (hist_index + 1) % HISTORY_SIZE; // Przewijanie bufora (Ring Buffer)

        // Odpoczynek np. 1ms (1000 Hz)
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
}

void app_main(void)
{

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);


    sensors_set();


    wifi_init_softap();


    data_queue = xQueueCreate(150, sizeof(global_data_t));


    
    xTaskCreate(measure_task, "MEASURE", 4096, NULL, 5, NULL);
    
 
    tcp_server_start();
}






