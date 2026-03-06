#ifndef NORMALIZE_C_H
#define NORMALIZE_C_H

#include "circular_buffer.h"
#include <math.h>

// Konwersja stopni na radiany
#define DEG_TO_RAD 0.017453292519943295f

//Kwaternion (w, x, y, z)
typedef struct {
    float w, x, y, z;
} Quaternion_t;

//Deklaracja zmiennej globalnej przechowującej aktualną orientację piłki.
extern Quaternion_t orientation; 

// Prototypy funkcji C
//normalizacja wektora
void normalize_vector(TripleF *v);
// Łączy dane z żyroskopu i akcelerometru, aby usunąć grawitację.
TripleF update_world_accel(TripleF gyro, TripleF accel, float dt);

#endif