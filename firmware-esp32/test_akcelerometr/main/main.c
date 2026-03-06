#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h" 
#include "driver/uart.h"     
#include "h3lis331dl_reg.h"
#include "L3G4200D.h"
#include "normalize.h"
#include "stats.h"

static const char *TAG = "SENSOR_TEST";
bool is_sleep_enabled = true;

// KONFIGURACJA
#define I2C_MASTER_SCL_IO           22    
#define I2C_MASTER_SDA_IO           21    
#define I2C_MASTER_NUM              I2C_NUM_0 
#define I2C_MASTER_FREQ_HZ          400000 
#define UDP_PORT                    5000
#define AP_SSID                     "SmartBall_ESP32"
#define AP_PASS                     "12345678"

#define ACCEL_SCALE_CORRECTION  1.0f

//usypianie
#define ACCEL_INT_GPIO              GPIO_NUM_32 
#define ACCEL_INT_PIN_MASK          (1ULL << ACCEL_INT_GPIO)
volatile uint64_t global_idle_timeout_us = 30 * 1000000; // Domyślnie 30 sekund
#define WAKEUP_THRESHOLD_G          1.2f           // Próg wybudzenia 

// GLOBALNE UCHWYTY
static TaskHandle_t udp_task_handle = NULL;
static SemaphoreHandle_t data_mutex = NULL;
static Sample_t current_sample_data;
static Stats_t stats_context;
static stmdev_ctx_t accel_ctx_global;
static l3g4200d_dev_t gyro_dev_global;

//prog wykrycia uderzenia
float global_hit_threshold_g = 1.2f;

//adaptery i2c
//funkcje posredniczace w zapisie do rejestrow czujnika prez I2C
int32_t platform_i2c_write(void *handle, uint8_t reg, const uint8_t *data, uint16_t len) {
    uint8_t slave_addr = (uint8_t)(uint32_t)handle;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, slave_addr | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write(cmd, (uint8_t *)data, len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK) ? 0 : -1;
}

int32_t platform_i2c_read(void *handle, uint8_t reg, uint8_t *data, uint16_t len) {
    uint8_t slave_addr = (uint8_t)(uint32_t)handle;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, slave_addr | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, slave_addr | I2C_MASTER_READ, true);
    if (len > 1) i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK) ? 0 : -1;
}

//wysylka binarna eventu
void udp_send_event_data_binary(int sock, struct sockaddr_in *dest_addr, Sample_t *data, size_t count, uint32_t duration) {
    //nagłowek
    struct { uint32_t magic; uint32_t count; uint32_t duration; } header = {0xDEADBEEF, (uint32_t)count, duration};
    sendto(sock, &header, sizeof(header), 0, (struct sockaddr *)dest_addr, sizeof(*dest_addr));
    
    //Wysylka grupowa
    const int samples_per_packet = 10;
    size_t samples_sent = 0;

    while (samples_sent < count) {
        size_t to_send = count - samples_sent;
        if (to_send > samples_per_packet) to_send = samples_per_packet;

        // Wysyłamy blok pamięci
        int bytes_sent = sendto(sock, &data[samples_sent], to_send * sizeof(Sample_t), 0, 
                                (struct sockaddr *)dest_addr, sizeof(*dest_addr));
        
        if (bytes_sent < 0) {
            ESP_LOGE(TAG, "Błąd wysyłki grupowej: errno %d", errno);
            break;
        }

        samples_sent += to_send;
        vTaskDelay(pdMS_TO_TICKS(5)); // Mała przerwa, aby telefon nadążył z odbiorem
    }

    //stopka
    uint32_t footer = 0xEEEEEEEE;
    sendto(sock, &footer, 4, 0, (struct sockaddr *)dest_addr, sizeof(*dest_addr));
}

