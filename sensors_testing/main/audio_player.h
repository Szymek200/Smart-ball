#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdbool.h>

// Piny dla modułu MAX98357A
#define I2S_BCLK_PIN    14 
#define I2S_LRC_PIN     13 
#define I2S_DIN_PIN     15
#define MAX_SD_PIN      16  

//loudness - 16 czy wykorzystujemy???

#define AUDIO_SAMPLE_RATE   16000 
#define AUDIO_BITS          16   

void audio_init(void);
void play_raw(const char* filepath); 
void stop_raw(void); 

#endif // AUDIO_PLAYER_H