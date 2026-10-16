#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "audio_player.h"
#include "esp_spiffs.h"

#include "gsm.h"
#include "ble_config.h"
#include "measure.h"
#include "config_manager.h"

static const char *TAG = "main";

//share data between tasks
QueueHandle_t data_queue = NULL; 
QueueHandle_t gps_queue = NULL;  

extern float config_wake_ths_g;
extern float config_sleep_ths_g;
extern int config_idle_time_s;
extern float crash_threshold_g;
;

#define DATA_QUEUE_SIZE 250

void app_main(void)
{
    esp_log_level_set("NimBLE", ESP_LOG_WARN); //wylaczenie powiadomien Bluetooth
    ESP_LOGI(TAG, "Uruchamianie aplikacji");

    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);        // Surowe pakiety MQTT
    esp_log_level_set("esp-netif_lwip-ppp", ESP_LOG_VERBOSE); // Szczegóły łącza internetowego PPP
    esp_log_level_set("modem_gsm", ESP_LOG_DEBUG);            // Logi z Twojego pliku gsm.c

    config_manager_init();

    // Montowanie partycji SPIFFS - pliki .raw
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    if (esp_vfs_spiffs_register(&spiffs_conf) != ESP_OK) {
        ESP_LOGE(TAG, "Błąd montowania SPIFFS. Pliki audio będą niedostępne.");
    } else {
        ESP_LOGI(TAG, "SPIFFS zamontowany pomyślnie.");
        // --- Wypisywanie plików w SPIFFS ---
        DIR *dir = opendir("/spiffs");
        if (dir == NULL) {
            ESP_LOGE(TAG, "Błąd otwierania katalogu /spiffs");
        } else {
            ESP_LOGI(TAG, "Znalezione pliki w partycji /spiffs:");
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                ESP_LOGI(TAG, " - %s", entry->d_name);
            }
            closedir(dir);
        }
    }

    audio_init();

    load_config_from_nvs();

    data_queue = xQueueCreate(DATA_QUEUE_SIZE, sizeof(global_data_t));
    if (data_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create data_queue");
        return;
    }

    ble_config_init();

    //wylaczam gps na czas jego symulacji do testow mqtt
    sensors_set(false); 

    vTaskDelay(pdMS_TO_TICKS(300));

    sensors_task_start();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //Inicjalizacja sprzętowa modemu GSM (PPP + MQTT)
    ESP_LOGI(TAG, "---------------------------------");
    ESP_LOGI(TAG, "Uruchamianie modemu GSM i MQTT...");
    startGSMtwo();

    // Uruchomienie w pętli zadania wysyłającego dane na serwer
    ESP_LOGI(TAG, "Tworzenie zadania lte_sender_task...");
    xTaskCreate(lte_sender_task, "lte_sender_task", 4096, NULL, 5, NULL);

}