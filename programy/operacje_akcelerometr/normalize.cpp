#include "normalize.h"

// single definition of orientation
Quaternion orientation = {1, 0, 0, 0};

//dlugosc wektora rowna 1
void normalizeVector(TripleF &v) {
    float mag = sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    if (mag > 0) {
        v.x /= mag;
        v.y /= mag;
        v.z /= mag;
    }
}

Quaternion qMultiply(const Quaternion& a, const Quaternion& b) {
    Quaternion r;
    r.w = a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z;
    r.x = a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y;
    r.y = a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x;
    r.z = a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w;
    return r;
}

//obrocenie osi na te globalne
void rotateVector(const Quaternion &q, TripleF &v) {
    Quaternion vecQuat = {0, v.x, v.y, v.z};
    Quaternion qInv = {q.w, -q.x, -q.y, -q.z};

    Quaternion res = qMultiply(qMultiply(q, vecQuat), qInv);

    v.x = res.x;
    v.y = res.y;
    v.z = res.z;
}

//dt - czas pomiedzy pomiarami
TripleF updateWorldAccel(TripleF gyro, TripleF accel, float dt) {
    //wartosc wyjsciowa
    TripleF worldAccel;


    // --- 1. Gyro integration (deg/s → rad/s)
    float wx = gyro.x * DEG_TO_RAD;
    float wy = gyro.y * DEG_TO_RAD;
    float wz = gyro.z * DEG_TO_RAD;

    // quaternion delta
    Quaternion dq = {1, 0.5f*wx*dt, 0.5f*wy*dt, 0.5f*wz*dt};
    orientation = qMultiply(orientation, dq);

    // normalize quaternion
    float mag = sqrt(orientation.w*orientation.w + orientation.x*orientation.x +
                     orientation.y*orientation.y + orientation.z*orientation.z);
    orientation.w /= mag;
    orientation.x /= mag;
    orientation.y /= mag;
    orientation.z /= mag;

    // --- 2. Accelerometer drift correction
    TripleF accelNorm = accel;
    normalizeVector(accelNorm);

    TripleF expectedGravity = {0, 0, -1};
    rotateVector(orientation, expectedGravity);

    // cross product error
    TripleF error;
    error.x = accelNorm.y*expectedGravity.z - accelNorm.z*expectedGravity.y;
    error.y = accelNorm.z*expectedGravity.x - accelNorm.x*expectedGravity.z;
    error.z = accelNorm.x*expectedGravity.y - accelNorm.y*expectedGravity.x;

    const float K = 0.02f;
    orientation.x += error.x * K;
    orientation.y += error.y * K;
    orientation.z += error.z * K;

    // --- 3. Rotate accelerometer into world frame
    worldAccel = accel;
    rotateVector(orientation, worldAccel);

    return worldAccel;
}