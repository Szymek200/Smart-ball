#include "communicate.h"
#include "normalize.h"

#include <string.h>
#include <errno.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "wifi_softap_tcp";

bool is_phone_connected = false;

extern float config_wake_ths_g;
extern float config_sleep_ths_g;
extern int config_idle_time_s;
extern float CRASH_THRESHOLD_G;

static void wifi_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if(event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        is_phone_connected = true;
    } else if(event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        is_phone_connected = false;
    }
}

void save_config_to_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("nvs", NVS_READWRITE, &my_handle);

    if (err == ESP_OK) {
        nvs_set_blob(my_handle, "wake_ths", &config_wake_ths_g, sizeof(float));
        nvs_set_blob(my_handle, "sleep_ths", &config_sleep_ths_g, sizeof(float));
        nvs_set_blob(my_handle, "idle_time", &config_idle_time_s, sizeof(int));
        nvs_set_blob(my_handle, "crash_ths", &CRASH_THRESHOLD_G, sizeof(float));
        
        err = nvs_commit(my_handle);
        if (err != ESP_OK) ESP_LOGE(TAG, "Błąd zapisu NVS commit!");
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Konfiguracja zapisana pomyślnie w NVS.");
    } else {
        ESP_LOGE(TAG, "Nie można otworzyć NVS do zapisu!");
    }
}

void wifi_init_softap(void)
{
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    strlcpy((char *)wifi_config.ap.ssid, WIFI_SSID, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(WIFI_SSID);
    wifi_config.ap.channel = WIFI_CHANNEL;
    wifi_config.ap.max_connection = MAX_STA_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        strlcpy((char *)wifi_config.ap.password, WIFI_PASS, sizeof(wifi_config.ap.password));
    }

    wifi_config.ap.pmf_cfg.capable = true;
    wifi_config.ap.pmf_cfg.required = false; 

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_max_tx_power(44);
}


static void tcp_server_task(void * pvParameters)
{
    //to do
}

void tcp_server_start(void) {
    xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);
}

static void tcp_config_server_task(void * pvParameters)
{
    char rx_buffer[128];
    char tx_buffer[128];
    struct sockaddr_in dest_addr;

    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT_CONFIG);

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(listen_sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) < 0) {
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
        int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);

        if (len > 0) {
            rx_buffer[len] = '\0';
            bool should_save = false;

            if (strncmp(rx_buffer, "CMD:WAKE_THS:", 13) == 0) {
                float val;
                if (sscanf(rx_buffer, "CMD:WAKE_THS:%f", &val) == 1) {
                    config_wake_ths_g = val;
                    lsm6dsv16x_configure_wakeup_threshold(config_wake_ths_g);
                    should_save = true;
                }
            }
            else if (strncmp(rx_buffer, "CMD:HIT_THS:", 12) == 0) {
                float val;
                if (sscanf(rx_buffer, "CMD:HIT_THS:%f", &val) == 1) {
                    CRASH_THRESHOLD_G = val;
                    should_save = true;
                }
            }
            else if (strncmp(rx_buffer, "CMD:IDLE_TIME:", 14) == 0) {
                int val;
                if (sscanf(rx_buffer, "CMD:IDLE_TIME:%d", &val) == 1) {
                    config_idle_time_s = val;
                    should_save = true;
                }
            }
            else if (strncmp(rx_buffer, "CMD:SLEEP_THS:", 14) == 0) {
                float val;
                if (sscanf(rx_buffer, "CMD:SLEEP_THS:%f", &val) == 1) {
                    config_sleep_ths_g = val;
                    should_save = true;
                }
            }
               
            if (should_save) {
                save_config_to_nvs();
            }

            send(sock, "STATUS:OK\n", 10, 0);
        }
        close(sock);
    }
    close(listen_sock);
    vTaskDelete(NULL);
}

void tcp_config_server_start(void) {
    xTaskCreate(tcp_config_server_task, "tcp_cfg_server", 4096, NULL, 4, NULL);
}