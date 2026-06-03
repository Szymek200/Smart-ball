#include "measure.h"
#include "h3lis331dl_reg.h"
#include "lsm6dsv16x_reg.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "H3LIS331DL_SPI";


//struktura z STM
//podpinamy wlasne funkcje zapisu i odczutu do funkcji bibliotecznych
static stmdev_ctx_t accel_ctx;
static stmdev_ctx_t imu_ctx;

static sensor_spi_handle_t accel_hardware;
static sensor_spi_handle_t imu_hardware;

//handler - do jakiego czujnika chcemy wyslac informacje
//reg - adres wewnetrznego rejestru wewnatrz czujnika
//bufp -dane, ktore chcemy zapisac
//len - ilosc danych w buforze
static int32_t sensor_write(void *handle, uint8_t header, const uint8_t *bufp, uint16_t len)
{
    sensor_spi_handle_t *sensor = (sensor_spi_handle_t *)handle;

    if (sensor->cs_pin == PIN_ACCEL_CS && len > 1) {
        header |= 0x40; // Tylko stary czujnik potrzebuje maski MS
        //do wielobajtowej komunkacji z akcelerometrem
        //ma zwiekszac adresy rejestru wewnetrznego

    }

    uint8_t tx_data[1 + 16]; //dodatkowy bajt dla header
    tx_data[0] = header;
    //kopiujemy dane z argumenty funkcji do tx_data
    memcpy(&tx_data[1], bufp, len);

    spi_transaction_t trans ={
        .length = (1 + len) * 8, //dlugosc w bitach
        .tx_buffer = tx_data,
        .rx_buffer = NULL //ignorujemy, co kontroler wysyla
    };

    //blokujaca komunikacja z czujnikiem
    //
    return spi_device_polling_transmit(sensor->spi_handle, &trans) == ESP_OK ? 0 : -1;

}

static int32_t sensor_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len)
{
    
    sensor_spi_handle_t *sensor = (sensor_spi_handle_t *)handle;
    uint8_t header = reg | 0x80;

    if (sensor->cs_pin == PIN_ACCEL_CS && len > 1) {
        header |= 0x40; // Tylko stary czujnik potrzebuje maski MS
    }


    //naglowek komunikacji zawiera
    //RW(read/write)
    //MS(multiple/single)
    //5 - 0: adres rejestru


    //SPI - full duplex
    //przy nadaniu bajtow, tyle samo musza odebrac

    //dodatkowy bajt, bo tracimy go na header

    //pomimo zmiennej len, maksymalnie mozemy odczytac 16 bajtow
    uint8_t tx_data[16+1] = {0};
    uint8_t rx_data[16+1] = {0};
    tx_data[0] = header;


    //mimo, ze odczytujemuy dane to najpierw wyslac info do odpowiedniego czujnika, co od niego chcemy
    spi_transaction_t trans= {
        .length = (1 + len)*8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };

    if(spi_device_polling_transmit(sensor->spi_handle, &trans) != ESP_OK)
    {
        return -1;
    }

    //dest, source
    memcpy(bufp, &rx_data[1], len);
    return 0;
}

