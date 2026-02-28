#include "string.h"
#include "driver/i2c.h" // Używamy starszego nagłówka dla i2c_cmd_link_create
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h> 

// Wymagany przez driver ST
#include "h3lis331dl_reg.h" 

// Adres I2C (7-bitowy)
#define I2C_MASTER_PORT     I2C_NUM_0
#define H3LIS331DL_SLAVE_ADDR  (H3LIS331DL_I2C_ADD_L >> 1) 

static const char *TAG_I2C = "I2C_ADAPT";

// Funkcja zapisująca dane do rejestru
int32_t platform_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len)
{
    // handle to teraz uchwyt portu I2C (I2C_NUM_0)
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    // 1. Start I2C
    i2c_master_start(cmd);
    
    // 2. Zapisz adres urządzenia i bit zapisu (Write bit = 0)
    // Tworzy 8-bitowy adres I2C = (7-bitowy adres << 1) | I2C_MASTER_WRITE
    i2c_master_write_byte(cmd, (H3LIS331DL_SLAVE_ADDR << 1) | I2C_MASTER_WRITE, true);
    
    // 3. Zapisz adres rejestru (pod-adres)
    i2c_master_write_byte(cmd, reg, true);
    
    // 4. Zapisz dane
    i2c_master_write(cmd, (uint8_t *)bufp, len, true);
    
    // 5. Stop I2C
    i2c_master_stop(cmd);
    
    // 6. Wykonanie transakcji
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_PORT, cmd, pdMS_TO_TICKS(100));
    
    // 7. Usuń link komend
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG_I2C, "I2C Write failed: 0x%X", ret);
    }
    return (ret == ESP_OK) ? 0 : -1;
}


// Funkcja odczytująca dane z rejestru
int32_t platform_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len)
{
    // handle to teraz uchwyt portu I2C (I2C_NUM_0)
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    // ----------------------------------------------------
    // CZĘŚĆ 1: Zapis adresu rejestru (Pod-adres)
    // ----------------------------------------------------
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (H3LIS331DL_SLAVE_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    
    // ----------------------------------------------------
    // CZĘŚĆ 2: Odczyt danych
    // ----------------------------------------------------
    i2c_master_start(cmd); // Powtórzony START lub Restart
    i2c_master_write_byte(cmd, (H3LIS331DL_SLAVE_ADDR << 1) | I2C_MASTER_READ, true);
    
    if (len > 1) {
        // Czytaj wiele bajtów i wysyłaj ACK po każdym (oprócz ostatniego)
        i2c_master_read(cmd, bufp, len - 1, I2C_MASTER_ACK);
    }
    // Czytaj ostatni bajt i wysyłaj NACK
    i2c_master_read_byte(cmd, bufp + len - 1, I2C_MASTER_NACK);
    
    i2c_master_stop(cmd);
    
    // ----------------------------------------------------
    // 3. Wykonanie transakcji
    // ----------------------------------------------------
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG_I2C, "I2C Read failed: 0x%X", ret);
    }
    return (ret == ESP_OK) ? 0 : -1;
}


// Implementacja funkcji opóźnienia
void platform_delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}