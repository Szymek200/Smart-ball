#ifndef CIRCULAR_BUFFER_C_H
#define CIRCULAR_BUFFER_C_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> // dla size_t

#define PRE_SAMPLES  100
#define POST_SAMPLES 150
#define MAX_EVENT_SAMPLES (PRE_SAMPLES + POST_SAMPLES) 
#define HISTORY_BUFFER_SIZE 100 
#define RINGSIZE 256 //korzystam w ogole z tego???

// STRUKTURY DANYCH C 


//dane 3-osiowe (X, Y, Z).
typedef struct {
    float x, y, z;
} TripleF;


//pojedyncza probka pomiarowa
typedef struct {
    TripleF accel; // Przyspieszenie w globalnym układzie (g)
    TripleF gyro;  // Prędkość kątowa (rad/s)
    uint32_t timestamp; // w milisekundach
} Sample_t;


//struktura opisujaca wykryte zdarzenie
typedef struct {
    Sample_t* eventPtr; // Wskaźnik na eventBuffer
    size_t size;      
    uint32_t duration;
} HitEventInfo_t;

//Struktura zarządzająca buforem kołowym historii.
typedef struct {

    //ciagla pamiec krotkotrwala
    Sample_t historyBuffer[HISTORY_BUFFER_SIZE];    //probki przed uderzeniem
    int historyIndex; 
    
    Sample_t eventBuffer[MAX_EVENT_SAMPLES]; 
    size_t eventSize;   // Aktualna liczba zapisanych próbek w buforze zdarzenia

    bool hitActive;     // Czy aktualnie trwa nagrywanie uderzenia?
    int normalCounter;  //do sygnalizacji zapisu, gdy nie ma zdarzen

    bool hasEventReady; //czy pomiar zdarzenia zostal zakonczony
    int postRemaining;
    uint32_t hitStartTime; // Czas rozpoczęcia uderzenia (w ms, z esp_timer_get_time)
    uint32_t hitDuration;
    float threshold_g;      
    float end_threshold_g;  
    float ballMass;
    
} Stats_history_t;


// --- PROTOTYPY ---

//Pobiera próbkę z historii na podstawie przesunięcia wstecz (offset)
Sample_t history_get_sample(const Stats_history_t *stats, int offset);
// Dodaje nową próbkę do bufora historii
void history_update(Stats_history_t *stats, const Sample_t *s);

#endif