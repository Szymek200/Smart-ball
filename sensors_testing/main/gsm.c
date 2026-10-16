#include "gsm.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "audio_player.h"
#include <string.h>

extern QueueHandle_t gps_queue;

static const char *TAG = "modem_gsm";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool is_mqtt_connected = false;

// --- FUNKCJE MQTT ---

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t )event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Połączono z brokerem! Subskrybuję kanał komend...");
            is_mqtt_connected = true;
            esp_mqtt_client_subscribe(mqtt_client, "esp32/device/audio", 0);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT Rozłączono z brokerem.");
            is_mqtt_connected = false;
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Otrzymano rozkaz z chmury. Temat: %.*s", event->topic_len, event->topic);
            if (strncmp(event->data, "PLAY", 4) == 0) {
                ESP_LOGW(TAG, "!!! ROZKAZ GSM: Uruchamiam lokalizator dźwiękowy !!!");
                play_raw("/spiffs/dzwonek.raw"); 
            } 
            else if (strncmp(event->data, "STOP", 4) == 0) {
                ESP_LOGW(TAG, "!!! ROZKAZ GSM: Zatrzymuję dźwięk !!!");
                stop_raw();
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT BŁĄD KRYTYCZNY! Typ błędu: %d", event->error_handle->error_type);
            break;

        default:
            break;
    }
}

void start_mqtt_client(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        //.broker.address.uri = "mqtt://broker.hivemq.com:1883", 
        .broker.address.uri = "mqtt://broker.emqx.io:1883",
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// --- FUNKCJE POMOCNICZE AT ---

#define UART_PORT_NUM UART_NUM_1
#define BUF_SIZE 1024

bool sim7070g_send_cmd_check(const char *cmd, const char *expected_resp, uint32_t timeout_ms) {
    char data[BUF_SIZE] = {0};
    size_t total = 0;

    ESP_LOGI(TAG, "Wysyłanie: %s", cmd);
    uart_flush_input(UART_PORT_NUM);
    uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    uart_write_bytes(UART_PORT_NUM, "\r\n", 2);
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));

    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        uint8_t temp[128];
        int len = uart_read_bytes(UART_PORT_NUM, temp, sizeof(temp), pdMS_TO_TICKS(100));

        if (len > 0) {
            size_t available = sizeof(data) - 1 - total;
            if ((size_t)len > available) len = (int)available;

            if (len > 0) {
                memcpy(data + total, temp, len);
                total += len;
                data[total] = '\0'; 
            }

            if (strstr(data, expected_resp) != NULL) {
                ESP_LOGI(TAG, "Odpowiedź OK:\n%s", data);
                return true;
            }

            if (strstr(data, "\r\nERROR\r\n") != NULL ||
                strstr(data, "+CME ERROR:") != NULL ||
                strstr(data, "+CMS ERROR:") != NULL) {
                ESP_LOGE(TAG, "Modem zwrócił błąd:\n%s", data);
                return false;
            }

            if (total >= sizeof(data) - 1) break;
        }
    }
    return false;
}

// --- GŁÓWNA INICJALIZACJA GSM ---

