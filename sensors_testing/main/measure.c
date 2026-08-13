#include "measure.h"
#include "normalize.h" 

#include <math.h> 
#include <string.h> 

#include "h3lis331dl_reg.h"
#include "lsm6dsv16x_reg.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_event.h"     
#include "nmea_parser.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "driver/uart.h"

#define BUF_SIZE (1024)

static const char *TAG = "Sensors_Engine";

#define GPS_UART_NUM UART_NUM_1 

bool config_enable_sleep = false;

//stm struct
static stmdev_ctx_t accel_ctx;
static stmdev_ctx_t imu_ctx;

//SPI device handle + cs pin
static sensor_spi_handle_t accel_hardware;
static sensor_spi_handle_t imu_hardware;

//latest gps data
static float current_lat = 0.0f;
static float current_lon = 0.0f;
//czy gps polaczyc sie z satelita
static bool  current_fix = false;

//bufor kolowy - przechowuje ostatnie pomiary
static global_data_t pre_hit_buffer[PRE_HIT_BUFFER_SIZE];
//indeks pozycji, ktora zaraz zaktualizujemy
static int pre_hit_index = 0;
static int pre_hit_count = 0;

//static nmea_parser_handle_t nmea_hdl = NULL;

static TaskHandle_t sensors_task_handle = NULL;

// zmienne konfiguracyjne
float CRASH_THRESHOLD_G = 4.5f;   // prog zderzneia
float config_wake_ths_g = 1.2f;   // prog wybudzenia 
float config_sleep_ths_g = 0.05f; // prog uspiennia
int config_idle_time_s = 60;      // Wymagany czas bezruchu w sekundach

//how often we measure sensors
int config_sensor_loop_ms = 30;

bool is_gps_connected = false;

// Zmienne stanu zasilania i liczników
static int seconds_in_immobility = 0;
//dzielnik czestotliwosci
static int loop_counter_1s = 0;

static void gps_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

bool get_gps_hardware_status(void) {
    return is_gps_connected;
}

//latest data from gps
void get_last_gps_data(float *lat, float *lon, bool *fix) {
    *lat = current_lat;
    *lon = current_lon;
    *fix = current_fix;
}

void lsm6dsv16x_configure_wakeup_threshold(float threshold_g)
{
    const stmdev_ctx_t *ctx = &imu_ctx; 

    lsm6dsv16x_mem_bank_set(ctx, LSM6DSV16X_MAIN_MEM_BANK);

    //all axes enabled
    lsm6dsv16x_tap_detection_t tap_axes = {
        .tap_x_en = PROPERTY_ENABLE,
        .tap_y_en = PROPERTY_ENABLE,
        .tap_z_en = PROPERTY_ENABLE
    };
    lsm6dsv16x_tap_detection_set(ctx, tap_axes);
    lsm6dsv16x_filt_wkup_act_feed_set(ctx, LSM6DSV16X_WK_FEED_HIGH_PASS);  //cut gravity

    lsm6dsv16x_interrupt_mode_t int_mode = {
        .enable = PROPERTY_ENABLE,
        .lir = PROPERTY_ENABLE //latch int
    };
    lsm6dsv16x_interrupt_enable_set(ctx, int_mode);
   
    lsm6dsv16x_data_ready_mode_set(ctx, LSM6DSV16X_DRDY_LATCHED);

    uint8_t ths_val = (uint8_t)((threshold_g * 1000.0f) / 31.25f);
    if (ths_val > 63) ths_val = 63;
    lsm6dsv16x_act_thresholds_t act_ths = {
        .threshold = ths_val,
        .duration = 2, 
        .inactivity_ths = 0 
    };
    lsm6dsv16x_act_thresholds_set(ctx, &act_ths);

    //internat int signal to pin
    lsm6dsv16x_pin_int_route_t int1_route;
    lsm6dsv16x_pin_int1_route_get(ctx, &int1_route);
    int1_route.wakeup = PROPERTY_ENABLE; 
    lsm6dsv16x_pin_int1_route_set(ctx, &int1_route);

    
    lsm6dsv16x_all_sources_t dummy_clear;
    lsm6dsv16x_all_sources_get(ctx, &dummy_clear); //read equals reset(of olf interrrupts)

    ESP_LOGI(TAG, "LSM6DSV16X: Skonfigurowano LATCHED Wake-Up za pomocą API. Próg = %.2f G", threshold_g);

    ESP_LOGI(TAG, "=== LSM REGISTER DUMP ===");
    uint8_t val;
    for(uint8_t reg = 0x45; reg <= 0x5E; reg++)
    {
        lsm6dsv16x_read_reg(ctx, reg, &val, 1);
        ESP_LOGI(TAG, "REG[0x%02X] = 0x%02X", reg, val);
    }

    lsm6dsv16x_all_sources_get(ctx, &dummy_clear);
}


