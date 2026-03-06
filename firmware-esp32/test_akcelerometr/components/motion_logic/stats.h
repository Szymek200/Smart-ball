#ifndef STATS_C_H
#define STATS_C_H

#include "circular_buffer.h" // Używa Sample_t, TripleF, MAX_EVENT_SAMPLES
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_log.h"
#include "esp_timer.h"

// --- STAŁE ---
#define BALL_MASS_KG            0.250f
#define HIT_START_THRESHOLD     1.5f
#define HIT_END_THRESHOLD       1.1f
#define NORMAL_SAMPLE_SKIP      100
#define MIN_POST_SAMPLES        50

// struktura stanu
typedef struct {
    float ballMass;
    
    // Bufor Historii
    Sample_t historyBuffer[RINGSIZE]; // Używamy RINGSIZE (256) jako bufor historii
    int historyIndex;                 // Wskaźnik na następne miejsce zapisu
    
    // Bufor Zdarzenia
    Sample_t eventBuffer[MAX_EVENT_SAMPLES]; 
    size_t eventSize;                  
    bool hasEventReady;                // Flaga dla writerTask
   
    int postRemaining;                 // Liczba próbek do zapisania po uderzeniu
    int normalCounter;                 // Licznik próbek do pominięcia
    bool hitActive;                    // Czy detekcja uderzenia jest aktywna
    
    uint32_t hitStartTime;             // Czas rozpoczęcia uderzenia (ms)
    uint32_t hitDuration;              // Całkowity czas trwania uderzenia (ms)

    float threshold_g; 
    float end_threshold_g;

} Stats_t;



// Inicjalizacja struktury statystyk
void motion_stats_init(Stats_t *s, float mass);

// Główna pętla logiczna - wywoływana dla każdej nowej próbki z czujnika
void stats_process_sample(Stats_t *s_ctx, Sample_t *sample);

// Flagi stanu
bool stats_background_ready(const Stats_t *s_ctx);
bool stats_event_ready(const Stats_t *s_ctx);
size_t stats_get_event_size(const Stats_t *s_ctx);
Sample_t* stats_get_event_ptr(Stats_t *s_ctx);
uint32_t stats_get_hit_duration(const Stats_t *s_ctx);


// Pomocnicze funkcje matematyczne
float stats_vector_lenght(TripleF r);
TripleF stats_mulvs(TripleF data, float scalar);
Sample_t stats_get_history_sample(const Stats_t *s_ctx, int offset);

#endif