#include "measure.h"
#include "h3lis331dl_reg.h"
#include "lsm6dsv16x_reg.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_event.h"     
#include "nmea_parser.h"  
#include <string.h> 

#include "normalize.h" 
#include <math.h> 

static const char *TAG = "Sensors_Engine";
static const char *AccelTAG = "Accel";
static const char *ImuTAG = "IMU";

#define GPS_UART_NUM UART_NUM_1 

//DEFINICJE STAŁYCH DLA BUFORA I DETEKCJI ZDERZENIA
#define PRE_HIT_BUFFER_SIZE  100
#define POST_HIT_SAMPLES     100


static stmdev_ctx_t accel_ctx;
static stmdev_ctx_t imu_ctx;

//uchwyty do urzadzen na SPI
static sensor_spi_handle_t accel_hardware;
static sensor_spi_handle_t imu_hardware;

//ostatnie dane z GPS
static float current_lat = 0.0f;
static float current_lon = 0.0f;
//czy gps polaczyc sie z satelita
static bool  current_fix = false;

//bufor kolowy - przechowuje ostatnie pomiary
static global_data_t pre_hit_buffer[PRE_HIT_BUFFER_SIZE];
//indeks pozycji, ktora zaraz zaktualizujemy
static int pre_hit_index = 0;
static int pre_hit_count = 0;

static nmea_parser_handle_t nmea_hdl = NULL;

// zmienne konfiguracyjne
float CRASH_THRESHOLD_G = 4.5f;   // prog zderzneia
float config_wake_ths_g = 1.5f;   // prog wybudzenia 
float config_sleep_ths_g = 0.05f; // prog uspiennia
int config_idle_time_s = 120;      // Wymagany czas bezruchu w sekundach

// Zmienne stanu zasilania i liczników
static bool is_device_sleeping = false;
static int seconds_in_immobility = 0;
//dzielnik czestotliwosci
static int loop_counter_1s = 0;

//zwraca najswiezsze dane z GPS
void get_last_gps_data(float *lat, float *lon, bool *fix) {
    *lat = current_lat;
    *lon = current_lon;
    *fix = current_fix;
}

//aktualizacja dannych z gps
static void gps_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    gps_t *gps = (gps_t *)event_data;
    switch (event_id) {
        case GPS_UPDATE:
            current_lat = gps->latitude;
            current_lon = gps->longitude;
            current_fix = gps->fix;
            break;
        case GPS_UNKNOWN:
            break;
        default:
            break;
    }
}

static int32_t sensor_write(void *handle, uint8_t header, const uint8_t *bufp, uint16_t len)
{
    //pin cs oraz jaki port SPI wykorzystujemy
    sensor_spi_handle_t *sensor = (sensor_spi_handle_t *)handle;

    //konfiguracja do odczytu wielobitowego
    if (sensor->cs_pin == PIN_ACCEL_CS && len > 1) {
        header |= 0x40; 
    }

    uint8_t tx_data[1 + 16]; 
    tx_data[0] = header;

    //desc, src, bajty
    memcpy(&tx_data[1], bufp, len);

    spi_transaction_t trans ={
        .length = (1 + len) * 8, 
        .tx_buffer = tx_data,
        .rx_buffer = NULL 
    };

    return spi_device_polling_transmit(sensor->spi_handle, &trans) == ESP_OK ? 0 : -1;
}

static int32_t sensor_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len)
{

    //reg - adres rejestru(w czujniku), z ktorego czytamy dane

    sensor_spi_handle_t *sensor = (sensor_spi_handle_t *)handle;
    //najstarszy bit w bajcie adresowym - odczyt danych

    //header - adres rejestru + polecenie, co chcemy zrobic z rejestrem
    uint8_t header = reg | 0x80;

    if (sensor->cs_pin == PIN_ACCEL_CS && len > 1) {
        header |= 0x40; 
    }

    uint8_t tx_data[16+1] = {0};
    uint8_t rx_data[16+1] = {0};
    tx_data[0] = header;

    spi_transaction_t trans= {
        .length = (1 + len)*8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };

    if(spi_device_polling_transmit(sensor->spi_handle, &trans) != ESP_OK) {
        return -1;
    }

    memcpy(bufp, &rx_data[1], len);
    return 0;
}