static int32_t sensor_write(void *handle, uint8_t header, const uint8_t *bufp, uint16_t len)
{
    sensor_spi_handle_t *sensor = (sensor_spi_handle_t *)handle;

    if (sensor->cs_pin == PIN_ACCEL_CS && len > 1) {
        header |= 0x40; 
    }

    uint8_t tx_data[1 + 16]; 
    tx_data[0] = header;

    //desc, src, bytes
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
        header |= 0x40; //auto increment address while reading
    }
    //header + 16 bytes
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

    //rx_data[0] - rubbish, because device didn't read header yet
    memcpy(bufp, &rx_data[1], len);
    return 0;
}

void sensors_set(bool GPS_on)
{
    ESP_LOGI(TAG, "Konfiguracja czujników");
    esp_err_t ret;
    spi_device_handle_t spi_accel_handle;
    spi_device_handle_t spi_imu_handle;

    //spi bus
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

    //adding H3Lis to bus
    spi_device_interface_config_t devcfg_accel = {
        .clock_speed_hz = 1 * 1000 * 1000, 
        .mode = 3, 
        .spics_io_num = PIN_ACCEL_CS, 
        .queue_size = 7
    };
    ret = spi_bus_add_device(SPI2_HOST, &devcfg_accel, &spi_accel_handle);
    ESP_ERROR_CHECK(ret);

    //adding IMU to bus
    spi_device_interface_config_t devcfg_imu = {
        .clock_speed_hz = 5 * 1000 * 1000, 
        .mode = 3,                         
        .spics_io_num = PIN_IMU_CS,        
        .queue_size = 7
    };
    ret = spi_bus_add_device(SPI2_HOST, &devcfg_imu, &spi_imu_handle);
    ESP_ERROR_CHECK(ret);
    
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
        ESP_LOGE(TAG, "Akcelerometr nie został znaleziony! Odczytane ID: 0x%02X", whoamI);
    } else {
        ESP_LOGI(TAG, "Akcelerometr H3LIS331DL wykryty poprawnie.");
    }

    // settings of ACCEL
    h3lis331dl_data_rate_set(&accel_ctx, H3LIS331DL_ODR_100Hz); 
    h3lis331dl_full_scale_set(&accel_ctx, H3LIS331DL_200g);

    lsm6dsv16x_device_id_get(&imu_ctx, &whoamI);
    if(whoamI != LSM6DSV16X_ID) {
        ESP_LOGE(TAG, "IMU nie zostało znalezione! Odczytane ID: 0x%02X", whoamI);
    } else {
        ESP_LOGI(TAG, "IMU LSM6DSV16X wykryte poprawnie.");
    }

    // imu settings
    lsm6dsv16x_xl_data_rate_set(&imu_ctx, LSM6DSV16X_ODR_AT_960Hz);
    lsm6dsv16x_gy_data_rate_set(&imu_ctx, LSM6DSV16X_ODR_AT_960Hz);
    lsm6dsv16x_xl_full_scale_set(&imu_ctx, LSM6DSV16X_2g);       
    lsm6dsv16x_gy_full_scale_set(&imu_ctx, LSM6DSV16X_4000dps);
    lsm6dsv16x_sflp_data_rate_set(&imu_ctx, LSM6DSV16X_SFLP_120Hz);
    lsm6dsv16x_sflp_game_rotation_set(&imu_ctx, PROPERTY_ENABLE);

    //Accel wakeup interrupt
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,            
        .pin_bit_mask = (1ULL << PIN_ACCEL_INT1),  
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE       
    };
    gpio_config(&io_conf);
    
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);    
    gpio_wakeup_enable(PIN_ACCEL_INT1, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    if(GPS_on) {
        gps_start();
    }
}

