#include "audio_player.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "AUDIO_PLAYER";

static TaskHandle_t audio_task_handle = NULL;
static volatile bool keep_playing = false;
static char current_filepath[64];

static i2s_chan_handle_t tx_handle = NULL;

void audio_init(void)
{

    gpio_config_t sd_conf = {
        .pin_bit_mask = (1ULL << MAX_SD_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&sd_conf));

    //włączenie MAX98357A
    gpio_set_level(MAX_SD_PIN, 1);

    // Utworzenie kanału TX I2S
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        I2S_NUM_0,
        I2S_ROLE_MASTER
    );

    ESP_ERROR_CHECK(
        i2s_new_channel(&chan_cfg, &tx_handle, NULL)
    );

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),

        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_MONO
        ),

        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK_PIN,
            .ws = I2S_LRC_PIN,
            .dout = I2S_DIN_PIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(
        i2s_channel_init_std_mode(tx_handle, &std_cfg)
    );

    // Włączenie kanału

    ESP_ERROR_CHECK(
        i2s_channel_enable(tx_handle)
    );


    ESP_LOGI(
        TAG,
        "Audio I2S zainicjalizowane: BCLK=%d, LRC=%d, DIN=%d, %d Hz, %d bit",
        I2S_BCLK_PIN,
        I2S_LRC_PIN,
        I2S_DIN_PIN,
        AUDIO_SAMPLE_RATE,
        AUDIO_BITS
    );
}

static void audio_loop_task(void *pvParameters)
{
    uint8_t buffer[1024];
    size_t bytes_written;

    while (keep_playing) {

        FILE *f = fopen(current_filepath, "rb");

        if (f == NULL) {
            ESP_LOGE(
                TAG,
                "Nie można otworzyć pliku: %s",
                current_filepath
            );
            break;
        }

        while (keep_playing && !feof(f)) {

            size_t bytes_read =
                fread(buffer, 1, sizeof(buffer), f);

            if (bytes_read > 0) {

                esp_err_t err = i2s_channel_write(
                    tx_handle,
                    buffer,
                    bytes_read,
                    &bytes_written,
                    portMAX_DELAY
                );

                if (err != ESP_OK) {
                    ESP_LOGE(
                        TAG,
                        "Błąd zapisu do I2S: %s",
                        esp_err_to_name(err)
                    );
                    break;
                }
            }
        }


        fclose(f);

        if (keep_playing) {
            ESP_LOGI(TAG, "Koniec pliku. Zapętlam...");

            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }


    ESP_LOGI(
        TAG,
        "Odtwarzanie zatrzymane."
    );


    audio_task_handle = NULL;

    vTaskDelete(NULL);
}


void play_raw(const char *filepath)
{
    if (filepath == NULL) {
        ESP_LOGE(TAG, "filepath == NULL");
        return;
    }


    if (keep_playing) {
        stop_raw();
    }


    strncpy(
        current_filepath,
        filepath,
        sizeof(current_filepath) - 1
    );

    current_filepath[sizeof(current_filepath) - 1] = '\0';

    keep_playing = true;


    BaseType_t result = xTaskCreate(
        audio_loop_task,
        "audio_loop",
        4096,
        NULL,
        4,
        &audio_task_handle
    );


    if (result != pdPASS) {
        keep_playing = false;
        audio_task_handle = NULL;

        ESP_LOGE(
            TAG,
            "Nie udało się utworzyć zadania audio"
        );

        return;
    }


    ESP_LOGI(
        TAG,
        "Uruchomiono odtwarzanie I2S: %s",
        current_filepath
    );
}

void stop_raw(void)
{
    if (!keep_playing) {
        return;
    }


    ESP_LOGW(
        TAG,
        "Zatrzymywanie odtwarzania I2S..."
    );


    keep_playing = false;


    int timeout = 0;

    while (
        audio_task_handle != NULL &&
        timeout < 100
    ) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout++;
    }


    if (audio_task_handle != NULL) {
        ESP_LOGW(
            TAG,
            "Zadanie audio nie zakończyło się w oczekiwanym czasie"
        );
    }
}