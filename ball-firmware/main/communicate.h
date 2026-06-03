#ifndef COMMUNICATE_H
#define COMMUNICATE_H

#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "measure.h" 

extern QueueHandle_t data_queue;

#define WIFI_SSID       "ESP32-S3-Hotspot"
#define WIFI_PASS       "12345678"
#define WIFI_CHANNEL    1
#define MAX_STA_CONN    4
#define PORT            3333

void wifi_init_softap(void);
void tcp_server_start(void); 


#endif // COMMUNICATE_H