#include "gsm.h"
#include "driver/uart.h"


QueueHandle_t gps_queue = NULL;

static const char *TAG = "modem_gsm";

void startGSM(void)
{
  
    esp_netif_init();
    esp_event_loop_create_default();

 
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MODEM_PWRKEY_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);
    
    ESP_LOGI(TAG, "Uruchamianie modemu SIM7070G...");
    gpio_set_level(MODEM_PWRKEY_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(MODEM_PWRKEY_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(2000)); 

   
    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.uart_config.tx_io_num = MODEM_UART_TX_PIN;
    dte_config.uart_config.rx_io_num = MODEM_UART_RX_PIN;
    

    dte_config.uart_config.port_num = UART_NUM_2; 

    esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
    esp_netif_t *esp_netif = esp_netif_new(&netif_ppp_config);
    assert(esp_netif != NULL);


    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG("internet"); // Wpisz APN swojego operatora

   
    esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7000, &dte_config, &dce_config, esp_netif);
    assert(dce != NULL);

   
    esp_err_t err = esp_modem_set_mode(dce, ESP_MODEM_MODE_DATA);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Modem połączony z siecią komórkową! Możesz używać standardowych gniazd TCP/IP.");
    } else {
        ESP_LOGE(TAG, "Nie udało się ustanowić połączenia PPP: %s", esp_err_to_name(err));
    }
}

void lte_sender_task(void *pvParameters) {

    gps_data received_data; 

    while (1) {
        if (gps_queue != NULL && xQueueReceive(gps_queue, &received_data, portMAX_DELAY) == pdPASS) {
          
            if (received_data.latitude != 0.0f && received_data.longitude != 0.0f) {
       
                ESP_LOGI("LTE", "Odebrano z kolejki: Lat: %.05f, Lon: %.05f. Wysyłam przez LTE...", 
                         received_data.latitude, received_data.longitude);
                
             
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100)); 
        }
    }
}