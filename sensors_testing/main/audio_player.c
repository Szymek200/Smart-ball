#include "audio_player.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "AUDIO_PLAYER";

static TaskHandle_t audio_task_handle = NULL;
static volatile bool keep_playing = false;
static char current_filepath[64];

void audio_init(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = 44100,  // Częstotliwość nośna PWM
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = AUDIO_PIN, // GPIO 4
        .duty           = 0, 
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    ESP_LOGI(TAG, "Audio PWM (LEDC) zainicjalizowane na GPIO %d", AUDIO_PIN);
}

static void audio_loop_task(void *pvParameters) {
    uint8_t probka;

    while (keep_playing) {
        FILE* f = fopen(current_filepath, "r");
        if (f == NULL) {
            ESP_LOGE(TAG, "Nie można otworzyć pliku do pętli: %s", current_filepath);
            break;
        }

        while (keep_playing && (fread(&probka, 1, 1, f) > 0)) {
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, probka);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

            // Opóźnienie dla Twojego pliku 8kHz
            esp_rom_delay_us(125); 
        }

        fclose(f);
        
        if (keep_playing) {
            ESP_LOGI(TAG, "Koniec pojedynczego odtworzenia. Zapętlam...");
            // Krótka przerwa dla FreeRTOS na złapanie oddechu między powtórzeniami
            vTaskDelay(pdMS_TO_TICKS(10)); 
        }
    }

    ESP_LOGI(TAG, "Zadanie audio przerwane przez Wi-Fi. Wyciszam głośnik.");
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

    audio_task_handle = NULL;
    vTaskDelete(NULL); 
}

void play_raw(const char* filepath) {
    
    if (keep_playing) {
        stop_raw();
    }

    strncpy(current_filepath, filepath, sizeof(current_filepath) - 1);
    keep_playing = true;

    xTaskCreate(audio_loop_task, "audio_loop", 4096, NULL, 4, &audio_task_handle);
    ESP_LOGI(TAG, "Uruchomiono zapętlone odtwarzanie LEDC: %s", filepath);
}

void stop_raw(void) {
    if (!keep_playing) return;

    ESP_LOGW(TAG, "Zatrzymywanie odtwarzania dźwięku LEDC...");
    keep_playing = false;
    
    int timeout = 0;
    while (audio_task_handle != NULL && timeout < 100) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout++;
    }
}