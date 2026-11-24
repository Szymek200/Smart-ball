#include "button.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "ble_config.h"

#define BOOT_BUTTON_PIN GPIO_NUM_0 

static const char *TAG = "BUTTON";

void button_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "Przycisk BOOT (GPIO %d) zainicjalizowany.", BOOT_BUTTON_PIN);
}

void button_task(void *pvParameters) {
    int press_counter = 0;
    
    while(1) {
        if (gpio_get_level(BOOT_BUTTON_PIN) == 0) {
            // button is being pressed
            press_counter++;
            
            //  5 sekund (50 * 100ms) - robimy twardy reset
            if (press_counter == 50) {
                ESP_LOGW(TAG, "Trwa usuwanie parowania (czyszczenie NVS)...");
                nvs_flash_erase(); 
                ESP_LOGW(TAG, "Pamięć wyczyszczona. Restart urządzenia!");
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart(); 
            }
        } else {
            // Przycisk został PUSZCZONY
            if (press_counter > 0) {
                // mniej niz 5s ale wiecej niz 1s
                if (press_counter >= 10 && press_counter < 50) {
                    ESP_LOGI(TAG, "Wykryto krótkie wciśnięcie! Uruchamiam Tryb Parowania.");
                    ble_enable_pairing_mode();
                }
                
                press_counter = 0; 
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}