// Parsuje komendy tekstowe przychodzące z telefonu
// Aktualizuje zmienne globalne
void process_incoming_command(char *cmd_buffer, int len) {
    cmd_buffer[len] = '\0'; // Zabezpieczenie stringa
    ESP_LOGI(TAG, "Otrzymano komendę: %s", cmd_buffer);

    //Ustawienie progu wybudzania (Hardware INT1 Threshold)
    if (strncmp(cmd_buffer, "CMD:THS:", 8) == 0) {
        int new_ths = atoi(cmd_buffer + 8);
        if (new_ths >= 1 && new_ths <= 127) {
            if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                h3lis331dl_int1_threshold_set(&accel_ctx_global, (uint8_t)new_ths);
                xSemaphoreGive(data_mutex);
                ESP_LOGI(TAG, "HARDWARE: Zaktualizowano próg wybudzania na: %d", new_ths);
            }
        }
    }
    
    //Ustawienie progu detekcji uderzenia (Software)
    else if (strncmp(cmd_buffer, "CMD:HIT:", 8) == 0) {
        float new_g = atof(cmd_buffer + 8); 
        if (new_g > 0.5f && new_g < 100.0f) {
            
            if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                stats_context.threshold_g = new_g; 
                xSemaphoreGive(data_mutex);
                ESP_LOGI(TAG, "SOFTWARE: Zaktualizowano próg detekcji na: %.2f G", new_g);
            }
        }
    }
    // Próg ZAKOŃCZENIA (Software Hit End)
    // Format: CMD:END:1.1
    else if (strncmp(cmd_buffer, "CMD:END:", 8) == 0) {
        float new_end = atof(cmd_buffer + 8);
        if (new_end > 0.1f && new_end < 50.0f) {
            if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                stats_context.end_threshold_g = new_end;
                xSemaphoreGive(data_mutex);
                ESP_LOGI(TAG, "SOFT: Zaktualizowano próg END na: %.2f G", new_end);
            }
        }
    }

    //Czas bezczynności (Idle Timeout) w sekundach
    // Format: CMD:IDLE:60
    else if (strncmp(cmd_buffer, "CMD:IDLE:", 9) == 0) {
        int seconds = atoi(cmd_buffer + 9);
        if (seconds >= 5 && seconds <= 3600) { // Min 5s, Max 1h
            if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                global_idle_timeout_us = (uint64_t)seconds * 1000000;
                xSemaphoreGive(data_mutex);
                ESP_LOGI(TAG, "SLEEP: Zaktualizowano czas bezczynności na: %d s", seconds);
            }
        }
    }
    else if (strncmp(cmd_buffer, "CMD:SLEEP_EN:", 13) == 0) {
        int val = atoi(cmd_buffer + 13);
        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            is_sleep_enabled = (val == 1);
            xSemaphoreGive(data_mutex);
            if (is_sleep_enabled) {
                ESP_LOGI(TAG, "SLEEP: Automatyczne usypianie WŁĄCZONE");
            } else {
                ESP_LOGI(TAG, "SLEEP: Automatyczne usypianie WYŁĄCZONE (Tryb ciągły)");
            }
        }
    }
}


