#include "measure.h"
#include "h3lis331dl_reg.h"
#include "lsm6dsv16x_reg.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_event.h"     
#include "nmea_parser.h"  

static const char *TAG = "Sensors_Engine";

#define GPS_UART_NUM UART_NUM_1 


static stmdev_ctx_t accel_ctx;
static stmdev_ctx_t imu_ctx;

static sensor_spi_handle_t accel_hardware;
static sensor_spi_handle_t imu_hardware;


static float current_lat = 0.0f;
static float current_lon = 0.0f;
static bool  current_fix = false;


void get_last_gps_data(float *lat, float *lon, bool *fix) {
    *lat = current_lat;
    *lon = current_lon;
    *fix = current_fix;
}


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
            // Optional: Handle unknown or unhandled custom NMEA strings
            break;
        default:
            break;
    }
}

static int32_t sensor_write(void *handle, uint8_t header, const uint8_t *bufp, uint16_t len)
{
    sensor_spi_handle_t *sensor = (sensor_spi_handle_t *)handle;

    if (sensor->cs_pin == PIN_ACCEL_CS && len > 1) {
        header |= 0x40; 
    }

    uint8_t tx_data[1 + 16]; 
    tx_data[0] = header;
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
    sensor_spi_handle_t *sensor = (sensor_spi_handle_t *)handle;
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

void sensors_set(void)
{
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
    lsm6dsv16x_sflp_game_rotation_set(&imu_ctx, PROPERTY_ENABLE);

    // ---- ORIGINAL REGISTRY NMEA PARSER CONFIGURATION ----
    nmea_parser_config_t config = NMEA_PARSER_CONFIG_DEFAULT();
    config.uart.uart_num = GPS_UART_NUM; 
    config.uart.rx_pin = GPS_RX_PIN;
    config.uart.tx_pin = GPS_TX_PIN;

    nmea_parser_handle_t nmea_hdl = nmea_parser_init(&config);
    if (nmea_hdl != NULL) {
        // Tie the driver context directly into the main ESP system event loop
        nmea_parser_add_handler(nmea_hdl, gps_event_handler, NULL);
        ESP_LOGI(TAG, "Official Registry NMEA engine mounted on UART1 successfully.");
    } else {
        ESP_LOGE(TAG, "Failed mounting the default system NMEA parser engine.");
    }
}

accel_data accel_get(void)
{
    int16_t data_raw[3];
    h3lis331dl_status_reg_t reg;
    h3lis331dl_status_reg_get(&accel_ctx, &reg);

    accel_data received_data = {0};

    if(reg.zyxda) {
        h3lis331dl_acceleration_raw_get(&accel_ctx, data_raw);
        received_data.x = h3lis331dl_from_fs400_to_mg(data_raw[0]) / 1000.0f;
        received_data.y = h3lis331dl_from_fs400_to_mg(data_raw[1]) / 1000.0f;
        received_data.z = h3lis331dl_from_fs400_to_mg(data_raw[2]) / 1000.0f;
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
            received_data.accel.x = lsm6dsv16x_from_fs2_to_mg(data_raw_xl[0]) / 1000.0f;
            received_data.accel.y = lsm6dsv16x_from_fs2_to_mg(data_raw_xl[1]) / 1000.0f;
            received_data.accel.z = lsm6dsv16x_from_fs2_to_mg(data_raw_xl[2]) / 1000.0f;
        }

        if (all_status.drdy_gy) {
            lsm6dsv16x_angular_rate_raw_get(&imu_ctx, data_raw_gy);
            received_data.gyro.x = lsm6dsv16x_from_fs4000_to_mdps(data_raw_gy[0]) / 1000.0f;
            received_data.gyro.y = lsm6dsv16x_from_fs4000_to_mdps(data_raw_gy[1]) / 1000.0f;
            received_data.gyro.z = lsm6dsv16x_from_fs4000_to_mdps(data_raw_gy[2]) / 1000.0f;
        }

        if (all_status.drdy_gy) {
            if (lsm6dsv16x_ln_pg_read(&imu_ctx, 0x6EU, (uint8_t *)data_raw_quat, 6) == 0) {
                received_data.quat.x = lsm6dsv16x_from_sflp_to_mg(data_raw_quat[0]);
                received_data.quat.y = lsm6dsv16x_from_sflp_to_mg(data_raw_quat[1]);
                received_data.quat.z = lsm6dsv16x_from_sflp_to_mg(data_raw_quat[2]);

                float sum_sq = (received_data.quat.x * received_data.quat.x) +
                               (received_data.quat.y * received_data.quat.y) +
                               (received_data.quat.z * received_data.quat.z);
                
                if (sum_sq < 1.0f) {
                    received_data.quat.w = sqrtf(1.0f - sum_sq);
                } else {
                    received_data.quat.w = 0.0f;
                }
            }
        }
    }
    return received_data;
}