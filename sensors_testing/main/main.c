#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h" 

#include "communicate.h"
#include "measure.h"

static const char *TAG = "main";

QueueHandle_t data_queue = NULL; //kolejka z pomiarami
QueueHandle_t gps_queue = NULL; // kolejka dla GSM

void app_main(void)
{
    ESP_LOGI(TAG, "Hello");

    // Pamiec NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //globalne ustawienia sieci
    //TCP stack and event loop - zadanie odbioru danych wifi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // kolejka dla pomiarow uderzen( 100 przed zderzeniem, 100 po zderzeniu, 50 jako bufor)
    data_queue = xQueueCreate(250, sizeof(global_data_t));
    if (data_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create data_queue for Wi-Fi");
        return;
    }

    gps_queue = xQueueCreate(5, sizeof(gps_data));
    if (gps_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create gps_queue for GSM");
        return;
    }

    // 4. START PODSYSTEMÓW
    sensors_set(false);
    wifi_init_softap();

    tcp_server_start();
    tcp_config_server_start();

    //lokalizacja

    /*
    // wlaczenie GSM
    startGSM(); 

    // utworzenie zadania GSM
    xTaskCreate(lte_sender_task, "lte_task", 4096, NULL, 3, NULL);
    */
    sensors_task_start();
}