void sensors_enter_light_sleep(void)
{
    ESP_LOGI(TAG, "ZASILANIE: Przygotowanie peryferiów do uśpienia...");

    // wake up on H3LIS331DL
    gpio_wakeup_enable(PIN_ACCEL_INT1, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    ESP_LOGW(TAG, "ZASILANIE: Wchodzę w tryb LIGHT SLEEP. Silne uderzenie wybudzi urządzenie.");
    
    // clearing UART console
    uart_wait_tx_idle_polling(CONFIG_ESP_CONSOLE_UART_NUM);

    // going to sleep
    esp_light_sleep_start(); 

    // =================================================================
    //         WAKE UP
    // =================================================================

    gpio_wakeup_disable(PIN_ACCEL_INT1);

    ESP_LOGW(TAG, "ZASILANIE: ESP32 wybudzony przez H3LIS! Czyszczę zatrzask przerwania przez SPI...");

    //reading int - flag down
    uint8_t accel_src = 0;
    if (h3lis331dl_read_reg(&accel_ctx, 0x31, &accel_src, 1) == 0) {
        if (accel_src & 0x40) { //if active source
            ESP_LOGI(TAG, "ZASILANIE: Potwierdzono wybudzenie przez blok przerwań H3LIS331DL (SRC: 0x%02X).", accel_src);
        } else {
            ESP_LOGW(TAG, "ZASILANIE: Wybudzenie nastąpiło, ale rejestr H3LIS nie zgłasza aktywnego źródła (SRC: 0x%02X).", accel_src);
        }
    } else {
        ESP_LOGE(TAG, "ZASILANIE: Błąd komunikacji SPI przy czyszczeniu rejestru przerwań akcelerometru!");
    }
}

accel_data accel_get(void)
{
    int16_t data_raw[3];
    h3lis331dl_status_reg_t reg;
    h3lis331dl_status_reg_get(&accel_ctx, &reg);

    accel_data received_data = {0};

    //if new data available
    if(reg.zyxda) {
        
        h3lis331dl_acceleration_raw_get(&accel_ctx, data_raw);

        // converting from mg to g
        received_data.x = h3lis331dl_from_fs200_to_mg(data_raw[0]) / 1000.0f;
        received_data.y = h3lis331dl_from_fs200_to_mg(data_raw[1]) / 1000.0f;
        received_data.z = h3lis331dl_from_fs200_to_mg(data_raw[2]) / 1000.0f;
    }
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
            //degrees per second
            received_data.gyro.x = lsm6dsv16x_from_fs4000_to_mdps(data_raw_gy[0]) / 1000.0f;
            received_data.gyro.y = lsm6dsv16x_from_fs4000_to_mdps(data_raw_gy[1]) / 1000.0f;
            received_data.gyro.z = lsm6dsv16x_from_fs4000_to_mdps(data_raw_gy[2]) / 1000.0f;
        }

        if (all_status.drdy_gy) {
            //getting internall quaternion
            if (lsm6dsv16x_ln_pg_read(&imu_ctx, 0x6EU, (uint8_t *)data_raw_quat, 6) == 0) {

                
                //scalling to -1, 1
                received_data.quat.x = (float)data_raw_quat[0] / 16384.0f;
                received_data.quat.y = (float)data_raw_quat[1] / 16384.0f;
                received_data.quat.z = (float)data_raw_quat[2] / 16384.0f;

                float sum_sq = (received_data.quat.x * received_data.quat.x) +
                               (received_data.quat.y * received_data.quat.y) +
                               (received_data.quat.z * received_data.quat.z);
                //calculating w
                if (sum_sq < 1.0f) {
                    received_data.quat.w = sqrtf(1.0f - sum_sq);
                } else {
                    received_data.quat.w = 0.0f;
                    
                    float norm = sqrtf(sum_sq);
                    //normalize vector
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
             received_data.accel.x, received_data.accel.y, received_data.accel.z);*/
    /*ESP_LOGI(ImuTAG, "Gyro [dps]:  X: %6.3f | Y: %6.3f | Z: %6.3f", 
             received_data.gyro.x,  received_data.gyro.y,  received_data.gyro.z);
    ESP_LOGI(ImuTAG, "Quat [SFLP]: X: %6.4f | Y: %6.4f | Z: %6.4f | W: %6.4f", 
             received_data.quat.x,  received_data.quat.y,  received_data.quat.z, received_data.quat.w);*/

    return received_data;
}

static void sensors_reading_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Sensors production task started with REAL Light Sleep Wakeup.");

    static int previous_level = -1;
    loop_counter_1s = 0; //counting till 1 second

    //how many loops - one loop 30 ms
    static int log_counter_3s = 0; // wyswietlanie danych raz na 3 sekundy

    //how many loops done per one second
    int loops_per_second = 1000 / config_sensor_loop_ms; 


    int loops_per_3_seconds = 3000 / config_sensor_loop_ms;

    while (1)
    {
        int level = gpio_get_level(PIN_ACCEL_INT1);
        if (level != previous_level)
        {
            previous_level = level;
            ESP_LOGW("GPIO", "INT1 changed state -> %d", level);
        }

        // getting dana from SPI
        global_data_t current_frame = convert_to_global_frame();

        float imu_x = current_frame.accel_imu.x;
        float imu_y = current_frame.accel_imu.y;
        float imu_z = current_frame.accel_imu.z;

        float imu_total_g = sqrtf(imu_x * imu_x + imu_y * imu_y + imu_z * imu_z);
        float imu_delta_g = fabsf(imu_total_g - 1.0f);

        loop_counter_1s++;

        // moveless detection
        
        if (loop_counter_1s >= loops_per_second)
        {//one second reached
            loop_counter_1s = 0; 

            if (imu_delta_g < config_sleep_ths_g)
            {
                seconds_in_immobility++; 
                ESP_LOGI(TAG, "Czas w bezruchu: %d / %d s", seconds_in_immobility, config_idle_time_s);
            }
            else
            {
                //there is move
                seconds_in_immobility = 0; 
            }
        }

        if (seconds_in_immobility >= config_idle_time_s)
        {
            if (config_enable_sleep && !is_phone_connected)
            {
                ESP_LOGW(TAG, "!!! BRAK RUCHU (%d s) I BRAK POŁĄCZENIA Z TELEFONEM -> LIGHT SLEEP !!!", config_idle_time_s);

                sensors_enter_light_sleep();

                seconds_in_immobility = 0;
                loop_counter_1s = 0;
            }
            else if (is_phone_connected)
            {
                seconds_in_immobility = 0;
                ESP_LOGI(TAG, "Telefon podłączony – pomijam uśpienie.");
            }
        }


        //printing logs per 3 seconds
        log_counter_3s++;
        

        if (log_counter_3s >= loops_per_3_seconds)
        {
            log_counter_3s = 0;
            log_global_data(&current_frame);
        }

        current_frame.packet_type = 0;

        float h3_x = current_frame.accel_h3lis.x;
        float h3_y = current_frame.accel_h3lis.y;
        float h3_z = current_frame.accel_h3lis.z; 

        float h3_dynamic_g = sqrtf(h3_x * h3_x + h3_y * h3_y + h3_z * h3_z);

        if (h3_dynamic_g > CRASH_THRESHOLD_G)
        {
            ESP_LOGW(TAG, "!!! DETEKCJA ZDERZENIA: Wykryto %2.2f G !!!", h3_dynamic_g);

            seconds_in_immobility = 0;
            loop_counter_1s = 0;

            int read_idx = (pre_hit_count < PRE_HIT_BUFFER_SIZE) ? 0 : pre_hit_index;

            for (int i = 0; i < pre_hit_count; i++)
            {
                pre_hit_buffer[read_idx].packet_type = 1;
                if (data_queue != NULL)
                {
                    xQueueSend(data_queue, &pre_hit_buffer[read_idx], pdMS_TO_TICKS(10));
                }
                read_idx = (read_idx + 1) % PRE_HIT_BUFFER_SIZE;
            }

            current_frame.packet_type = 1;
            if (data_queue != NULL)
            {
                xQueueSend(data_queue, &current_frame, pdMS_TO_TICKS(10));
            }

            for (int i = 0; i < POST_HIT_SAMPLES; i++)
            {
                // Dynamiczny czas zapisu po zderzeniu
                vTaskDelay(pdMS_TO_TICKS(config_sensor_loop_ms)); 

                global_data_t post_frame = convert_to_global_frame();
                post_frame.packet_type = 1;

                if (data_queue != NULL)
                {
                    xQueueSend(data_queue, &post_frame, pdMS_TO_TICKS(10));
                }
            }

            pre_hit_index = 0;
            pre_hit_count = 0;
        }
        else
        {
            pre_hit_buffer[pre_hit_index] = current_frame;
            pre_hit_index = (pre_hit_index + 1) % PRE_HIT_BUFFER_SIZE;

            if (pre_hit_count < PRE_HIT_BUFFER_SIZE)
            {
                pre_hit_count++;
            }

            if ( data_queue != NULL)
            {
                xQueueSend(data_queue, &current_frame, 0);
            }
        }
       
        vTaskDelay(pdMS_TO_TICKS(config_sensor_loop_ms));
    }
}

void sensors_task_start(void)
{
    xTaskCreatePinnedToCore(
        sensors_reading_task,   
        "sensors_task",         
        4096,                   
        NULL,   //parameters                
        5,                      
        &sensors_task_handle,            
        1                       
    );
}

void log_global_data(const global_data_t *data)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "log_global_data: Wskaźnik do danych jest NULL!");
        return;
    }

    ESP_LOGI(TAG, "================ GLOBAL DATA FRAME ================");
    ESP_LOGI(TAG, "Typ pakietu (Packet Type): %u (%s)", 
             data->packet_type, 
             data->packet_type == 1 ? "ZDERZENIE / HIT" : "LOT / NORMAL");
    
    ESP_LOGI(TAG, "Accel H3LIS [g]:  X: %6.2f | Y: %6.2f | Z: %6.2f", 
             data->accel_h3lis.x, data->accel_h3lis.y, data->accel_h3lis.z);
    
    ESP_LOGI(TAG, "Accel IMU   [g]:  X: %6.2f | Y: %6.2f | Z: %6.2f", 
             data->accel_imu.x, data->accel_imu.y, data->accel_imu.z);
    
    ESP_LOGI(TAG, "Gyro IMU [dps]:  X: %6.2f | Y: %6.2f | Z: %6.2f", 
             data->gyro.x, data->gyro.y, data->gyro.z);
    
    bool hardware_ok = get_gps_hardware_status();

    if (hardware_ok) {
        ESP_LOGI(TAG, "Komunikacja GPS (UART): POŁĄCZONO (Odebrano ramki NMEA)");

        if (data->gps_fix) {
            ESP_LOGI(TAG, "Status FIX GPS:        TAK (Pozycja aktualna)");
            ESP_LOGI(TAG, "GPS Pozycja:           Szerokość: %.6f° %c | Długość: %.6f° %c", 
                     fabsf(data->latitude),  data->latitude >= 0 ? 'N' : 'S',
                     fabsf(data->longitude), data->longitude >= 0 ? 'E' : 'W');
        } else {
            ESP_LOGW(TAG, "Status FIX GPS:        BRAK FIXA (Szukam satelitów...)");
            if (data->latitude != 0.0f || data->longitude != 0.0f) {
                ESP_LOGW(TAG, "Ostatnia znana poz.:   Szer: %.6f, Dł: %.6f", 
                         data->latitude, data->longitude);
            }
        }
    } else {
        ESP_LOGE(TAG, "Komunikacja GPS (UART): BRAK SYGNAŁU! ");
        ESP_LOGE(TAG, "Status FIX GPS:        NIEAKTYWNY");
    }

    ESP_LOGI(TAG, "===================================================");
}

