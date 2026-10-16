#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdbool.h>

// Piny dla modułu MAX98357A
#define I2S_BCLK_PIN    46  // Bit Clock
#define I2S_LRC_PIN     1   // Left/Right Clock (Word Select)
#define I2S_DIN_PIN     6//4 - tymczasowa zmiana bo GSM   // Data In (użyjemy wolnego GPIO 4)
#define MAX_SD_PIN      2   // Shutdown / Tryb kanału

// Ustawienia pliku audio (dostosuj do pliku, który wgrasz)
#define AUDIO_SAMPLE_RATE   16000 // np. 16kHz
#define AUDIO_BITS          16    // 16-bitowe próbki

void audio_init(void);
void play_raw(const char* filepath); 
void stop_raw(void); 

#endif // AUDIO_PLAYER_H