#include "stats.h"

static const char *TAG_STATS = "STATS";


//dlugosc wektora
float stats_vector_lenght(TripleF r)
{
    return sqrtf(r.x * r.x + r.y * r.y + r.z * r.z);
}

//wektor razy skalar
TripleF stats_mulvs(TripleF data, float scalar)
{
    TripleF result;
    result.x = data.x * scalar;
    result.y = data.y * scalar;
    result.z = data.z * scalar;
    return result;
}

// Pobieranie próbki z historii (Little Endian Indexing)
Sample_t stats_get_history_sample(const Stats_t *s_ctx, int offset) {
    // offset 0: ostatnia próbka; offset 1: przedostatnia,
    // historyIndex to NEXT WRITE position. Musimy cofnąć się od niego.

    //dodajemy RINGSIZE, gdybysmy sie za bardzo cofneli - zapetlenie
    int idx = (s_ctx->historyIndex - 1 - offset + RINGSIZE) % RINGSIZE;
    return s_ctx->historyBuffer[idx];
}


// inicjalizacja
void motion_stats_init(Stats_t *s, float mass) {
    s->ballMass = mass;
    s->historyIndex = 0;
    s->eventSize = 0;
    s->hasEventReady = false;
    s->postRemaining = 0;
    s->normalCounter = 0;
    s->hitActive = false;
    s->hitStartTime = 0;
    s->hitDuration = 0;
    s->threshold_g = HIT_START_THRESHOLD;
    s->end_threshold_g = HIT_END_THRESHOLD;
}

// Metoda do zapisu historycznego (wywoływana w processSample)
static void update_history(Stats_t *s_ctx, const Sample_t *s) {
    s_ctx->historyBuffer[s_ctx->historyIndex] = *s;
    s_ctx->historyIndex = (s_ctx->historyIndex + 1) % RINGSIZE;
}


// logika zdarzenia
// Rozpoczyna nagrywanie zdarzenia po wykryciu przekroczenia progu
static void start_hit_event(Stats_t *s_ctx) {
   s_ctx->eventSize = 0;
   s_ctx->hitActive = true;
   s_ctx->postRemaining = MIN_POST_SAMPLES;
   s_ctx->hitStartTime = esp_timer_get_time() / 1000; // Czas w ms

    // Kopiuj PRE_SAMPLES z historii
    for (int i = PRE_SAMPLES; i > 0; i--) {

 
        if (s_ctx->eventSize < MAX_EVENT_SAMPLES) {
            // offset i - 1: ostatnia próbka ma offset 0
            s_ctx->eventBuffer[s_ctx->eventSize++] = stats_get_history_sample(s_ctx, i - 1); 
        } else {
             break;
        }
    }

    ESP_LOGI(TAG_STATS, "Impact detected -> Starting event capture");
}

// Kończy nagrywanie i oznacza dane jako gotowe do wysyłki
static void end_hit_event(Stats_t *s_ctx) {
    s_ctx->hitActive = false;
    s_ctx->hasEventReady = true;

    uint32_t hitEndTime = esp_timer_get_time() / 1000;
    s_ctx->hitDuration = hitEndTime - s_ctx->hitStartTime;
        
    ESP_LOGI(TAG_STATS, "Event completed (Size: %u, Duration: %u ms)", s_ctx->eventSize, s_ctx->hitDuration);
}

// Główna funkcja analizująca próbkę - wywoływana w pętli 100Hz
void stats_process_sample(Stats_t *s_ctx, Sample_t *s) {
    // Zapisz do historii
    update_history(s_ctx, s); 
    
    // Oblicz całkowitą siłę (Magnituda wektora)
    float total_magnitude = stats_vector_lenght(s->accel); 
    
    //  Usuń grawitację (odejmij 1G)
    float dynAcc = fabsf(total_magnitude - 1.0f);

    // Filtr szumów (martwa strefa): małe drgania zerujemy
    if (dynAcc < 0.1f) dynAcc = 0.0f;

    // LOGIKA DETEKCJI

    if (!s_ctx->hitActive && dynAcc > s_ctx->threshold_g) {
        start_hit_event(s_ctx);
        // Zabezpieczenie przed wyjściem poza tablicę już przy pierwszej próbce
        if (s_ctx->eventSize < MAX_EVENT_SAMPLES) {
            s_ctx->eventBuffer[s_ctx->eventSize++] = *s;
        }
    } 
    else if (s_ctx->hitActive) {
       // Zawsze zapisuj próbkę, dopóki jest miejsce
       if (s_ctx->eventSize < MAX_EVENT_SAMPLES) {
           s_ctx->eventBuffer[s_ctx->eventSize++] = *s;
       }

       if (dynAcc > s_ctx->end_threshold_g) {
           s_ctx->postRemaining = MIN_POST_SAMPLES;
       } else {
           // Sygnał jest słaby - odliczamy próbki końcowe
           s_ctx->postRemaining--;
       }

       // Warunek zakończenia: albo minął czas "ogona" (postRemaining <= 0),
       // albo skończyło się miejsce w buforze.
       if (s_ctx->postRemaining <= 0 || s_ctx->eventSize >= MAX_EVENT_SAMPLES) {
            end_hit_event(s_ctx);   
       }
    }
    else {
        s_ctx->normalCounter++;
    }
}

// FLAGI STANU (DLA Tasków ODBIORCZYCH)

bool stats_background_ready(const Stats_t *s_ctx) { 
    return (s_ctx->normalCounter >= NORMAL_SAMPLE_SKIP); 
}

bool stats_event_ready(const Stats_t *s_ctx) {
    return s_ctx->hasEventReady;
}

size_t stats_get_event_size(const Stats_t *s_ctx) { 
    return s_ctx->eventSize; 
}

Sample_t* stats_get_event_ptr(Stats_t *s_ctx) {
    s_ctx->hasEventReady = false; // reset flag po odebraniu
    return s_ctx->eventBuffer;           
}

uint32_t stats_get_hit_duration(const Stats_t *s_ctx) {
    return s_ctx->hitDuration;
}