///gps all

// === OBSŁUGA ODCZYTU I PARSOWANIA GPS ===

static void gps_read_task(void *pvParameters)
{
    uint8_t rx_buf[1];
    char line_buf[256];
    int line_idx = 0;
    int no_data_counter = 0;

    ESP_LOGI(TAG, "GPS Parser uruchomiony...");

    while (1) {
        // read one byte at a time wiht 100 ms break
        int len = uart_read_bytes(GPS_UART_NUM, rx_buf, 1, pdMS_TO_TICKS(100));

        if (len > 0) {
            no_data_counter = 0; 

            char c = (char)rx_buf[0];

            if (c != '\r' && c != '\n') {
                if (line_idx < sizeof(line_buf) - 1) {
                    line_buf[line_idx++] = c;
                }
            } 
            else if (line_idx > 0) {
                line_buf[line_idx] = '\0'; 

                if (line_buf[0] == '$' && (strstr(line_buf, "GP") || strstr(line_buf, "GN") || strstr(line_buf, "GA") || strstr(line_buf, "GL") || strstr(line_buf, "BD"))) {
                    is_gps_connected = true;
                }

                if (strstr(line_buf, "RMC")) {
                    char status = 'V';
                    float lat_raw = 0.0f, lon_raw = 0.0f;
                    char lat_dir = 'N', lon_dir = 'E';

                    // Format NMEA: $XXRMC,time,status,lat,N/S,lon,E/W,...
                    int parsed = sscanf(line_buf, "%*[^,],%*[^,],%c,%f,%c,%f,%c", 
                                        &status, &lat_raw, &lat_dir, &lon_raw, &lon_dir);

                    if (parsed >= 5 && status == 'A') {
                        // Konwersja DDMM.MMMM na DD.DDDDDD (Szerokość)
                        int lat_deg = (int)(lat_raw / 100.0f);
                        float lat_min = lat_raw - (lat_deg * 100.0f);
                        float lat_decimal = lat_deg + (lat_min / 60.0f);
                        if (lat_dir == 'S') lat_decimal = -lat_decimal;

                        // Konwersja DDMM.MMMM na DD.DDDDDD (Długość)
                        int lon_deg = (int)(lon_raw / 100.0f);
                        float lon_min = lon_raw - (lon_deg * 100.0f);
                        float lon_decimal = lon_deg + (lon_min / 60.0f);
                        if (lon_dir == 'W') lon_decimal = -lon_decimal;

                        current_lat = lat_decimal;
                        current_lon = lon_decimal;
                        current_fix = true;
                    } else {
                        current_fix = false;
                    }
                }

                line_idx = 0; 
            }
        } else {
           
            no_data_counter++;

           
            if (no_data_counter >= 30) {
                is_gps_connected = false;
                current_fix = false;
                no_data_counter = 30;
            }
        }
    }
}

