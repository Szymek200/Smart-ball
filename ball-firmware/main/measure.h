#ifndef MEASURE_H
#define MEASURE_H

#include <stdint.h>           
#include <stdbool.h>          
#include "driver/spi_master.h"
#include "esp_event.h"        
//#include "nmea_parser.h"      
#include "nmea.h"

#define PIN_MOSI 23
#define PIN_MISO 19
#define PIN_SCLK 18

#define PIN_ACCEL_CS 5
#define PIIN_ACCEL_INT1 21
#define PIIN_ACCEL_INT2 35

#define PIN_IMU_CS 17
#define PIIN_IMU_INT1 13
#define PIIN_IMU_INT2 36



#define GPS_RX_PIN 18  // Pin ESP32 połączony z TX modułu TAU1201
#define GPS_TX_PIN 19


typedef struct {
    float x;
    float y;
    float z;
} vector3d_t; 

typedef struct {
    vector3d_t accel_h3lis;
    vector3d_t accel_imu;
    vector3d_t gyro;

    //GPS
    float latitude;
    float longitude;
    bool gps_fix;


} global_data_t;

typedef struct {
    spi_device_handle_t spi_handle; 
    uint8_t cs_pin; 
} sensor_spi_handle_t;

typedef struct {
    struct {
        float x;
        float y;
        float z;
    } accel; 
    
    struct {
        float x;
        float y;
        float z;
    } gyro;  

    struct {
        float x; float y; float z; float w;
    } quat; 
} imu_data;

typedef struct {
    uint8_t hour;      /*!< Hour */
    uint8_t minute;    /*!< Minute */
    uint8_t second;    /*!< Second */
    uint16_t thousand; /*!< Thousand */
} gps_time;

typedef struct {
     float latitude;                                                /*!< Latitude (degrees) */
    float longitude;                                               /*!< Longitude (degrees) */
    float altitude;                                                /*!< Altitude (meters) */
    float speed;                                                   /*!< Ground speed, unit: m/s */
    float cog;                                                      /*!< Course over ground */
    uint8_t sats_in_use;
    gps_time time;
} gps_data;



typedef struct {
    float x;
    float y;
    float z;
} accel_data;

void sensors_set(void);
accel_data accel_get(void);
imu_data imu_get(void);
global_data_t convert_to_global_frame(void); // Nasza nowa funkcja

#endif // MEASURE_H