#include "normalize.h"



// w, x, y, z
Quaternion_t orientation = {1.0f, 0.0f, 0.0f, 0.0f};

// mnozy 2 kwaterniony
static Quaternion_t quaternion_multiply(const Quaternion_t *a, const Quaternion_t *b) {
    Quaternion_t r;
    r.w = a->w*b->w - a->x*b->x - a->y*b->y - a->z*b->z;
    r.x = a->w*b->x + a->x*b->w + a->y*b->z - a->z*b->y;
    r.y = a->w*b->y - a->x*b->z + a->y*b->w + a->z*b->x;
    r.z = a->w*b->z + a->x*b->y - a->y*b->x + a->z*b->w;
    return r;
}

//obracamy wektor za pomoca kwaternionu
static void rotate_vector(const Quaternion_t *q, TripleF *v) {

    //kwaternion czysty - w = 0
    Quaternion_t vecQuat = {0.0f, v->x, v->y, v->z};

    //obliczenie kwaternionu odwrotnego - sprzezonego do kw. obrotu - rotation

    //gdy kw. jest jednostkowy - kw. odwortny = kw. sprzezonemu
    Quaternion_t qInv = {q->w, -q->x, -q->y, -q->z};
    Quaternion_t temp1, res;

    temp1 = quaternion_multiply(q, &vecQuat);
    res = quaternion_multiply(&temp1, &qInv);

    v->x = res.x;
    v->y = res.y;
    v->z = res.z;
}

//normalizacja wektora
void normalize_vector(TripleF *v) {
    float mag = sqrt(v->x*v->x + v->y*v->y + v->z*v->z);
    if (mag > 0.0f) {
        v->x /= mag;
        v->y /= mag;
        v->z /= mag;
    }
}

// Główna funkcja integracji (Magnetometer and Gyro Fusion)
TripleF update_world_accel(TripleF gyro, TripleF accel, float dt) {
    // Gyro integration (deg/s → rad/s)
    float wx = gyro.x * DEG_TO_RAD;
    float wy = gyro.y * DEG_TO_RAD;
    float wz = gyro.z * DEG_TO_RAD;

    // quaternion delta

    //uaktualnienie obecnego kata przechylenia/obrotu pilki
    Quaternion_t dq = {1.0f, 0.5f*wx*dt, 0.5f*wy*dt, 0.5f*wz*dt};
    orientation = quaternion_multiply(&orientation, &dq);

    // normalize quaternion
    float mag = sqrt(orientation.w*orientation.w + orientation.x*orientation.x +
                     orientation.y*orientation.y + orientation.z*orientation.z);
    orientation.w /= mag;
    orientation.x /= mag;
    orientation.y /= mag;
    orientation.z /= mag;

    //Accelerometer drift correction
    TripleF accelNorm = accel;
    normalize_vector(&accelNorm);


    //obracamy wektor grawitacji, aby odpowiadal danym pobranym z czujnikow
    TripleF expectedGravity = {0.0f, 0.0f, -1.0f}; // Grawitacja w dół w układzie świata
    rotate_vector(&orientation, &expectedGravity); // Użyj bieżącej orientacji do obrócenia wektora

    //filtr komplementarny oparty na kwaternionach
    //korekta bledu orientacji zyroskopu przy uzyciu danych z akcelerometru
    TripleF error;
    error.x = accelNorm.y*expectedGravity.z - accelNorm.z*expectedGravity.y;
    error.y = accelNorm.z*expectedGravity.x - accelNorm.x*expectedGravity.z;
    error.z = accelNorm.x*expectedGravity.y - accelNorm.y*expectedGravity.x;

//dlugosc wektroa error jest proporcjonalna do kata pomiedzy wektorami
//korygujemy zyroskop(zmienna orientation), aby wyrownac z odczytem akcelerometru

    const float K = 0.02f; // Współczynnik filtru - powolna zmiana, zyroskop dominuje w krotkim okresie
    //krotkie uderzenie nie wplynie na to
    orientation.x += error.x * K;
    orientation.y += error.y * K;
    orientation.z += error.z * K;

    //Rotate accelerometer into world frame
    TripleF worldAccel = accel;
    rotate_vector(&orientation, &worldAccel);

    return worldAccel;
}