void startGSMtwo(void) {
    // 1. Czekamy na samoistny start modemu (bez sterowania GPIO)
    ESP_LOGI(TAG, "Oczekiwanie na uruchomienie modemu SIM7070G (10s)...");
    vTaskDelay(pdMS_TO_TICKS(10000)); 

    // 2. Tymczasowa inicjalizacja UART do wstrzyknięcia działającej konfiguracji
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB
    };

    uart_driver_install(UART_PORT_NUM, BUF_SIZE, 0, 0, NULL, 0);
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_set_pin(UART_PORT_NUM, MODEM_UART_TX_PIN, MODEM_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    ESP_LOGI(TAG, "Synchronizacja Auto-Baud...");
    bool sync_ok = false;
    for(int i = 0; i < 10; i++) {
        uart_flush(UART_PORT_NUM); 
        if (sim7070g_send_cmd_check("AT", "OK", 3000)) {
            sync_ok = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (sync_ok) {
        ESP_LOGI(TAG, "Wstrzykiwanie sprawdzonej konfiguracji Orange IoT...");
        
        sim7070g_send_cmd_check("AT+CFUN=0", "OK", 10000); 
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        sim7070g_send_cmd_check("AT+CNMP=38", "OK", 2000); 
        sim7070g_send_cmd_check("AT+CMNB=3", "OK", 2000);  
        sim7070g_send_cmd_check("AT+CGDCONT=1,\"IP\",\"iot\"", "OK", 2000); 
        sim7070g_send_cmd_check("AT+CGAUTH=1,1,\"iot\",\"\"", "OK", 2000); 
        
        sim7070g_send_cmd_check("AT+CFUN=1", "OK", 10000); 
        
        ESP_LOGI(TAG, "Oczekiwanie na rejestrację w sieci (do kilkudziesięciu sekund)...");
        bool network_ok = false;
        for(int i = 0; i < 60; i++) {
            if (sim7070g_send_cmd_check("AT+CGATT?", "+CGATT: 1", 2000)) {
                ESP_LOGI(TAG, "Sukces! Moduł podłączony do sieci Orange!");
                network_ok = true;
                break;
            }
        }
        
        if(!network_ok) {
            ESP_LOGE(TAG, "Nie udało się połączyć z siecią. Przerywam.");
            uart_driver_delete(UART_PORT_NUM);
            return;
        }
    } else {
        ESP_LOGE(TAG, "Brak komunikacji z modemem! Sprawdź piny RX/TX.");
        uart_driver_delete(UART_PORT_NUM);
        return;
    }

    // 3. Twardy reset UART przed oddaniem go do esp_modem (PPP)
    ESP_LOGI(TAG, "Przekazywanie kontroli do systemowego stosu PPP (esp_modem)...");
    uart_driver_delete(UART_PORT_NUM);
    gpio_reset_pin(MODEM_UART_TX_PIN);
    gpio_reset_pin(MODEM_UART_RX_PIN);
    vTaskDelay(pdMS_TO_TICKS(500)); 

    // 4. Uruchomienie interfejsu sieciowego PPP
    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.uart_config.tx_io_num = MODEM_UART_TX_PIN; 
    dte_config.uart_config.rx_io_num = MODEM_UART_RX_PIN; 
    dte_config.uart_config.port_num = UART_PORT_NUM; 
    dte_config.uart_config.baud_rate = 115200;

    esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
    esp_netif_t *esp_netif = esp_netif_new(&netif_ppp_config);
    
    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG("iot"); 
    esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7000, &dte_config, &dce_config, esp_netif);
    
    ESP_LOGI(TAG, "Przełączanie modemu w tryb DATA (PPP)...");
    esp_err_t err = esp_modem_set_mode(dce, ESP_MODEM_MODE_DATA);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Połączenie PPP zestawione! Uruchamiam klienta MQTT...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        start_mqtt_client();
    } else {
        ESP_LOGE(TAG, "Błąd negocjacji PPP: %s", esp_err_to_name(err));
    }
}

// --- TASK WYSYŁAJĄCY DANE LOKALIZACYJNE ---

void lte_sender_task(void *pvParameters) {
    char json_payload[128];
    float sim_lat = 52.2297f; 
    float sim_lon = 21.0122f;

    while (1) {
        if (is_mqtt_connected) {
            sim_lat += 0.0001f; 
            sim_lon += 0.0001f;

            snprintf(json_payload, sizeof(json_payload), 
                     "{\"lat\":%.5f,\"lon\":%.5f,\"fix\":1}", 
                     sim_lat, sim_lon);
            
            esp_mqtt_client_publish(mqtt_client, "esp32/device/location", json_payload, 0, 1, 0);
            ESP_LOGI("LTE_MQTT", "Wysłano współrzędne przez MQTT: %s", json_payload);
        } else {
            ESP_LOGW("LTE_MQTT", "Brak połączenia MQTT. Oczekuję...");
        }
        
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
}