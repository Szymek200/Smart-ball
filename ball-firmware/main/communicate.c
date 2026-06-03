#include "communicate.h"
#include "normalize.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <errno.h>

static const char *TAG = "wifi_softap_tcp";

static void wifi_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if(event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Client connected, Mac:"MACSTR" aid:%d", MAC2STR(event->mac), event->aid);
    }
    else if(event_id == WIFI_EVENT_AP_STADISCONNECTED) // Poprawiona literówka
    {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Client disconnected, Mac:"MACSTR" aid:%d", MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
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
    char rx_buffer[64];
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_in dest_addr_ip4;

  
    dest_addr_ip4.sin_addr.s_addr = htonl(INADDR_ANY); 
    dest_addr_ip4.sin_family = AF_INET;
    dest_addr_ip4.sin_port = htons(PORT);

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol); // Poprawione SOCK_STREAM
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(listen_sock, (struct sockaddr*)&dest_addr_ip4, sizeof(dest_addr_ip4)) < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }
    
    listen(listen_sock, 1);

    // Zmienne do obsługi logowania co 2 sekundy
    uint32_t log_timer = 0;

    while(1)
    {
        ESP_LOGI(TAG, "Waiting for connection...");
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }
      ESP_LOGI(TAG, "Phone connected! Transmission started.");

        while(1)
        {
            global_data_t data_to_send;
            
            // 1. Oczekiwanie na dane z kolejki (blokuje się, aż coś się pojawi)
            if (xQueueReceive(data_queue, &data_to_send, portMAX_DELAY) == pdTRUE) 
            {
                // 2. Wysyłanie danych binarnie przez TCP
                int sent = send(sock, &data_to_send, sizeof(data_to_send), 0);
                if(sent < 0)
                {
                    ESP_LOGE(TAG, "Error sending data. errno %d", errno);
                    break; // Przerwij wewnętrzną pętlę i czekaj na nowe połączenie
                }

                // (Opcjonalnie) Odbiór danych z telefonu nieblokujący
                int len = recv(sock, rx_buffer, sizeof(rx_buffer), MSG_DONTWAIT);
                if(len == 0) {
                    ESP_LOGI(TAG, "Phone disconnected");
                    break;
                }
            }
        }
        
        shutdown(sock, 0);
        close(sock);
    }
    
    close(listen_sock);
    vTaskDelete(NULL);
}


// Funkcja pomocnicza do uruchomienia zadania FreeRTOS z poziomu main.c
void tcp_server_start(void)
{
    xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);
}