void sensors_set(void)
{
    //do przechowywania wyniku funkcji systemowej
    esp_err_t ret;

    //uchwyt do urzadzenia spi
    spi_device_handle_t spi_accel_handle;
    spi_device_handle_t spi_imu_handle;

    spi_bus_config_t buscfg = 
    {
        .miso_io_num = PIN_MISO,
        .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_SCLK,
        //quad write protect
        .quadwp_io_num = -1, //wylaczenie trybu ochrony przed zapisem
        //quad hold
        .quadhd_io_num = -1, //wylaczenie linii wstrzymania komunikacji
        .max_transfer_sz = 32
    };

    //REJESTRACJA CZUJNIKOW

    //uruchomienie kontrolera spi w esp
    //spi1 - wykorzystywany do czytania wlasnej pamieci flash
    //wykorzystujemy tryb z DMA
    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret); //sprawdzenie czy inicjalizacja sie udala

    spi_device_interface_config_t devcfg_accel = {
        .clock_speed_hz = 1 * 1000 * 1000, //czestotliwosc - 1 MHz
        .mode = 3, //kombinacja polaryzacji zegara i fazy zegara
        .spics_io_num = PIN_ACCEL_CS,
        .queue_size = 7
    };

    ret = spi_bus_add_device(SPI2_HOST, &devcfg_accel, &spi_accel_handle);
    ESP_ERROR_CHECK(ret);


    spi_device_interface_config_t devcfg_imu = {
        .clock_speed_hz = 5 * 1000 * 1000, // Nowe IMU bez problemu radzi sobie z 5 MHz (jest szybsze!)
        .mode = 3,                         // Tryb SPI 3 jest poprawny również dla LSM6DSV
        .spics_io_num = PIN_IMU_CS,        // Drugi, dedykowany pin CS z measure.h
        .queue_size = 7
    };
    ret = spi_bus_add_device(SPI2_HOST, &devcfg_imu, &spi_imu_handle);
    ESP_ERROR_CHECK(ret);

    //powiazanie interfejsu STM z moimi funkcjami SPI

    accel_hardware.spi_handle = spi_accel_handle;
    accel_hardware.cs_pin = PIN_ACCEL_CS;
    accel_ctx.handle = (void*)&accel_hardware;

    imu_hardware.spi_handle = spi_imu_handle;
    imu_hardware.cs_pin = PIN_IMU_CS;
    imu_ctx.handle = (void*)&imu_hardware;

    //sprawdzenie komunikacji

    uint8_t whoamI = 0;
    h3lis331dl_device_id_get(&accel_ctx, &whoamI);

    if(whoamI != H3LIS331DL_ID)
    {
        ESP_LOGE(TAG, "Accelerometr wasn't found. Received id: 0x%02X (Expected 0x%02X, whoamI, H3LIS331DL_ID)");
    }
    else
    {
        ESP_LOGI(TAG,"Accelerometr was found");
    }

    //konfigurowanie czujnika

    h3lis331dl_data_rate_set(&accel_ctx, H3LIS331DL_ODR_100Hz); //czestotliwosc odswiezania
    h3lis331dl_full_scale_set(&accel_ctx, H3LIS331DL_400g); //skala pomiarow


    h3lis331dl_device_id_get(&imu_ctx, &whoamI);

    if(whoamI != LSM6DSV16X_ID)
    {
        ESP_LOGE(TAG, "IMU wasn't found. Received id: 0x%02X (Expected 0x%02X", whoamI, LSM6DSV16X_ID);
    }
    else
    {
        ESP_LOGI(TAG,"IMU was found");
    }

    // odświeżania (np. 120 Hz)
    lsm6dsv16x_xl_data_rate_set(&imu_ctx, LSM6DSV16X_ODR_AT_960Hz);
    lsm6dsv16x_gy_data_rate_set(&imu_ctx, LSM6DSV16X_ODR_AT_960Hz);

    //maksymalne zakresy
    lsm6dsv16x_xl_full_scale_set(&imu_ctx, LSM6DSV16X_2g);       
    lsm6dsv16x_gy_full_scale_set(&imu_ctx, LSM6DSV16X_4000dps);

    //SFLP
    //fuzja sensorow

    //czestotliwosc
    lsm6dsv16x_sflp_data_rate_set(&imu_ctx, LSM6DSV16X_SFLP_120Hz);

    // Aktywacja sprzętowego generowania kwaternionu (Game Rotation Vector)
    lsm6dsv16x_sflp_game_rotation_set(&imu_ctx, PROPERTY_ENABLE);

  
}

