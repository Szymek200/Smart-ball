#include "normalize.h"
#include "measure.h"

// function from measure.c
//void get_last_gps_data(float *lat, float *lon, bool *fix);

static vector3d_t rotate_vector_by_quaternion(vector3d_t v, imu_data imu)
{
    vector3d_t result;

    // Przypisanie dla czytelności wzoru
    float qx = imu.quat.x;
    float qy = imu.quat.y;
    float qz = imu.quat.z;
    float qw = imu.quat.w;

    // Obliczenie iloczynu wektorowego: q_xyz x v
    float tx = 2.0f * (qy * v.z - qz * v.y);
    float ty = 2.0f * (qz * v.x - qx * v.z);
    float tz = 2.0f * (qx * v.y - qy * v.x);

    // Wynik = v + w * t + q_xyz x t
    result.x = v.x + qw * tx + (qy * tz - qz * ty);
    result.y = v.y + qw * ty + (qz * tx - qx * tz);
    result.z = v.z + qw * tz + (qx * ty - qy * tx);

    return result;
}

global_data_t convert_to_global_frame(void)
{
    accel_data raw_h3lis = accel_get();
    imu_data raw_imu = imu_get();

    global_data_t global = {0};

    //default data to GPS
    float temp_lat = 0.0f;
    float temp_lon = 0.0f;
    bool temp_fix = false;

    get_last_gps_data(&temp_lat, &temp_lon, &temp_fix);

    // to global structure
    global.latitude = temp_lat;
    global.longitude = temp_lon;
    global.gps_fix = temp_fix;

    vector3d_t local_h3lis = { .x = raw_h3lis.x,        .y = raw_h3lis.y,        .z = raw_h3lis.z };
    vector3d_t local_imu_xl = { .x = raw_imu.accel.x,    .y = raw_imu.accel.y,    .z = raw_imu.accel.z };
    vector3d_t local_imu_gy = { .x = raw_imu.gyro.x,     .y = raw_imu.gyro.y,     .z = raw_imu.gyro.z };

    if (raw_imu.quat.w != 0.0f) 
    {
        global.accel_h3lis = rotate_vector_by_quaternion(local_h3lis, raw_imu);
        global.accel_imu   = rotate_vector_by_quaternion(local_imu_xl, raw_imu);
        global.gyro        = rotate_vector_by_quaternion(local_imu_gy, raw_imu);
    }
    else
    {
        // Jeśli system dopiero startuje, przepisujemy dane 1:1, nie niszcząc ich zerowaniem
        global.accel_h3lis = local_h3lis;
        global.accel_imu   = local_imu_xl;
        global.gyro        = local_imu_gy;
    }

    return global;
}