void sensors_set(bool GPS_on)
{
    ESP_LOGI(TAG,"Setting up sensors");
    esp_err_t ret;
    spi_device_handle_t spi_accel_handle;
    spi_device_handle_t spi_imu_handle;

    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_MISO,
        .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1, 
        .quadhd_io_num = -1, 
        .max_transfer_sz = 32
    };

    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret); 

    //konfiguracja slave
    spi_device_interface_config_t devcfg_accel = {
        .clock_speed_hz = 1 * 1000 * 1000, 
        .mode = 3, 
        .spics_io_num = PIN_ACCEL_CS,
        .queue_size = 7
    };
    ret = spi_bus_add_device(SPI2_HOST, &devcfg_accel, &spi_accel_handle);
    ESP_ERROR_CHECK(ret);

    spi_device_interface_config_t devcfg_imu = {
        .clock_speed_hz = 5 * 1000 * 1000, 
        .mode = 3,                         
        .spics_io_num = PIN_IMU_CS,        
        .queue_size = 7
    };
    ret = spi_bus_add_device(SPI2_HOST, &devcfg_imu, &spi_imu_handle);
    ESP_ERROR_CHECK(ret);

    //konfigurowanie mojej wlasnej struktury SPI

    accel_hardware.spi_handle = spi_accel_handle;
    accel_hardware.cs_pin = PIN_ACCEL_CS;
    accel_ctx.handle = (void*)&accel_hardware;
    accel_ctx.write_reg = sensor_write;
    accel_ctx.read_reg = sensor_read;

    imu_hardware.spi_handle = spi_imu_handle;
    imu_hardware.cs_pin = PIN_IMU_CS;
    imu_ctx.handle = (void*)&imu_hardware;
    imu_ctx.write_reg = sensor_write;
    imu_ctx.read_reg = sensor_read;

    uint8_t whoamI = 0;
    h3lis331dl_device_id_get(&accel_ctx, &whoamI);
    if(whoamI != H3LIS331DL_ID) {
        ESP_LOGE(TAG, "Accelerometer wasn't found. Received id: 0x%02X", whoamI);
    } else {
        ESP_LOGI(TAG,"Accelerometer was found");
    }

    h3lis331dl_data_rate_set(&accel_ctx, H3LIS331DL_ODR_100Hz); 
    h3lis331dl_full_scale_set(&accel_ctx, H3LIS331DL_400g); 

    lsm6dsv16x_device_id_get(&imu_ctx, &whoamI);
    if(whoamI != LSM6DSV16X_ID) {
        ESP_LOGE(TAG, "IMU wasn't found. Received id: 0x%02X", whoamI);
    } else {
        ESP_LOGI(TAG,"IMU was found");
    }

    lsm6dsv16x_xl_data_rate_set(&imu_ctx, LSM6DSV16X_ODR_AT_960Hz);
    lsm6dsv16x_gy_data_rate_set(&imu_ctx, LSM6DSV16X_ODR_AT_960Hz);
    lsm6dsv16x_xl_full_scale_set(&imu_ctx, LSM6DSV16X_2g);       
    lsm6dsv16x_gy_full_scale_set(&imu_ctx, LSM6DSV16X_4000dps);

    lsm6dsv16x_sflp_data_rate_set(&imu_ctx, LSM6DSV16X_SFLP_120Hz);

    //sila grawitacji to liwelowania dryfu grawitacyjnego 
    lsm6dsv16x_sflp_game_rotation_set(&imu_ctx, PROPERTY_ENABLE);

    if(GPS_on) {
        // Logika opcjonalnego włączania GPS
    }
}

accel_data accel_get(void)
{
    int16_t data_raw[3];
    h3lis331dl_status_reg_t reg;
    h3lis331dl_status_reg_get(&accel_ctx, &reg);

    accel_data received_data = {0};

    //pobieramy nowe, swieze dane(wszystkie osie musza byc odswiezone, nie polowa)
    if(reg.zyxda) {
        //automatyczne czyszcze flagi odczytu
        h3lis331dl_acceleration_raw_get(&accel_ctx, data_raw);

        //mamy juz w mg
        received_data.x = h3lis331dl_from_fs400_to_mg(data_raw[0]) / 1000.0f;
        received_data.y = h3lis331dl_from_fs400_to_mg(data_raw[1]) / 1000.0f;
        received_data.z = h3lis331dl_from_fs400_to_mg(data_raw[2]) / 1000.0f;
    }
   // ESP_LOGI(AccelTAG,"Acel measured: %f, %f, %f",  received_data.x ,  received_data.y,  received_data.z);
    return received_data;
}

