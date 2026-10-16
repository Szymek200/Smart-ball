#include "config_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

// Deklaracje zmiennych zewnętrznych (zdefiniowanych najpewniej w measure.c)
extern float config_wake_ths_g;
extern float config_sleep_ths_g;
extern int config_idle_time_s;
extern float crash_threshold_g;

static const char *TAG = "CONFIG_MGR";

void config_manager_init(void) {
    ESP_LOGI(TAG, "Inicjalizacja pamięci NVS...");
    esp_err_t ret = nvs_flash_init();
    
    // Jeśli pamięć jest pełna lub wersja się nie zgadza, formatujemy ją
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Błąd NVS (%s). Formatowanie partycji...", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "Pamięć NVS zainicjalizowana pomyślnie.");
}

void load_config_from_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("nvs", NVS_READONLY, &my_handle);
    
    if (err == ESP_OK) {
        size_t required_size = sizeof(float);
        nvs_get_blob(my_handle, "wake_ths", &config_wake_ths_g, &required_size);
        nvs_get_blob(my_handle, "sleep_ths", &config_sleep_ths_g, &required_size);
        
        required_size = sizeof(int);
        nvs_get_blob(my_handle, "idle_time", &config_idle_time_s, &required_size);
        
        required_size = sizeof(float);
        nvs_get_blob(my_handle, "crash_ths", &crash_threshold_g, &required_size);
        
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Wczytano konfigurację z NVS: Wake=%.2fG, Sleep=%.3fG, Idle=%ds, Crash=%.1fG",
                 config_wake_ths_g, config_sleep_ths_g, config_idle_time_s, crash_threshold_g);
    } else {
        ESP_LOGW(TAG, "Brak zapisanego profilu w NVS (błąd: %s). Używanie wartości domyślnych.", esp_err_to_name(err));
    }
}

void save_config_to_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("nvs", NVS_READWRITE, &my_handle);
    
    if (err == ESP_OK) {
        nvs_set_blob(my_handle, "wake_ths", &config_wake_ths_g, sizeof(float));
        nvs_set_blob(my_handle, "sleep_ths", &config_sleep_ths_g, sizeof(float));
        nvs_set_blob(my_handle, "idle_time", &config_idle_time_s, sizeof(int));
        nvs_set_blob(my_handle, "crash_ths", &crash_threshold_g, sizeof(float));
        
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Konfiguracja pomyślnie zapisana w NVS.");
    } else {
        ESP_LOGE(TAG, "Błąd otwarcia NVS do zapisu: %s", esp_err_to_name(err));
    }
}