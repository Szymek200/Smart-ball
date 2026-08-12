#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include "driver/ledc.h"

#define AUDIO_PIN           (4) 
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_DUTY_RES       LEDC_TIMER_8_BIT

void audio_init(void);
void play_raw(const char* filepath); 
void stop_raw(void); 

#endif // AUDIO_PLAYER_H

/*
deuter@MacBook-Air-3 sensors_testing % python $IDF_PATH/components/spiffs/spiffsgen.py 11534336 ./storage build/storage.bin --page-size=256 --obj-name-len=32 --meta-len=4 --use-magic --use-magic-len
deuter@MacBook-Air-3 sensors_testing % esptool --chip esp32s3 -p /dev/cu.usbmodem5B8E0911891 -b 460800 write-flash 0x410000 build/storage.bin
*/