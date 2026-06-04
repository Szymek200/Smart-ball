#ifndef GSM_H
#define GSM_H

#include "esp_modem_api.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_event.h"       
#include "esp_netif_ppp.h"   
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"  
#include "measure.h"         

#define MODEM_UART_TX_PIN      17
#define MODEM_UART_RX_PIN      16
#define MODEM_PWRKEY_PIN       4


extern QueueHandle_t gps_queue; 

void startGSM(void);
void lte_sender_task(void *pvParameters);

#endif // GSM_H