imu_data imu_get(void)
{
    int16_t data_raw_xl[3] = {0};
    int16_t data_raw_gy[3] = {0};
    int16_t data_raw_quat[3] = {0}; 
    
    imu_data received_data = {0};
    lsm6dsv16x_all_sources_t all_status;

    if (lsm6dsv16x_all_sources_get(&imu_ctx, &all_status) == 0) 
    {
        if (all_status.drdy_xl) {
            lsm6dsv16x_acceleration_raw_get(&imu_ctx, data_raw_xl);

            //mg
            received_data.accel.x = lsm6dsv16x_from_fs2_to_mg(data_raw_xl[0]) / 1000.0f;
            received_data.accel.y = lsm6dsv16x_from_fs2_to_mg(data_raw_xl[1]) / 1000.0f;
            received_data.accel.z = lsm6dsv16x_from_fs2_to_mg(data_raw_xl[2]) / 1000.0f;
        }

        if (all_status.drdy_gy) {
            lsm6dsv16x_angular_rate_raw_get(&imu_ctx, data_raw_gy);
            //stopnie na sekunde
            received_data.gyro.x = lsm6dsv16x_from_fs4000_to_mdps(data_raw_gy[0]) / 1000.0f;
            received_data.gyro.y = lsm6dsv16x_from_fs4000_to_mdps(data_raw_gy[1]) / 1000.0f;
            received_data.gyro.z = lsm6dsv16x_from_fs4000_to_mdps(data_raw_gy[2]) / 1000.0f;
        }

        if (all_status.drdy_gy) {
            if (lsm6dsv16x_ln_pg_read(&imu_ctx, 0x6EU, (uint8_t *)data_raw_quat, 6) == 0) {

                //sami musimy wyznaczyc skladowa w

                //zamiana wartosci ze staloprzecinkowej na -1, 1
                received_data.quat.x = (float)data_raw_quat[0] / 16384.0f;
                received_data.quat.y = (float)data_raw_quat[1] / 16384.0f;
                received_data.quat.z = (float)data_raw_quat[2] / 16384.0f;

                float sum_sq = (received_data.quat.x * received_data.quat.x) +
                               (received_data.quat.y * received_data.quat.y) +
                               (received_data.quat.z * received_data.quat.z);
                
                if (sum_sq < 1.0f) {
                    received_data.quat.w = sqrtf(1.0f - sum_sq);
                } else {
                    received_data.quat.w = 0.0f;
                    //wyliczamy dlugosc kwaterniona, bo wiemy, ze nie jest = 1
                    float norm = sqrtf(sum_sq);
                    //normalizujemy wektor
                    received_data.quat.x /= norm;
                    received_data.quat.y /= norm;
                    received_data.quat.z /= norm;
                }
            }
        }
    }
    /*
    ESP_LOGI(ImuTAG,"Imu measured");

    ESP_LOGI(ImuTAG, "=== SENSOR MEASUREMENT ===");
    ESP_LOGI(ImuTAG, "Accel [g]:   X: %6.3f | Y: %6.3f | Z: %6.3f", 
             received_data.accel.x, received_data.accel.y, received_data.accel.z);
    ESP_LOGI(ImuTAG, "Gyro [dps]:  X: %6.3f | Y: %6.3f | Z: %6.3f", 
             received_data.gyro.x,  received_data.gyro.y,  received_data.gyro.z);
    ESP_LOGI(ImuTAG, "Quat [SFLP]: X: %6.4f | Y: %6.4f | Z: %6.4f | W: %6.4f", 
             received_data.quat.x,  received_data.quat.y,  received_data.quat.z, received_data.quat.w);*/

    return received_data;
}