void gps_start(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    ESP_ERROR_CHECK(uart_driver_install(GPS_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM, GPS_TX_PIN, GPS_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreate(gps_read_task, "gps_read_task", 4096, NULL, 10, NULL);
    ESP_LOGI(TAG, "Parser danych GPS został pomyślnie uruchomiony.");
}

/*
//czyta wszystko z uart od gps
static void gps_dump_all_task(void *pvParameters)
{
    uint8_t rx_buf[128];
    char line_buf[256];
    int line_idx = 0;

    ESP_LOGI("GPS_DUMP", "Uruchomiono podgląd surowych danych GPS...");

    while (1) {
        // Czytamy dane po 1 bajcie z portu UART
        int len = uart_read_bytes(GPS_UART_NUM, rx_buf, 1, pdMS_TO_TICKS(100));

        if (len > 0) {
            char c = (char)rx_buf[0];

            // Składamy linię do napotkania znaku nowej linii '\n' lub '\r'
            if (c != '\r' && c != '\n') {
                if (line_idx < sizeof(line_buf) - 1) {
                    line_buf[line_idx++] = c;
                }
            } 
            else if (line_idx > 0) {
                line_buf[line_idx] = '\0'; // Zwieńczenie ciągu znaków
                
                // Wypisujemy całą odebraną linię NMEA na ekran logów
                ESP_LOGI("GPS_RAW", "%s", line_buf);
                
                line_idx = 0; // Reset bufora linii dla kolejnej ramki
            }
        }
    }
}

//gps start do gps_dump
void gps_start(void)
{
    // 1. Konfiguracja UART
    uart_config_t uart_config = {
        .baud_rate = 115200, // Zmień na 115200 jeśli Twój moduł działa na wyższym baudrate
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    // 2. Instalacja sterownika i przypisanie pinów
    ESP_ERROR_CHECK(uart_driver_install(GPS_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM, GPS_TX_PIN, GPS_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // 3. Utworzenie zadania wypisującego logi
    xTaskCreate(gps_dump_all_task, "gps_dump_all_task", 4096, NULL, 10, NULL);
    ESP_LOGI("GPS_DUMP", "Zadanie podglądu GPS zostało uruchomione.");
}
    */