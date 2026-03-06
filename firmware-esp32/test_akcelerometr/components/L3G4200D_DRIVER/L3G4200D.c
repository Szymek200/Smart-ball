#include "L3G4200D.h"
#include <esp_log.h>
#include <math.h>


static const char *TAG_L3G = "L3G4200D";



static esp_err_t write_reg_8(const l3g4200d_dev_t *dev, uint8_t reg, uint8_t value) {
 
    uint8_t addr_8bit = dev->i2c_addr << 1; 
    
    
    return platform_i2c_write((void*)addr_8bit, reg, &value, 1) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t read_reg_8(const l3g4200d_dev_t *dev, uint8_t reg, uint8_t *value) {
    uint8_t addr_8bit = dev->i2c_addr << 1; 

    
    return platform_i2c_read((void*)addr_8bit, reg, value, 1) == 0 ? ESP_OK : ESP_FAIL;
}


esp_err_t l3g4200d_init(l3g4200d_dev_t *dev, uint8_t i2c_addr, l3g4200d_dps_t scale, l3g4200d_odrbw_t odrbw)
{
  
    dev->i2c_addr = i2c_addr; 
    
  
    uint8_t id = 0;
    if (read_reg_8(dev, L3G4200D_REG_WHO_AM_I, &id) != ESP_OK || id != L3G4200D_ID_VAL) {
        ESP_LOGE(TAG_L3G, "ID mismatch! Read 0x%02X, expected 0xD3.", id);
        return ESP_ERR_NOT_FOUND;
    }

    
    uint8_t reg1 = 0x00;
    reg1 |= 0x0F; 
    reg1 |= (odrbw << 4); 
    write_reg_8(dev, L3G4200D_REG_CTRL_REG1, reg1);

    
    write_reg_8(dev, L3G4200D_REG_CTRL_REG4, (uint8_t)scale << 4);
    

    switch(scale) {
        case L3G4200D_SCALE_250DPS: dev->dpsPerDigit = 0.00875f; break;
        case L3G4200D_SCALE_500DPS: dev->dpsPerDigit = 0.0175f; break;
        case L3G4200D_SCALE_2000DPS: dev->dpsPerDigit = 0.07f; break;
        default: dev->dpsPerDigit = 0.07f; break;
    }

  
    write_reg_8(dev, L3G4200D_REG_CTRL_REG2, 0x00);
    write_reg_8(dev, L3G4200D_REG_CTRL_REG3, 0x08); 
    write_reg_8(dev, L3G4200D_REG_CTRL_REG5, 0x00); 

    return ESP_OK;
}


esp_err_t l3g4200d_get_id(l3g4200d_dev_t *dev, uint8_t *id) {
    return read_reg_8(dev, L3G4200D_REG_WHO_AM_I, id);
}



esp_err_t l3g4200d_read_raw(l3g4200d_dev_t *dev, Vector *raw) 
{
    uint8_t buffer[6];
  
    uint8_t reg_addr = L3G4200D_REG_OUT_X_L | 0x80;


    esp_err_t ret = platform_i2c_read((void*)(dev->i2c_addr << 1), reg_addr, buffer, 6) == 0 ? ESP_OK : ESP_FAIL;
    if (ret != ESP_OK) return ret;

    raw->XAxis = (int16_t)(buffer[1] << 8 | buffer[0]);
    raw->YAxis = (int16_t)(buffer[3] << 8 | buffer[2]);
    raw->ZAxis = (int16_t)(buffer[5] << 8 | buffer[4]);

    return ESP_OK;
}


esp_err_t l3g4200d_read_normalize(l3g4200d_dev_t *dev, Vector *norm)
{
    Vector raw;
    if (l3g4200d_read_raw(dev, &raw) != ESP_OK) return ESP_FAIL;

    norm->XAxis = raw.XAxis * dev->dpsPerDigit;
    norm->YAxis = raw.YAxis * dev->dpsPerDigit;
    norm->ZAxis = raw.ZAxis * dev->dpsPerDigit;

    return ESP_OK;
}