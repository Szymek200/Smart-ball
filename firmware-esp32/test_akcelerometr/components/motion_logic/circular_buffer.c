#include "circular_buffer.h"

// Funkcja inicjalizująca historię 
void history_update(Stats_history_t *stats, const Sample_t *s) { 
    // 1. Zapisz próbkę do historii
    stats->historyBuffer[stats->historyIndex] = *s;
    
    stats->historyIndex = (stats->historyIndex + 1) % HISTORY_BUFFER_SIZE; 
}

// Funkcja do pobierania próbek z historii 
Sample_t history_get_sample(const Stats_history_t *stats, int offset) {
    // offset 0: ostatnia próbka, offset 1: przedostatnia, itd.

    int idx = (stats->historyIndex - 1 - offset + HISTORY_BUFFER_SIZE) % HISTORY_BUFFER_SIZE;
    return stats->historyBuffer[idx];
}
