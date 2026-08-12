#ifndef MEASURE_H
#define MEASURE_H

#include <stdint.h>           
#include <stdbool.h> 
#include <math.h> 

#include "driver/spi_master.h"
#include "esp_event.h"        
#include "nmea_parser.h"     

extern QueueHandle_t data_queue;
extern QueueHandle_t gps_queue;

extern float config_wake_ths_g;
extern float config_sleep_ths_g;
extern int config_idle_time_s;
extern float CRASH_THRESHOLD_G; 
extern bool is_gps_connected; 
extern bool is_phone_connected;

#define PIN_MISO            17
#define PIN_MOSI            18
#define PIN_SCLK            8

// Akcelerometr H3LIS331DL
#define PIN_ACCEL_CS        14
#define PIN_ACCEL_INT1      21  // To ten pin (GPIO 21) będzie teraz budził ESP32!

// IMU LSM6DSV16X
#define PIN_IMU_CS          13  
// PIN_IMU_INT1 usunięty – nie korzystamy z niego

//GPS
#define GPS_RX_PIN 5  // pin tx z gps
#define GPS_TX_PIN 15 //pin rx z gps

#define PRE_HIT_BUFFER_SIZE  100
#define POST_HIT_SAMPLES     100

//how flequent data from sensors is measured
extern int config_sensor_loop_ms;
extern bool config_enable_sleep; 

typedef struct {
    float x;
    float y;
    float z;
} vector3d_t; 

typedef struct {
    uint8_t packet_type; // hit or flight
    vector3d_t accel_h3lis;
    vector3d_t accel_imu;
    vector3d_t gyro;
    // GPS data
    float latitude;
    float longitude;
    bool gps_fix;
} __attribute__((packed)) global_data_t; //without padding between variables -> send by bluetooth


typedef struct {
    spi_device_handle_t spi_handle; 
    uint8_t cs_pin; 
} sensor_spi_handle_t;

//raw data
typedef struct { float x; float y; float z; } accel_data;

//raw data
typedef struct {
    struct { float x; float y; float z; } accel; 
    struct { float x; float y; float z; } gyro;  
    struct { float x; float y; float z; float w; } quat; 
} imu_data;

typedef struct {
    uint8_t hour; uint8_t minute; uint8_t second; uint16_t thousand; 
} gps_time;

typedef struct {
    //cog - direction of device
    float latitude; float longitude; float altitude; float speed; float cog;
    uint8_t sats_in_use; 
    gps_time time;
} gps_data;

void sensors_set(bool GPS_on);
accel_data accel_get(void);
imu_data imu_get(void);

//from normalize.c
global_data_t convert_to_global_frame(void);

void sensors_task_start(void);
void gps_start(void);
void gps_stop(void);

void lsm6dsv16x_configure_wakeup_threshold(float threshold_g);
void log_global_data(const global_data_t *data);
void get_last_gps_data(float *lat, float *lon, bool *fix);

#endif // MEASURE_H