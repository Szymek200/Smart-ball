#include "communicate.h"
#include "normalize.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <errno.h>

static const char *TAG = "wifi_softap_tcp";

bool is_phone_connected = false;

//zmienne z measure.h
extern float config_wake_ths_g;
extern float config_sleep_ths_g;
extern int config_idle_time_s;
extern float CRASH_THRESHOLD_G;

static void wifi_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if(event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Client connected, Mac:"MACSTR" aid:%d", MAC2STR(event->mac), event->aid);
        is_phone_connected = true;
    }
    else if(event_id == WIFI_EVENT_AP_STADISCONNECTED) 
    {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Client disconnected, Mac:"MACSTR" aid:%d", MAC2STR(event->mac), event->aid);
        is_phone_connected = false;
    }
}

void wifi_init_softap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .password = WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = true,
            },
        },
    };

    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Hotspot started. SSID:%s Password:%s", WIFI_SSID, WIFI_PASS);
}

static void tcp_server_task(void * pvParameters)
{
    char rx_buffer[128]; 
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_in dest_addr_ip4;

    dest_addr_ip4.sin_addr.s_addr = htonl(INADDR_ANY); 
    dest_addr_ip4.sin_family = AF_INET;
    dest_addr_ip4.sin_port = htons(PORT);

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    //mozliwosc ponownego uzycia portu
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(listen_sock, (struct sockaddr*)&dest_addr_ip4, sizeof(dest_addr_ip4)) < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }
    
    listen(listen_sock, 1);

    bool is_client_connected = false;
    

    while(1)
    {
        ESP_LOGI(TAG, "Waiting for connection...");
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        
        xQueueReset(data_queue);
        is_client_connected = false;

        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }
        
        ESP_LOGI(TAG, "Phone connected! Transmission started.");
        is_client_connected = true;
        

        while(is_client_connected)
        {
            global_data_t data_to_send;
            
            if (xQueueReceive(data_queue, &data_to_send, portMAX_DELAY) == pdTRUE) 
            {
                size_t total_sent = 0;
                size_t to_send = sizeof(global_data_t);
                //rzutowanie na int
                uint8_t *data_ptr = (uint8_t *)&data_to_send;

                while (total_sent < to_send) {
                    int sent = send(sock, data_ptr + total_sent, to_send - total_sent, 0);
                    if (sent <= 0) {
                        ESP_LOGE(TAG, "Error sending data. Client probably disconnected. errno %d", errno);
                        is_client_connected = false;
                        break;
                    }
                    total_sent += sent;
                }

                if (!is_client_connected) break;
                  
            }
        }
        
        shutdown(sock, 0);
        close(sock);
        xQueueReset(data_queue);
        
    }
    
    close(listen_sock);
    vTaskDelete(NULL);
}

void tcp_server_start(void)
{
    xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);
}



// Zadanie obsługujące dedykowany serwer konfiguracji na porcie 3334
static void tcp_config_server_task(void * pvParameters)
{
    char rx_buffer[128];
    char tx_buffer[128];
    struct sockaddr_in dest_addr;

    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT_CONFIG); // Port 3334

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Config Socket unable to create");
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(listen_sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) < 0) {
        ESP_LOGE(TAG, "Config Socket unable to bind");
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }
    
    listen(listen_sock, 1);

    while(1)
    {
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        
        snprintf(tx_buffer, sizeof(tx_buffer), "CFG:%.2f:%.3f:%d:%.1f\n", 
                 config_wake_ths_g, config_sleep_ths_g, config_idle_time_s, CRASH_THRESHOLD_G);
        send(sock, tx_buffer, strlen(tx_buffer), 0);

       
        memset(rx_buffer, 0, sizeof(rx_buffer));
        int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0); // Blokujące czytanie z timeoutem

        if (len > 0) {
            rx_buffer[len] = '\0';

           //czulosc wybudzenia 
            if (strncmp(rx_buffer, "CMD:WAKE_THS:", 13) == 0) {
                float val;
                if (sscanf(rx_buffer, "CMD:WAKE_THS:%f", &val) == 1) {
                    config_wake_ths_g = val;
                    ESP_LOGW(TAG, "-> ZMIANA TCP: Nowy próg WYBUDZENIA IMU: %.2f G", config_wake_ths_g);
                }
            }
            // prog sily uderzenia
            else if (strncmp(rx_buffer, "CMD:HIT_THS:", 12) == 0) {
                float val;
                if (sscanf(rx_buffer, "CMD:HIT_THS:%f", &val) == 1) {
                    CRASH_THRESHOLD_G = val;
                    ESP_LOGW(TAG, "-> ZMIANA TCP: Nowy próg SILY ZDERZENIA: %.2f G", CRASH_THRESHOLD_G);
                }
            }
            // czas bazczynnosci
            else if (strncmp(rx_buffer, "CMD:IDLE_TIME:", 14) == 0) {
                int val;
                if (sscanf(rx_buffer, "CMD:IDLE_TIME:%d", &val) == 1) {
                    config_idle_time_s = val;
                    ESP_LOGW(TAG, "-> ZMIANA TCP: Nowy CZAS IDLE DO USPIENIA: %d sek", config_idle_time_s);
                }
            }
            //detekcja bezruchu
            else if (strncmp(rx_buffer, "CMD:SLEEP_THS:", 14) == 0) {
                float val;
                if (sscanf(rx_buffer, "CMD:SLEEP_THS:%f", &val) == 1) {
                    config_sleep_ths_g = val;
                    ESP_LOGW(TAG, "-> ZMIANA TCP: Nowy próg USYPIANIA IMU: %.3f G", config_sleep_ths_g);
                }
            }
               
            //po zapisie potwierdzamy - ok
            send(sock, "STATUS:OK\n", 10, 0);
        }

        
        close(sock);
    }
    close(listen_sock);
    vTaskDelete(NULL);
}

// Funkcja uruchamiająca drugi serwer
void tcp_config_server_start(void)
{
    xTaskCreate(tcp_config_server_task, "tcp_cfg_server", 4096, NULL, 4, NULL);
}