static void udp_server_task(void *pvParameters) {
    ESP_LOGI(TAG, "UDP: Start gniazda...");
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "UDP: Błąd socket: %d", errno);
        vTaskDelete(NULL);
        return;
    }

    //timeout odbioru
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 1000; // 1 ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(UDP_PORT);
    dest_addr.sin_addr.s_addr = inet_addr("192.168.4.2");

    struct sockaddr_in my_addr;
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    my_addr.sin_port = htons(UDP_PORT);

    if (bind(sock, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
        ESP_LOGE(TAG, "UDP: Błąd Bind: %d", errno);
    }

    ESP_LOGI(TAG, "UDP: Gniazdo gotowe (TX/RX).");
    
    char rx_buffer[64]; // Bufor na komendy

   while (1) {

        //Sprawdź czy przyszły jakieś komendy
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

        if (len > 0) {
            // Przyszła komenda!
            process_incoming_command(rx_buffer, len);
        }

        //Czekaj na sygnał z taska sensorów, żeby wysłać dane
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        
        //Sprawdź czy jest gotowe zdarzenie
        if (stats_event_ready(&stats_context)) {
            size_t ev_size = stats_get_event_size(&stats_context);
            uint32_t ev_dur = stats_get_hit_duration(&stats_context);

            //Ignoruj jeśli rozmiar jest podejrzanie maly
            if (ev_size > 10 && ev_dur > 0) {
                Sample_t* ev_data = stats_get_event_ptr(&stats_context);
                
                // Zwalniamy mutex przed wysyłką, żeby nie blokować sensorów
                xSemaphoreGive(data_mutex);
                
                udp_send_event_data_binary(sock, &dest_addr, ev_data, ev_size, ev_dur);
                
                // Czyścimy wszystkie oczekujące powiadomienia       
                ulTaskNotifyValueClear(NULL, 0xffffffff);
                continue; 
            }
        }

        //Standardowa wysyłka (Live)
        Sample_t temp = current_sample_data;
        xSemaphoreGive(data_mutex);

        uint8_t packet[32];
        memcpy(packet, "SMPL", 4);
        memcpy(packet + 4, &temp, sizeof(Sample_t));
        sendto(sock, packet, sizeof(packet), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }
}
}


// Przygotowuje czujniki i procesor do głębokiego uśpienia
void enter_deep_sleep(void) {
    
    ESP_LOGI(TAG, "Przygotowanie do uśpienia...");

    //zyroskop
    uint8_t pd_cmd = 0x00;
    platform_i2c_write((void*)(0x69 << 1), 0x20, &pd_cmd, 1); 

    //Skonfiguruj H3LIS331DL do wybudzania
    
    //Reset filtrów
    h3lis331dl_hp_reset_get(&accel_ctx_global);

    //Włącz filtr HP na INT1 
    h3lis331dl_hp_path_set(&accel_ctx_global, H3LIS331DL_HP_ON_INT1); 

    //Zwiększamy ODR na chwilę, żeby filtr szybciej się ustabilizował
    h3lis331dl_data_rate_set(&accel_ctx_global, H3LIS331DL_ODR_100Hz); 

    //Ustaw próg wybudzenia (np. 2 * 0.78G = 1.5G)
    h3lis331dl_int1_threshold_set(&accel_ctx_global, 2); 
    h3lis331dl_int1_dur_set(&accel_ctx_global, 0);

    //Konfiguracja zdarzeń (XHIE, YHIE, ZHIE)
    h3lis331dl_int1_on_th_conf_t int1_conf = {
        .int1_xlie = 0, .int1_xhie = 1,
        .int1_ylie = 0, .int1_yhie = 1,
        .int1_zlie = 0, .int1_zhie = 1
    };
    h3lis331dl_int1_on_threshold_conf_set(&accel_ctx_global, int1_conf);

    //Route do pinu i polaryzacja
    h3lis331dl_pin_int1_route_set(&accel_ctx_global, H3LIS331DL_PAD1_INT1_SRC);
    h3lis331dl_int1_notification_set(&accel_ctx_global, H3LIS331DL_INT1_LATCHED);
    h3lis331dl_pin_polarity_set(&accel_ctx_global, H3LIS331DL_ACTIVE_HIGH);

    ESP_LOGI(TAG, "Czekam na stabilizację filtra...");
    vTaskDelay(pdMS_TO_TICKS(200)); 

    //Czyścimy ewentualne fałszywe przerwanie powstałe przy włączaniu filtra
    h3lis331dl_int1_src_t dummy;
    h3lis331dl_int1_src_get(&accel_ctx_global, &dummy); // Pierwszy odczyt
    vTaskDelay(pdMS_TO_TICKS(10));
    h3lis331dl_int1_src_get(&accel_ctx_global, &dummy); // Drugi odczyt dla pewności

    //Zwalniamy ODR do 50Hz dla oszczędności energii
    h3lis331dl_data_rate_set(&accel_ctx_global, H3LIS331DL_ODR_50Hz);

    // Konfiguracja wybudzania ESP32
    rtc_gpio_pullup_dis(ACCEL_INT_GPIO);
    rtc_gpio_pulldown_en(ACCEL_INT_GPIO); //sciągamy do masy, bo czekamy na stan wysoki

    esp_sleep_enable_ext0_wakeup(ACCEL_INT_GPIO, 1); // 1 = High Level

    ESP_LOGI(TAG, "Dobranoc! Wybudzenie nastąpi po ruchu.");
    uart_wait_tx_idle_polling(CONFIG_ESP_CONSOLE_UART_NUM);
    
    esp_deep_sleep_start();
}


