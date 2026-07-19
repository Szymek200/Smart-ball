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
            esp_mqtt_client_subscribe(mqtt_client, "esp32/device/audio", 0);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT Rozłączono z brokerem.");
            is_mqtt_connected = false;
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Otrzymano rozkaz z chmury. Temat: %.*s", event->topic_len, event->topic);
            if (strncmp(event->data, "PLAY", event->data_len) == 0) {
                ESP_LOGW(TAG, "!!! ROZKAZ GSM: Uruchamiam lokalizator dźwiękowy (Buzer/Głośnik) - niezaimplementowane!!!");
               // play_raw("/spiffs/dzwonek.raw"); 
            }
            break;
        default:
            break;
    }
}

void start_mqtt_client(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://broker.hivemq.com:1883", 
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void gsm_uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, MODEM_UART_TX_PIN, MODEM_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, 2048, 0, 0, NULL, 0));

    ESP_LOGI("SIM7070", "UART2 initialized");
}

static bool gsm_send_cmd(const char *cmd, char *response, size_t response_size, uint32_t timeout_ms)
{
    memset(response, 0, response_size);
    ESP_LOGI("SIM7070", "SEND: %s", cmd);
    uart_write_bytes(UART_NUM_2, cmd, strlen(cmd));

    uint32_t elapsed = 0;
    size_t total = 0;

    while (elapsed < timeout_ms)
    {
        int len = uart_read_bytes(UART_NUM_2, (uint8_t *)&response[total], response_size - total - 1, pdMS_TO_TICKS(200));
        if (len > 0)
        {
            total += len;
            response[total] = 0;
        }
        elapsed += 200;
    }

    ESP_LOGI("SIM7070", "RESPONSE (%d bytes):\n%s", (int)total, response);
    return (total > 0);
}

static bool gsm_wait_for_ok(void)
{
    char rsp[512];
    for (int i = 0; i < 10; i++)
    {
        memset(rsp, 0, sizeof(rsp));
        uart_write_bytes(UART_NUM_2, "AT\r\n", 4);
        vTaskDelay(pdMS_TO_TICKS(1000));

        int len = uart_read_bytes(UART_NUM_2, (uint8_t *)rsp, sizeof(rsp) - 1, pdMS_TO_TICKS(1000));
        if (len > 0)
        {
            rsp[len] = 0;
            ESP_LOGI("SIM7070", "AT TRY %d:\n%s", i + 1, rsp);
            if (strstr(rsp, "OK"))
            {
                ESP_LOGI("SIM7070", "Modem odpowiada poprawnie");
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return false;
}

// Główna konfiguracja przed-PPP (Ustawienie trybu SIM i APN w pamięci nieulotnej modemu)
void sim7070_prepare_carrier_settings(void)
{
    char rsp[256];
    ESP_LOGW("SIM7070", "--- KONFIGURACJA KARTY ORANGE IoT ORAZ FIZYCZNEGO SLOTU SIM ---");
    
    // 1. Wymuszenie fizycznego slotu SIM zamiast wewnętrznego chipu eSIM
    // AT+EXSIMSELECT=0 ustawia zewnętrzny slot SIM jako aktywny
    gsm_send_cmd("AT+EXSIMSELECT=0\r\n", rsp, sizeof(rsp), 3000);
    
    // Zabezpieczenie specyficzne dla SIM7070G (0 = Dual SIM wyłączone, używaj fizycznego pinoutu)
    gsm_send_cmd("AT+CSIDLEVEL=0\r\n", rsp, sizeof(rsp), 3000);

    // 2. Konfiguracja pasm radiowych dla technologii IoT w Polsce (LTE-M i NB-IoT)
    // 1 = CAT-M (LTE-M), 2 = NB-IoT, 3 = Oba aktywne
    gsm_send_cmd("AT+CMNB=3\r\n", rsp, sizeof(rsp), 3000); 

    // Upewniamy się, że modem nie próbuje rejestrować się w klasycznym GPRS (oszczędność czasu)
    gsm_send_cmd("AT+CNMP=38\r\n", rsp, sizeof(rsp), 3000); // 38 = Wybór wyłącznie trybów LTE (LTE-M / NB-IoT)

    // 3. Konfiguracja profilu APN w slocie nr 1 dla Orange IoT
    gsm_send_cmd("AT+CGDCONT=1,\"IP\",\"internet.iot\"\r\n", rsp, sizeof(rsp), 3000);

    ESP_LOGW("SIM7070", "-------------------------------------------------------------");
}

void startGSMtwo(void)
{
    // 1. Hardware Power-up sequence
    gpio_config_t pen_conf = {
        .pin_bit_mask = (1ULL << MODEM_PEN_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&pen_conf);
    gpio_set_level(MODEM_PEN_PIN, 1); // Włączenie zasilania regulatora
    vTaskDelay(pdMS_TO_TICKS(500));

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MODEM_PWRKEY_PIN),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);
    
    gpio_set_level(MODEM_PWRKEY_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Generowanie impulsu PWRKEY dla SIM7070G...");
    gpio_set_level(MODEM_PWRKEY_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1200)); 
    gpio_set_level(MODEM_PWRKEY_PIN, 1); 

    ESP_LOGI(TAG, "Czekam na bootowanie modemu (12s)...");
    vTaskDelay(pdMS_TO_TICKS(12000)); 

    // Tymczasowa inicjalizacja UART, aby wstrzyknąć konfigurację Orange IoT przed PPP
    gsm_uart_init();
    if (gsm_wait_for_ok()) {
        sim7070_prepare_carrier_settings();
    }
    // Usuwamy sterownik UART, ponieważ esp_modem stworzy własny wewnętrzny mechanizm DTE
    uart_driver_delete(UART_NUM_2);

    // 2. Konfiguracja interfejsu DTE dla esp_modem
    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.uart_config.tx_io_num = MODEM_UART_TX_PIN; 
    dte_config.uart_config.rx_io_num = MODEM_UART_RX_PIN; 
    dte_config.uart_config.port_num = UART_NUM_2; 
    dte_config.uart_config.baud_rate = 115200;

    // 3. Inicjalizacja interfejsu sieciowego PPP
    esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
    esp_netif_t *esp_netif = esp_netif_new(&netif_ppp_config);
    assert(esp_netif != NULL);

    // KLUCZOWE: Zmiana APN na "internet.iot" dla kart telemetrycznych Orange
    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG("internet.iot"); 

    // 4. Tworzenie instancji urządzenia SIM7070
    esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7000, &dte_config, &dce_config, esp_netif);
    assert(dce != NULL);

    // 5. Uruchomienie połączenia transmisji danych (PPP)
    ESP_LOGI(TAG, "Przełączanie modemu w DATA MODE (PPP)...");
    esp_err_t err = esp_modem_set_mode(dce, ESP_MODEM_MODE_DATA);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Modem SIM7070G pomyślnie zalogowany do Orange IoT! Stos TCP/IP aktywny.");
        start_mqtt_client();
    } else {
        ESP_LOGE(TAG, "Błąd negocjacji PPP: %s. Sprawdź kartę SIM Orange lub zasięg anteny LTE-M.", esp_err_to_name(err));
    }
}

