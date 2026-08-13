#ifndef MEASURE_H
#define MEASURE_H

#include <stdint.h>           
#include <stdbool.h>          
#include "driver/spi_master.h"
#include "esp_event.h"        
#include "nmea_parser.h"     
#include <math.h> 

extern QueueHandle_t data_queue;
extern QueueHandle_t gps_queue; // Potrzebne do main.c

//globalne zmienne do communicate.c, konfiguracyjne
extern float config_wake_ths_g;
extern float config_sleep_ths_g;
extern int config_idle_time_s;
extern float CRASH_THRESHOLD_G; 

extern bool is_phone_connected;

#define PIN_MISO 17
#define PIN_MOSI 18
#define PIN_SCLK 8

#define PIN_ACCEL_CS 14
#define PIIN_ACCEL_INT1 21
//#define PIIN_ACCEL_INT2 

#define PIN_IMU_CS 9
#define PIIN_IMU_INT1 13
//#define PIIN_IMU_INT2 36

#define PRE_HIT_BUFFER_SIZE  100
#define POST_HIT_SAMPLES     100

#define GPS_RX_PIN 5  
#define GPS_TX_PIN 4

extern int config_sensor_loop_ms;

//komunikacja sieciowa
typedef struct {
    float x;
    float y;
    float z;
} vector3d_t; 

typedef struct {
    //uderzenie czy lot
    uint8_t packet_type;
    vector3d_t accel_h3lis;
    vector3d_t accel_imu;
    vector3d_t gyro;
    //dane z GPS
    float latitude;
    float longitude;
    //czy dane z gps sa aktualne
    bool gps_fix;
} __attribute__((packed)) global_data_t;

//wewnetrzne zastosowanie
typedef struct {
    //uchwyt urzadzenia, z ktorym sie komunikujemy
    spi_device_handle_t spi_handle; 
    uint8_t cs_pin; 
} sensor_spi_handle_t;

typedef struct { float x; float y; float z; } accel_data;

typedef struct {
    struct { float x; float y; float z; } accel; 
    struct { float x; float y; float z; } gyro;  
    struct { float x; float y; float z; float w; } quat; 
} imu_data;

typedef struct {
    uint8_t hour; uint8_t minute; uint8_t second; uint16_t thousand; 
} gps_time;

typedef struct {
    //cog - kierunek w jakim porusza sie urzadzenie (polnoc, poludnie, wschod, zachod)
    float latitude; float longitude; float altitude; float speed; float cog;
    uint8_t sats_in_use; 
    gps_time time;
} gps_data;

extern bool config_enable_sleep; // True = usypianie włączone, False = usypianie całkowicie zablokowane

void sensors_set(bool GPS_on);
accel_data accel_get(void);
imu_data imu_get(void);
//z normalize.c
global_data_t convert_to_global_frame(void); 
void sensors_task_start(void);
void gps_start(void);
void gps_stop(void);

void lsm6dsv16x_configure_wakeup_threshold(float threshold_g);

#endif // MEASURE_H