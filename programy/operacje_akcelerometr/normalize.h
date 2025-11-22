
#ifndef NORMALIZE
#define NORMALIZE

#include "circularBuffer.h"
#include "math.h"

#define DEG_TO_RAD 0.017453292519943295  // π / 180

struct Quaternion {
    float w, x, y, z;
};

extern Quaternion orientation;
//Quaternion orientation = {1, 0, 0, 0};  // identity quaternion

void normalizeVector(TripleF &v);
Quaternion qMultiply(const Quaternion& a, const Quaternion& b);


void rotateVector(const Quaternion &q, TripleF &v) ;

TripleF updateWorldAccel(TripleF gyro, TripleF accel, float dt);


#endif