static void sensors_reading_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Sensors production task started with Dynamic Sleep/Wakeup and Wi-Fi Guard.");
    
    while (1) {
        //konwersja danych i gotowosc do przeslania poprzez wifi
        global_data_t current_frame = convert_to_global_frame();
        
        
        //wypadkowe g na imu
        float imu_x = current_frame.accel_imu.x;
        float imu_y = current_frame.accel_imu.y;
        float imu_z = current_frame.accel_imu.z;
        float imu_total_g = sqrtf(imu_x*imu_x + imu_y*imu_y + imu_z*imu_z);

        //odcinamy stale 1g grawitacji
        //kierunek nas nie interesuje
        float imu_delta_g = fabsf(imu_total_g - 1.0f);

      
        if (is_device_sleeping) {
            //tryb uspienia
           
            if (imu_delta_g > config_wake_ths_g || is_phone_connected) {
                is_device_sleeping = false;
                seconds_in_immobility = 0;
                loop_counter_1s = 0;
                ESP_LOGW(TAG, "!!! URZĄDZENIE WYBUDZONE !!! (Ruch: %.2f G, Wi-Fi: %s)", 
                         imu_delta_g, is_phone_connected ? "TAK" : "NIE");
                
                // gps_start();
            }
            
            // W trybie uśpienia rzadziej odpytujemy sensory i oszczędzamy CPU
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        } 
        else {
            //aktywne urzadzenie
            loop_counter_1s++;
            
            // 100 iteracji * 10ms = 1 sekunda
            //po po kazdej iteracji mamy sleep na 10ms
            if (loop_counter_1s >= 100) {
                loop_counter_1s = 0;

                //podlaczenie do telefonu - nie usypiamy
                if (is_phone_connected) {
                    seconds_in_immobility = 0;
                    ESP_LOGI(TAG, "Bezruch wykryty, ale aplikacja jest połączona. Blokada uśpienia.");
                }
                
                else if (imu_delta_g < config_sleep_ths_g) {
                    seconds_in_immobility++;
                    ESP_LOGI(TAG, "Bezruch... Sekund: %d/%d (Aktualna delta: %.3f G)", 
                             seconds_in_immobility, config_idle_time_s, imu_delta_g);
                    
                    if (seconds_in_immobility >= config_idle_time_s) {
                        is_device_sleeping = true;
                        ESP_LOGW(TAG, "!!! SYSTEM IDZIE SPAĆ !!! Wykryto długotrwały bezruch.");
                        // gps_stop();
                    }
                } else {
                    // Wykryto normalny ruch w przestrzeni — reset odliczania
                    seconds_in_immobility = 0;
                }
            }
        }

        //packet type = 0, lot
        current_frame.packet_type = 0;
        float h3_total_g = sqrtf(current_frame.accel_h3lis.x * current_frame.accel_h3lis.x + 
                                 current_frame.accel_h3lis.y * current_frame.accel_h3lis.y + 
                                 current_frame.accel_h3lis.z * current_frame.accel_h3lis.z);

        if (h3_total_g > CRASH_THRESHOLD_G) {
            ESP_LOGW(TAG, "!!! DETEKCJA ZDERZENIA: Wykryto %2.2f G !!!", h3_total_g);

            
            seconds_in_immobility = 0;
            loop_counter_1s = 0;

            int read_idx = (pre_hit_count < PRE_HIT_BUFFER_SIZE) ? 0 : pre_hit_index;
            for (int i = 0; i < pre_hit_count; i++) {
                pre_hit_buffer[read_idx].packet_type = 1;
                if (data_queue != NULL) xQueueSend(data_queue, &pre_hit_buffer[read_idx], pdMS_TO_TICKS(10));
                read_idx = (read_idx + 1) % PRE_HIT_BUFFER_SIZE;
            }

            //type - uderzenie
            current_frame.packet_type = 1;
            //maks 10 ms czekanania na wolne miejsce w kolejce
            if (data_queue != NULL) xQueueSend(data_queue, &current_frame, pdMS_TO_TICKS(10));

            for (int i = 0; i < POST_HIT_SAMPLES; i++) {
                vTaskDelay(pdMS_TO_TICKS(10));
                global_data_t post_frame = convert_to_global_frame();
                post_frame.packet_type = 1;
                if (data_queue != NULL) xQueueSend(data_queue, &post_frame, pdMS_TO_TICKS(10));
            }

            pre_hit_index = 0; pre_hit_count = 0;
        } 
        else {
            pre_hit_buffer[pre_hit_index] = current_frame;
            pre_hit_index = (pre_hit_index + 1) % PRE_HIT_BUFFER_SIZE;
            if (pre_hit_count < PRE_HIT_BUFFER_SIZE) pre_hit_count++;

            if (data_queue != NULL) {
                if (xQueueSend(data_queue, &current_frame, 0) != pdTRUE) {
                    // Kolejka pełna
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void sensors_task_start(void)
{
    xTaskCreatePinnedToCore(
        sensors_reading_task,   
        "sensors_task",         
        4096,                   
        NULL,   //parametry                
        5,                      
        NULL,  //handle             
        1                       
    );
}

void gps_start(void)
{
    if (nmea_hdl != NULL) {
        ESP_LOGW(TAG, "GPS/NMEA parser jest już uruchomiony.");
        return;
    }

    nmea_parser_config_t config = NMEA_PARSER_CONFIG_DEFAULT();
    config.uart.uart_port = GPS_UART_NUM; 
    config.uart.rx_pin = GPS_RX_PIN;

    nmea_hdl = nmea_parser_init(&config);
    if (nmea_hdl != NULL) {
        nmea_parser_add_handler(nmea_hdl, gps_event_handler, NULL);
        ESP_LOGI(TAG, "Parser NMEA (GPS) został włączony.");
    } else {
        ESP_LOGE(TAG, "Nie udało się uruchomić parsera NMEA.");
    }
}

void gps_stop(void)
{
    if (nmea_hdl == NULL) {
        ESP_LOGW(TAG, "GPS jest już wyłączony.");
        return;
    }

    esp_err_t err = nmea_parser_deinit(nmea_hdl);
    if (err == ESP_OK) {
        nmea_hdl = NULL;
        current_fix = false; 
        current_lat = 0.0f;
        current_lon = 0.0f;
        ESP_LOGI(TAG, "Parser NMEA (GPS) został pomyślnie zatrzymany.");
    } else {
        ESP_LOGE(TAG, "Błąd podczas zatrzymywania parsera NMEA.");
    }
}