static void sensor_monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "SENSOR: Inicjalizacja sprzętu...");

    uint8_t whoamI = 0;
    // 1. Konfiguracja kontekstu akcelerometru
    accel_ctx_global.write_reg = platform_i2c_write;
    accel_ctx_global.read_reg = platform_i2c_read;
    accel_ctx_global.mdelay = vTaskDelay;
    accel_ctx_global.handle = (void*)(uint32_t)(0x18 << 1); 

    // Konfiguracja zyroskopu
    esp_err_t gyro_ret = l3g4200d_init(
        &gyro_dev_global, 
        0x69,                    // Adres 7-bitowy
        L3G4200D_SCALE_2000DPS,  // Skala
        L3G4200D_DATARATE_400HZ_50 // ODR/BW
    );

    if (gyro_ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd inicjalizacji żyroskopu: %d", gyro_ret);
    } else {
        ESP_LOGI(TAG, "Żyroskop zainicjalizowany poprawnie.");
    }

    // 2. Inicjalizacja rejestrów H3LIS331DL
    h3lis331dl_device_id_get(&accel_ctx_global, &whoamI);
    
    //Odczyt źródła przerwania, aby wyczyścić ew. flagę latched po wybudzeniu
    h3lis331dl_int1_src_t dummy_src;
    h3lis331dl_int1_src_get(&accel_ctx_global, &dummy_src);

    // Konfiguracja do normalnej pracy
    h3lis331dl_data_rate_set(&accel_ctx_global, H3LIS331DL_ODR_400Hz);
    h3lis331dl_full_scale_set(&accel_ctx_global, H3LIS331DL_100g);
    //filtry wylaczone
    h3lis331dl_hp_path_set(&accel_ctx_global, H3LIS331DL_HP_DISABLE);

    //Stabilizacja
    vTaskDelay(pdMS_TO_TICKS(500)); 
    ESP_LOGI(TAG, "SENSOR: Wykryto ID: 0x%02X. Pętla pomiarowa ruszyła.", whoamI);

    // Zmienne do kontroli czasu
    uint64_t last_sensor_read_time = esp_timer_get_time();
    uint64_t last_wifi_send_time = last_sensor_read_time;
    
    //Zmienne do Deep Sleep
    uint64_t last_activity_time = esp_timer_get_time();

    const uint64_t WIFI_SEND_INTERVAL_US = 50000;  // 20 FPS
    
    // Progi wykrywania aktywności (żeby nie zasnąć)
    const float ACT_THRESH_ACC_HIGH = 1.2f; // 1.2G
    const float ACT_THRESH_ACC_LOW = 0.8f;  // 0.8G
    const float ACT_THRESH_GYRO = 0.5f;     // 0.5 rad/s

    while (1) {
        //SZYBKI ODCZYT SENSORÓW
        int16_t a_raw[3];
        Vector g_dps;
        
        h3lis331dl_acceleration_raw_get(&accel_ctx_global, a_raw);
        l3g4200d_read_normalize(&gyro_dev_global, &g_dps);

        uint64_t now_us = esp_timer_get_time();
        
        // Obliczamy dt
        float dt = (float)(now_us - last_sensor_read_time) / 1000000.0f;
        last_sensor_read_time = now_us;

        // Konwersja na jednostki inżynierskie
        TripleF accel_g = { 
           (h3lis331dl_from_fs100_to_mg(a_raw[0])/1000.0f) * ACCEL_SCALE_CORRECTION, 
            (h3lis331dl_from_fs100_to_mg(a_raw[1])/1000.0f) * ACCEL_SCALE_CORRECTION, 
            (h3lis331dl_from_fs100_to_mg(a_raw[2])/1000.0f) * ACCEL_SCALE_CORRECTION
        };
        TripleF gyro_rad = { 
            g_dps.XAxis * (M_PI/180.0f), 
            g_dps.YAxis * (M_PI/180.0f), 
            g_dps.ZAxis * (M_PI/180.0f) 
        };

        uint64_t current_timeout;
        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            current_timeout = global_idle_timeout_us;
            xSemaphoreGive(data_mutex);
        } else {
            current_timeout = 30 * 1000000; // Wartość domyślna w razie błędu mutexa 
        }

        //LOGIKA WYKRYWANIA BEZCZYNNOŚCI
       if (stats_context.hitActive) {
            last_activity_time = now_us;
        }
      
      if ((now_us % 5000000) < 20000) { 
            ESP_LOGI(TAG, "Czas bezruchu: %llu s / %llu s", 
                     (now_us - last_activity_time)/1000000, 
                     current_timeout/1000000);
        }

        // Sprawdź czy czas na sen
      if (is_sleep_enabled && (now_us - last_activity_time) > current_timeout) {
            ESP_LOGW(TAG, "Brak aktywności przez %llu s. Usypianie...", current_timeout/1000000);
            
            // Zwalniamy mutex dla bezpieczeństwa (choć przy sleep resetuje się pamięć)
            if (xSemaphoreGetMutexHolder(data_mutex) == xTaskGetCurrentTaskHandle()) {
                xSemaphoreGive(data_mutex);
            }
            
            // Wywołujemy funkcję usypiającą (zdefiniowaną poza taskiem)
            enter_deep_sleep(); 
        }



        // Fuzja sensorów
        Sample_t new_sample;
        new_sample.accel = update_world_accel(gyro_rad, accel_g, dt); 
        new_sample.gyro = gyro_rad;
        new_sample.timestamp = (uint32_t)now_us; 


        // Przetwarzanie statystyk (wykrywanie uderzeń)
        stats_process_sample(&stats_context, &new_sample);

        //OGRANICZONA WYSYŁKA WIFI (20 FPS) ---
        if (now_us - last_wifi_send_time >= WIFI_SEND_INTERVAL_US) {
            if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                current_sample_data = new_sample; 
                xSemaphoreGive(data_mutex);

                if (udp_task_handle != NULL) {
                    xTaskNotifyGive(udp_task_handle);
                }
            }
            last_wifi_send_time = now_us;
        }

        // Czekamy 10ms (100Hz)
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

