#ifndef MEASURE_H
#define MEASURE_H

#include <stdint.h>           
#include "driver/spi_master.h"

#define PIN_MOSI 18
#define PIN_MISO 17
#define PIN_SCLK 8

#define PIN_ACCEL_CS 14
#define PIIN_ACCEL_INT1 21
#define PIIN_ACCEL_INT2 35

#define PIN_IMU_CS 19
#define PIIN_IMU_INT1 13
#define PIIN_IMU_INT2 36


typedef struct {
    float x;
    float y;
    float z;
} vector3d_t; 

typedef struct {
    vector3d_t accel_h3lis;
    vector3d_t accel_imu;
    vector3d_t gyro;
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
    float x;
    float y;
    float z;
} accel_data;

void sensors_set(void);
accel_data accel_get(void);
imu_data imu_get(void);
global_data_t convert_to_global_frame(void); // Nasza nowa funkcja

#endif // MEASURE_H