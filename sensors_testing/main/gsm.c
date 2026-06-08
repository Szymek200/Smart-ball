#include "gsm.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "audio_player.h"

// Odwołanie do zmiennej kolejki zadeklarowanej w main.c
extern QueueHandle_t gps_queue;

static const char *TAG = "modem_gsm";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool is_mqtt_connected = false;

// Handler zdarzeń MQTT
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t )event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Połączono z brokerem! Subskrybuję kanał komend...");
            is_mqtt_connected = true;
           
            //subskrybujemy temat, na ktory aplikacja wysyla zadanie wylaczenia dzwieku
            esp_mqtt_client_subscribe(mqtt_client, "esp32/device/audio", 0);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT Rozłączono z brokerem.");
            is_mqtt_connected = false;
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Otrzymano rozkaz z chmury. Temat: %.*s", event->topic_len, event->topic);
            
         
            if (strncmp(event->data, "PLAY", event->data_len) == 0) {
                ESP_LOGW(TAG, "!!! ROZKAZ GSM: Uruchamiam lokalizator dźwiękowy (Buzer/Głośnik) !!!");
                
                play_wav("/spiffs/melodia.wav"); 
            }
            break;
        default:
            break;
    }
}

void start_mqtt_client(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        // Publiczny darmowy broker HiveMQ 
        .broker.address.uri = "mqtt://broker.hivemq.com:1883", 
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void startGSM(void)
{
    // Konfiguracja pinu PWRKEY dla układu SIM7070G 
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MODEM_PWRKEY_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);
    
    ESP_LOGI(TAG, "Generowanie impulsu PWRKEY dla SIM7070G...");
    gpio_set_level(MODEM_PWRKEY_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(MODEM_PWRKEY_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(2000)); 

    // Konfiguracja interfejsu DTE dla komunikacji z modemem
    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.uart_config.tx_io_num = MODEM_UART_TX_PIN; 
    dte_config.uart_config.rx_io_num = MODEM_UART_RX_PIN; 
    dte_config.uart_config.port_num = UART_NUM_2; 

    // Inicjalizacja interfejsu sieciowego PPP
    esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
    esp_netif_t *esp_netif = esp_netif_new(&netif_ppp_config);
    assert(esp_netif != NULL);

    // Definiujemy APN Twojego operatora GSM (np. "internet", "plus", "play")
    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG("internet"); 

    // Tworzenie instancji urządzenia SIM7070
    esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7000, &dte_config, &dce_config, esp_netif);
    assert(dce != NULL);

    // Uruchomienie połączenia transmisji danych w tle (PPP)
    esp_err_t err = esp_modem_set_mode(dce, ESP_MODEM_MODE_DATA);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Modem SIM7070G pomyślnie zalogowany do GPRS/LTE! Stos TCP/IP aktywny.");
        
        // jest internet, wlaczamy MQTT
        start_mqtt_client();
    } else {
        ESP_LOGE(TAG, "Błąd negocjacji PPP: %s. Sprawdź kartę SIM lub antenę LTE.", esp_err_to_name(err));
    }
}

// Zoptymalizowane zadanie wysyłania pozycji GPS przez sieć komórkową MQTT
void lte_sender_task(void *pvParameters) {
    gps_data received_data; 
    char json_payload[128];

    while (1) {
      
        if (gps_queue != NULL && xQueueReceive(gps_queue, &received_data, pdMS_TO_TICKS(1000)) == pdPASS) {
            
            // Wysyłamy pozycję tylko, jeśli klient MQTT jest połączony i mamy współrzędne
            if (is_mqtt_connected && received_data.latitude != 0.0f && received_data.longitude != 0.0f) {
                
                //pakowanie do json
                snprintf(json_payload, sizeof(json_payload), 
                         "{\"lat\":%.5f,\"lon\":%.5f,\"fix\":%d}", 
                         received_data.latitude, received_data.longitude, received_data.sats_in_use > 0 ? 1 : 0);
                
                
                esp_mqtt_client_publish(mqtt_client, "esp32/device/location", json_payload, 0, 1, 0);
                ESP_LOGI("LTE_MQTT", "Wysłano współrzędne przez sieć komórkową: %s", json_payload);
            }
        }
        
        // Pozycję GPS przez GSM wysyłamy co 4 sekundy, aby nie wysyłać identycznych punktów
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
}