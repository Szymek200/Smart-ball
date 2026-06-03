#include "normalize.h"



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

    // Konwersja jednostek do pełnych G
    vector3d_t local_h3lis = { .x = raw_h3lis.x / 1000.0f, .y = raw_h3lis.y / 1000.0f, .z = raw_h3lis.z / 1000.0f };
    vector3d_t local_imu_xl = { .x = raw_imu.accel.x, .y = raw_imu.accel.y, .z = raw_imu.accel.z };
    vector3d_t local_imu_gy = { .x = raw_imu.gyro.x, .y = raw_imu.gyro.y, .z = raw_imu.gyro.z };

    // Bezpiecznik: sprawdzamy długość kwaternionu z Twojej struktury imu.quat
    float q_mag_sq = (raw_imu.quat.x * raw_imu.quat.x) + 
                     (raw_imu.quat.y * raw_imu.quat.y) + 
                     (raw_imu.quat.z * raw_imu.quat.z) + 
                     (raw_imu.quat.w * raw_imu.quat.w);

    if (q_mag_sq > 0.9f && q_mag_sq < 1.1f) 
    {
        // Przekazujemy wektor oraz całą strukturę raw_imu
        global.accel_h3lis = rotate_vector_by_quaternion(local_h3lis, raw_imu);
        global.accel_imu   = rotate_vector_by_quaternion(local_imu_xl, raw_imu);
        global.gyro        = rotate_vector_by_quaternion(local_imu_gy, raw_imu);
    }
    else
    {
        // Jeśli kwaternion nie jest jeszcze zainicjalizowany przez SFLP
        global.accel_h3lis = local_h3lis;
        global.accel_imu   = local_imu_xl;
        global.gyro        = local_imu_gy;
    }

    return global;
}