//INICJALIZACJA WIFI
void wifi_init_softap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = { .ap = { .ssid = AP_SSID, .ssid_len = strlen(AP_SSID), .channel = 1, .password = AP_PASS, .max_connection = 4, .authmode = WIFI_AUTH_WPA_WPA2_PSK } };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void) {
    esp_log_level_set("*", ESP_LOG_INFO);

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        ESP_LOGI(TAG, "Wybudzono przez ruch (akcelerometr)!");
    } else {
        ESP_LOGI(TAG, "Normalny start systemu (Power ON / Reset).");
    }


    ESP_ERROR_CHECK(nvs_flash_init());
    
    //Inicjalizacja I2C
    i2c_config_t conf = { .mode = I2C_MODE_MASTER, .sda_io_num = 21, .scl_io_num = 22, .sda_pullup_en = 1, .scl_pullup_en = 1, .master.clk_speed = 400000 };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);

    //Wi-Fi i Mutex
    wifi_init_softap();
    data_mutex = xSemaphoreCreateMutex();
    motion_stats_init(&stats_context, 0.45f);

    //Zadania
    xTaskCreate(udp_server_task, "udp_task", 4096, NULL, 5, &udp_task_handle);
    xTaskCreate(sensor_monitor_task, "sensor_task", 4096, NULL, 10, NULL);
}