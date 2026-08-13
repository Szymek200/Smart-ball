#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h" 
#include "audio_player.h"
#include "esp_spiffs.h"
#include "gsm.h"

#include <dirent.h>     // <-- POPRAWIONE (zamiast <sys/dirent.h>)
#include <sys/stat.h>   // Dla funkcji stat (rozmiar plików)

#include "communicate.h"
#include "measure.h"

static const char *TAG = "main";

QueueHandle_t data_queue = NULL; //kolejka z pomiarami
QueueHandle_t gps_queue = NULL; // kolejka dla GSM

#include <sys/dirent.h>
#include <sys/stat.h>
#include "esp_log.h"

void list_spiffs_files(void) {
    ESP_LOGW("DIAGNOSTYKA", "--- Lista plików na partycji SPIFFS ---");
    
    DIR *dir = opendir("/spiffs");
    if (dir == NULL) {
        ESP_LOGE("DIAGNOSTYKA", "Nie można otworzyć katalogu /spiffs!");
        return;
    }

    struct dirent *entry;
    int file_count = 0;
    
 while ((entry = readdir(dir)) != NULL) {
        file_count++;
        struct stat st;
        // Zwiększamy rozmiar do 300, aby z zapasem pomieścić max 255 bajtów z d_name
        char full_path[300]; 
        
        // Używamy bezpiecznego snprintf zamiast sprintf
        snprintf(full_path, sizeof(full_path), "/spiffs/%s", entry->d_name);
        
        if (stat(full_path, &st) == 0) {
            ESP_LOGI("DIAGNOSTYKA", "Znaleziono plik: %s (%ld bajtów)", entry->d_name, st.st_size);
        } else {
            ESP_LOGI("DIAGNOSTYKA", "Znaleziono plik: %s (nie można pobrać rozmiaru)", entry->d_name);
        }
    }

    closedir(dir);
    
    if (file_count == 0) {
        ESP_LOGW("DIAGNOSTYKA", "Partycja SPIFFS jest całkowicie PUSTA!");
    }
    ESP_LOGW("DIAGNOSTYKA", "---------------------------------------");
}

void init_spiffs(void)
{
    ESP_LOGI(TAG, "Inicjalizacja SPIFFS...");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = false
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Błąd montowania systemu plików SPIFFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Nie znaleziono partycji SPIFFS");
        } else {
            ESP_LOGE(TAG, "Błąd inicjalizacji SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "SPIFFS zamontowany pomyślnie!");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Hello");

    esp_log_level_set("esp_modem", ESP_LOG_DEBUG);
esp_log_level_set("esp_modem_dte", ESP_LOG_DEBUG);
esp_log_level_set("esp_modem_dce", ESP_LOG_DEBUG);

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

   // AUDIO
    init_spiffs();
    list_spiffs_files();
    audio_init();

    // START PODSYSTEMÓW
    sensors_set(true);
    wifi_init_softap();

    tcp_server_start();
    tcp_config_server_start();

    // --- URUCHOMIENIE DIAGNOSTYKI GSM ---
    //sim7070_full_test(); 
   
    
    sensors_task_start();
}