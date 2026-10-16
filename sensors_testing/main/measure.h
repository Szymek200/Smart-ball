#ifndef MEASURE_H
#define MEASURE_H

#include <stdint.h>           
#include <stdbool.h> 
#include <math.h> 

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/spi_master.h"
#include "esp_event.h"        

// Kolejki FreeRTOS
extern QueueHandle_t data_queue;
extern QueueHandle_t gps_queue;

// Zmienne konfiguracyjne z NVS / systemowe
extern float config_wake_ths_g;
extern float config_sleep_ths_g;
extern int config_idle_time_s;
extern float crash_threshold_g; 
extern int config_sensor_loop_ms;
extern bool config_enable_sleep; 

// Flagi stanu peryferiów
extern bool is_gps_connected; 
extern bool is_phone_connected;

// Piny magistrali SPI
#define PIN_MISO            17
#define PIN_MOSI            18
#define PIN_SCLK            8

// Akcelerometr H3LIS331DL
#define PIN_ACCEL_CS        14
#define PIN_ACCEL_INT1      21  // Pin wybudzania ESP32 z trybu Light Sleep

// IMU LSM6DSV16X
#define PIN_IMU_CS          13  

// GPS UART
#define GPS_RX_PIN          5   // TX z modułu GPS -> RX w ESP32
#define GPS_TX_PIN          15  // RX w module GPS <- TX z ESP32

// Parametry buforowania zderzenia
#define PRE_HIT_BUFFER_SIZE  100
#define POST_HIT_SAMPLES     100

// Struktury danych
typedef struct {
    float x;
    float y;
    float z;
} vector3d_t; 

typedef struct {
    uint8_t packet_type; // 0: Lot/Normalny, 1: Zderzenie (Hit)
    vector3d_t accel_h3lis;
    vector3d_t accel_imu;
    vector3d_t gyro;
    float latitude;
    float longitude;
    bool gps_fix;
    float battery_voltage;
    float battery_percent;
} __attribute__((packed)) global_data_t;

typedef struct {
    spi_device_handle_t spi_handle; 
    uint8_t cs_pin; 
} sensor_spi_handle_t;

typedef struct {
    struct { float x; float y; float z; } accel; 
    struct { float x; float y; float z; } gyro;  
    struct { float x; float y; float z; float w; } quat; 
} imu_data;

// Funkcje inicjalizacyjne i kontrolne
void sensors_set(bool GPS_on);
void accel_set(void);
void imu_set(void);
void sensors_task_start(void);
void sensors_enter_light_sleep(void);

// Odczyt danych z sensorów
vector3d_t accel_get(void);
imu_data imu_get(void);
global_data_t convert_to_global_frame(void);

// Obsługa GPS
void gps_start(void);
bool get_gps_hardware_status(void);
void get_last_gps_data(float *lat, float *lon, bool *fix);

// Diagnostyka
void log_global_data(const global_data_t *data);

#endif // MEASURE_H