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
#include <stdbool.h>

/*
#define MODEM_UART_TX_PIN      17  // ESP32-S3 bezpieczny cyfrowy TX
#define MODEM_UART_RX_PIN      18  // ESP32-S3 bezpieczny cyfrowy RX
#define MODEM_UART_RTS_PIN     19  // Wybierz wolne GPIO na płytce dla RTS (np. 19)
#define MODEM_PWRKEY_PIN       5   // Sterowanie włączeniem (P/R)
#define MODEM_PEN_PIN          4   // Zasilanie główne modułu (PEN)
*/

//dla dev board

#define MODEM_UART_TX_PIN      4//4  // Pin RX w modemie- czerowny
#define MODEM_UART_RX_PIN      5  // Pin TX w modemie - bialy
//#define MODEM_UART_RTS_PIN     19  // niepodlaczone
//#define MODEM_PWRKEY_PIN       4   // Sterowanie włączeniem (P/R)
//#define MODEM_PEN_PIN          5   // niepodlaczone

extern QueueHandle_t gps_queue; 

void startGSM(void);
void lte_sender_task(void *pvParameters);
void startGSMtwo(void);

void sim7070_full_test(void);
void gsm_uart_init(void);

#endif // GSM_H