accel_data accel_get(void)
{
    int16_t data_raw[3];

    //odczytywanie pomiarow

    h3lis331dl_status_reg_t reg;
    h3lis331dl_status_reg_get(&accel_ctx, &reg);

    accel_data received_data;

    if(reg.zyxda) //.zyxda (Z, Y, X data available)
    {
        //pobranie danych
        h3lis331dl_acceleration_raw_get(&accel_ctx, data_raw);

        //przeliczenie na mili g
        received_data.x = h3lis331dl_from_fs400_to_mg(data_raw[0])/ 1000.0f;
        received_data.y = h3lis331dl_from_fs400_to_mg(data_raw[1])/ 1000.0f;
        received_data.z = h3lis331dl_from_fs400_to_mg(data_raw[2])/ 1000.0f;
    }

    return received_data;
   
    //vTaskDelay(pdMS_TO_TICKS(10)); // Częstotliwość sprawdzania rejestru (10ms)
}

imu_data imu_get(void)
{
    int16_t data_raw_xl[3] = {0};
    int16_t data_raw_gy[3] = {0};
    int16_t data_raw_quat[3] = {0}; // Tablica na 3 osie kwaternionu z SFLP
    
    // Struktura wyjściowa z zainicjalizowanymi zerami
    imu_data received_data = {0};

    // Jedna struktura na status, z której będziemy korzystać
    lsm6dsv16x_all_sources_t all_status;

    // Pobierz statusy wszystkich rejestrów źródłowych IMU
    if (lsm6dsv16x_all_sources_get(&imu_ctx, &all_status) == 0) 
    {
        // 1. Akcelerometr (Używamy poprawnej zmiennej all_status)
        if (all_status.drdy_xl)
        {
            lsm6dsv16x_acceleration_raw_get(&imu_ctx, data_raw_xl);

            received_data.accel.x = lsm6dsv16x_from_fs2_to_mg(data_raw_xl[0]) / 1000.0f;
            received_data.accel.y = lsm6dsv16x_from_fs2_to_mg(data_raw_xl[1]) / 1000.0f;
            received_data.accel.z = lsm6dsv16x_from_fs2_to_mg(data_raw_xl[2]) / 1000.0f;
        }

        // 2. Żyroskop
        if (all_status.drdy_gy)
        {
            lsm6dsv16x_angular_rate_raw_get(&imu_ctx, data_raw_gy);

            received_data.gyro.x = lsm6dsv16x_from_fs4000_to_mdps(data_raw_gy[0]) / 1000.0f;
            received_data.gyro.y = lsm6dsv16x_from_fs4000_to_mdps(data_raw_gy[1]) / 1000.0f;
            received_data.gyro.z = lsm6dsv16x_from_fs4000_to_mdps(data_raw_gy[2]) / 1000.0f;
        }

        // 3. Blok Fuzji Sensorów (SFLP)
        // W trybie bezpośrednim (bez FIFO), gotowość nowych danych kwaternionu 
        // pokrywa się z częstotliwością odświeżania danych żyroskopu.
        if (all_status.drdy_gy) 
        {
            // Adres rejestru surowych danych kwaternionu (X) SFLP w pamięci zaawansowanej to 0x6E.
            // Czytamy 6 bajtów (3 osie x 2 bajty), które funkcja automatycznie rzutuje na int16_t.
            if (lsm6dsv16x_ln_pg_read(&imu_ctx, 0x6EU, (uint8_t *)data_raw_quat, 6) == 0)
            {
                // Oficjalna konwersja ST z formatu SFLP do wartości float (-1.0 do 1.0)
                received_data.quat.x = lsm6dsv16x_from_sflp_to_mg(data_raw_quat[0]);
                received_data.quat.y = lsm6dsv16x_from_sflp_to_mg(data_raw_quat[1]);
                received_data.quat.z = lsm6dsv16x_from_sflp_to_mg(data_raw_quat[2]);

                // Składową W obliczamy matematycznie ze wzoru na kwaternion jednostkowy
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