void lte_sender_task(void *pvParameters) {
    gps_data received_data; 
    char json_payload[128];

    while (1) {
        if (gps_queue != NULL && xQueueReceive(gps_queue, &received_data, pdMS_TO_TICKS(1000)) == pdPASS) {
            if (is_mqtt_connected && received_data.latitude != 0.0f && received_data.longitude != 0.0f) {
                snprintf(json_payload, sizeof(json_payload), 
                         "{\"lat\":%.5f,\"lon\":%.5f,\"fix\":%d}", 
                         received_data.latitude, received_data.longitude, received_data.sats_in_use > 0 ? 1 : 0);
                
                esp_mqtt_client_publish(mqtt_client, "esp32/device/location", json_payload, 0, 1, 0);
                ESP_LOGI("LTE_MQTT", "Wysłano współrzędne przez sieć komórkową: %s", json_payload);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
}

void sim7070_full_test(void)
{
    char rsp[1024];

    //--------------------------------------------------
    // 1. SPRZĘTOWA KONTROLA ZASILANIA (PEN / PWREN)
    //--------------------------------------------------
    gpio_config_t pen_conf = {
        .pin_bit_mask = (1ULL << MODEM_PEN_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&pen_conf);

    ESP_LOGI("SIM7070", "Krok 1: Włączanie głównego zasilania (PEN -> HIGH)...");
    gpio_set_level(MODEM_PEN_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(500));

    //--------------------------------------------------
    // 2. FIZYCZNE URUCHOMIENIE PROCESORA MODEMU (PWRKEY)
    //--------------------------------------------------
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MODEM_PWRKEY_PIN),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);

    ESP_LOGI("SIM7070", "Krok 2: Generowanie impulsu startowego na PWRKEY...");
    gpio_set_level(MODEM_PWRKEY_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    gpio_set_level(MODEM_PWRKEY_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1200));

    gpio_set_level(MODEM_PWRKEY_PIN, 1);

    //--------------------------------------------------
    // 3. OCZEKIWANIE NA PROCEDURĘ STARTOWĄ MODEMU
    //--------------------------------------------------
    ESP_LOGI("SIM7070", "Krok 3: Oczekiwanie na pełny boot systemu operacyjnego modemu (15s)...");
    vTaskDelay(pdMS_TO_TICKS(15000));

    //--------------------------------------------------
    // 4. KONFIGURACJA INTERFEJSU UART Z OBSŁUGĄ RTS
    //--------------------------------------------------
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_RTS,  
        .rx_flow_ctrl_thresh = 122,
    };

    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, MODEM_UART_TX_PIN, MODEM_UART_RX_PIN, MODEM_UART_RTS_PIN, UART_PIN_NO_CHANGE));

    esp_err_t err = uart_driver_install(UART_NUM_2, 4096, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE("SIM7070", "Błąd instalacji sterownika UART: %s", esp_err_to_name(err));
        return;
    }
    
    ESP_LOGI("SIM7070", "UART2 (z obsługą sprzętową RTS) zainicjalizowany pomyślnie ");

    //--------------------------------------------------
    // 5. CZYSZCZENIE BUFORA I ODCZYT WIADOMOŚCI STARTOWYCH
    //--------------------------------------------------
    uint8_t boot[1024];
    int boot_len = uart_read_bytes(UART_NUM_2, boot, sizeof(boot) - 1, pdMS_TO_TICKS(2000));
    if (boot_len > 0)
    {
        boot[boot_len] = 0;
        ESP_LOGI("SIM7070", "ODCZYTANE LOGI BOOTOWANIA MODEMU:\n%s", (char *)boot);
    }

    //--------------------------------------------------
    // 6. TEST AT & DOSTOSOWANIE DLA ORANGE IoT
    //--------------------------------------------------
    if (!gsm_wait_for_ok())
    {
        ESP_LOGE("SIM7070", "BŁĄD: Modem nie odpowiedział na sekwencję handshake AT");
        return;
    }

    gsm_send_cmd("ATE0\r\n", rsp, sizeof(rsp), 3000);
    
    // WYMUSZENIE FIZYCZNEGO SLOTU SIM
    gsm_send_cmd("AT+EXSIMSELECT=0\r\n", rsp, sizeof(rsp), 3000);
    gsm_send_cmd("AT+CSIDLEVEL=0\r\n", rsp, sizeof(rsp), 3000);

    // WYMUSZENIE TRYBÓW IoT (LTE-M / NB-IoT)
    gsm_send_cmd("AT+CMNB=3\r\n", rsp, sizeof(rsp), 3000);
    gsm_send_cmd("AT+CNMP=38\r\n", rsp, sizeof(rsp), 3000);

    gsm_send_cmd("ATI\r\n", rsp, sizeof(rsp), 3000);
    gsm_send_cmd("AT+CPIN?\r\n", rsp, sizeof(rsp), 3000);
    gsm_send_cmd("AT+CSQ\r\n", rsp, sizeof(rsp), 3000);
    gsm_send_cmd("AT+COPS?\r\n", rsp, sizeof(rsp), 5000);

    //--------------------------------------------------
    // 7. PĘTLA REJESTRACJI W SIECI LTE
    //--------------------------------------------------
    bool registered = false;
    for (int i = 0; i < 45; i++) // Wydłużono czas dla sieci IoT (rejestracja eMTC może trwać dłużej)
    {
        gsm_send_cmd("AT+CEREG?\r\n", rsp, sizeof(rsp), 3000);
        if (strstr(rsp, ",1") || strstr(rsp, ",5"))
        {
            registered = true;
            break;
        }
        ESP_LOGW("SIM7070", "Oczekiwanie na logowanie do sieci Orange IoT (%d/45)...", i + 1);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (!registered)
    {
        ESP_LOGE("SIM7070", "BŁĄD: Brak rejestracji w sieci komórkowej");
        return;
    }

    ESP_LOGI("SIM7070", "Sukces! Modem zarejestrowany w sieci LTE IoT.");

    gsm_send_cmd("AT+CGATT?\r\n", rsp, sizeof(rsp), 3000);
    
    // USTAWIENIE ODPOWIEDNIEGO APN DLA ORANGE TELEMETRII
    gsm_send_cmd("AT+CGDCONT=1,\"IP\",\"internet.iot\"\r\n", rsp, sizeof(rsp), 5000);
    gsm_send_cmd("AT+CGDCONT?\r\n", rsp, sizeof(rsp), 5000);

    ESP_LOGI("SIM7070", "=======================================");
    ESP_LOGI("SIM7070", " DIAGNOSTYKA ZAKOŃCZONA PEŁNYM SUKCESEM ");
    ESP_LOGI("SIM7070", "=======================================");
}