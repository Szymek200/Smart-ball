#ifndef L3G4200D_DRIVER_H
#define L3G4200D_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include "driver/i2c.h" // Wymagane dla i2c_port_t, aby uniknąć błędów
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- REJESTRY I ADRESY ---
#define L3G4200D_ADDRESS_7BIT      0x69 // Adres urządzenia (ustawiony na 0x69 zgodnie ze skanem)
#define L3G4200D_ID_VAL            0xD3

#define L3G4200D_REG_WHO_AM_I      (0x0F)
#define L3G4200D_REG_CTRL_REG1     (0x20)
#define L3G4200D_REG_CTRL_REG2     (0x21)
#define L3G4200D_REG_CTRL_REG3     (0x22)
#define L3G4200D_REG_CTRL_REG4     (0x23)
#define L3G4200D_REG_CTRL_REG5     (0x24)
#define L3G4200D_REG_OUT_X_L       (0x28)
// ... (pozostałe rejestry OUT_Y_L, OUT_Z_L są niepotrzebne, bo używamy auto-inkrementacji od X_L)


// --- TYPY ENUM (Odpowiadają logice Arduino) ---
typedef enum {
    L3G4200D_SCALE_2000DPS = 0b10,
    L3G4200D_SCALE_500DPS  = 0b01,
    L3G4200D_SCALE_250DPS  = 0b00
} l3g4200d_dps_t;

typedef enum {
    L3G4200D_DATARATE_100HZ_12_5 = 0b0000,
    L3G4200D_DATARATE_400HZ_50   = 0b1010,

} l3g4200d_odrbw_t;


typedef struct {
    float XAxis;
    float YAxis;
    float ZAxis;
} Vector;



typedef struct {
  
    uint8_t i2c_addr; 
    float dpsPerDigit;
    Vector r; 
    Vector n; 
} l3g4200d_dev_t;



esp_err_t l3g4200d_init(l3g4200d_dev_t *dev, uint8_t i2c_addr, l3g4200d_dps_t scale, l3g4200d_odrbw_t odrbw);
esp_err_t l3g4200d_read_normalize(l3g4200d_dev_t *dev, Vector *norm);
esp_err_t l3g4200d_get_id(l3g4200d_dev_t *dev, uint8_t *id);



extern int32_t platform_i2c_write(void *slave_addr_handle, uint8_t reg_addr, const uint8_t *data, uint16_t len);
extern int32_t platform_i2c_read(void *slave_addr_handle, uint8_t reg_addr, uint8_t *data, uint16_t len);


#ifdef __cplusplus
}
#endif

#endif // L3G4200D_DRIVER_H