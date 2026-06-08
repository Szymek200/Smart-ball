#include "audio_player.h"
#include "driver/i2s_std.h"
#include "esp_spiffs.h" // Flash jako dysk
#include "esp_log.h"

// NAPRAWA BŁĘDU: portMAX_DELAY wymaga tych dwóch nagłówków FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio_player";

// Kanał dźwiękowy
static i2s_chan_handle_t tx_chan;

void audio_init(void) {
    // Inicjalizacja SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs", .partition_label = NULL, .max_files = 2, .format_if_mount_failed = true
    };
    esp_vfs_spiffs_register(&conf);

    // NAPRAWA BŁĘDU: W ESP-IDF v6 prawidłowe makro to I2S_CHANNEL_DEFAULT_CONFIG (pełne słowo CHANNEL)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &tx_chan, NULL);

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100), // Częstotliwość próbkowania pliku WAV
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = GPIO_NUM_41, .ws = GPIO_NUM_42, .dout = GPIO_NUM_2, .din = I2S_GPIO_UNUSED }
    };
    
    // NAPRAWA BŁĘDU: W ESP-IDF v6 używamy i2s_channel_init_std_mode
    i2s_channel_init_std_mode(tx_chan, &std_cfg);
    i2s_channel_enable(tx_chan);
}

void play_wav(const char* filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return;

    fseek(f, 44, SEEK_SET); // Pomiń nagłówek WAV
    int16_t buffer[512];
    size_t bytes_read, bytes_written;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        i2s_channel_write(tx_chan, buffer, bytes_read, &bytes_written, portMAX_DELAY);
    }
    fclose(f);
}