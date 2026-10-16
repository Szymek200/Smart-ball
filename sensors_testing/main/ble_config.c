#include "ble_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_store.h"
#include "freertos/timers.h"

#include "measure.h"
#include "config_manager.h"

static const char *TAG = "BLE_CONFIG";

static uint16_t gatt_char_val_handle;
static uint8_t ble_addr_type;

static bool is_pairing_mode = false;
static TimerHandle_t pairing_timer = NULL;
static void pairing_timer_cb(TimerHandle_t xTimer);


// from measure
extern float config_wake_ths_g;
extern float config_sleep_ths_g;
extern int config_idle_time_s;
extern float crash_threshold_g;

bool is_phone_connected = false;
static volatile bool is_notify_enabled = false;

// Usługa GATT: "D7863B5F-AD11-431D-8A2D-05BD307ACB96"
static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0x96, 0xCB, 0x7A, 0x30, 0xBD, 0x05, 0x2D, 0x8A, 0x1D, 0x43, 0x11, 0xAD, 0x5F, 0x3B, 0x86, 0xD7);

// Charakterystyka konfiguracyjna (Write/Read): "07694EBE-272D-4D30-ADB0-223B449DBF48"
static const ble_uuid128_t gatt_svr_chr_uuid =
    BLE_UUID128_INIT(0x48, 0xBF, 0x9D, 0x44, 0x3B, 0x22, 0xB0, 0xAD, 0x30, 0x4D, 0x2D, 0x27, 0xBE, 0x4E, 0x69, 0x07);

// Charakterystyka danych (Notify): "1C081A8B-3707-4E39-A5BA-2D10191AE744"
static const ble_uuid128_t gatt_data_chr_uuid =
    BLE_UUID128_INIT(0x44, 0xE7, 0x1A, 0x19, 0x10, 0x2D, 0xBA, 0xA5, 0x39, 0x4E, 0x07, 0x37, 0x8B, 0x1A, 0x08, 0x1C);


void ble_store_config_init(void);
static void ble_app_on_reset(int reason);
static void ble_app_on_sync(void);
static void ble_host_task(void *param);
static void ble_data_tx_task(void *pvParameters);
static void ble_app_advertise(void);
static int ble_app_gap_event(struct ble_gap_event *event, void *arg);
static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

// Przechowujemy identyfikator aktywnego połączenia i uchwyt nowej charakterystyki
static uint16_t active_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t gatt_data_char_val_handle;

//arrays configuring GATT
static const struct ble_gatt_chr_def gatt_svr_chrs[] = {
    {
        // 1. Charakterystyka konfiguracyjna (ZAPIS / ODCZYT)
        .uuid = &gatt_svr_chr_uuid.u, //uuid
        .access_cb = gatt_svr_chr_access, //function
        .flags = BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .val_handle = &gatt_char_val_handle,
    },
    {
        // 2. Charakterystyka NOTIFY
        .uuid = &gatt_data_chr_uuid.u,
        .access_cb = gatt_svr_chr_access,
        .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
        .val_handle = &gatt_data_char_val_handle,
    },
    { 0 } 
};

// Tablica usług GATT - services
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = gatt_svr_chrs,
    },
    {
        0, 
    },
};

//turning on ble
void ble_config_init(void) {
    int rc;

    rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "Błąd nimble_port_init: %d", rc);
        return;
    }

    ble_hs_cfg.reset_cb = ble_app_on_reset;
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT; 
    
    // zapamietywanie sparowanych urzadzen
    ble_hs_cfg.sm_bonding = 1; 
    
    //  Secure Connections
    ble_hs_cfg.sm_sc = 1;      
    
    // key distribution
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
 
    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Błąd ble_gatts_count_cfg: %d", rc);
        return;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Błąd ble_gatts_add_svcs: %d", rc);
        return;
    }

    rc = ble_svc_gap_device_name_set("SmartBall");
    if (rc != 0) {
        ESP_LOGE(TAG, "Błąd ble_svc_gap_device_name_set: %d", rc);
    }

    ble_store_config_init();
    nimble_port_freertos_init(ble_host_task);
    //tasks for sending notifications
    xTaskCreate(ble_data_tx_task, "ble_data_tx_task", 4096, NULL, 5, NULL);
}

static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE Host Task uruchomiony.");
    nimble_port_run(); //bluetooth event loop - it is separate task(for all general BLE tasks)
    nimble_port_freertos_deinit();
}

//setting MAC
static void ble_app_on_sync(void) {

    //getting MAC
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Błąd ble_hs_util_ensure_addr: %d", rc);
    }

    rc = ble_hs_id_infer_auto(0, &ble_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Błąd ble_hs_id_infer_auto: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE Zsynchronizowany. Startowanie rozgłaszania...");
    ble_app_advertise();
}

//broadcasting that we are available
static void ble_app_advertise(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name = "SmartBall";

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP; //generally visible(all the time), only BLE
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Błąd ble_gap_adv_set_fields: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; //every MAC connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; //all visible

    // MECHANIZM WHITE-LIST
    ble_addr_t bonded_addrs[3];
    int num_bonded = 0;
    
    rc = ble_store_util_bonded_peers(bonded_addrs, &num_bonded, 3);
    
    if (is_pairing_mode) {
        ESP_LOGI(TAG, "[BLE] Tryb parowania AKTYWNY. Rozgłaszanie otwarte!");
        adv_params.filter_policy = BLE_HCI_ADV_FILT_NONE;
    } 
    else if (rc == 0 && num_bonded > 0) {
        ESP_LOGI(TAG, "[BLE] Znaleziono %d sparowanych urządzeń. Włączam WhiteList!", num_bonded);
        ble_gap_wl_set(bonded_addrs, num_bonded);
        adv_params.filter_policy = BLE_HCI_ADV_FILT_CONN; 
    } else {
        ESP_LOGI(TAG, "[BLE] Brak sparowanych urządzeń. Rozgłaszanie otwarte.");
        adv_params.filter_policy = BLE_HCI_ADV_FILT_NONE;
    }
    // ----------------------------

    // Uruchomienie radia
    rc = ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_app_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Błąd ble_gap_adv_start: %d", rc);
    }
}

//main entry point when phone connects or disconnects or set notify
static int ble_app_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                is_phone_connected = true;
                active_conn_handle = event->connect.conn_handle;
            } else {
                ble_app_advertise(); //failed connection
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            is_phone_connected = false;
            is_notify_enabled = false; 
            active_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ble_app_advertise();
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "Zdarzenie SUBSCRIBE: attr_handle=%d, gatt_data_handle=%d, cur_notify=%d",
                     event->subscribe.attr_handle, 
                     gatt_data_char_val_handle, 
                     event->subscribe.cur_notify);

            if (event->subscribe.attr_handle == gatt_data_char_val_handle) {
                is_notify_enabled = event->subscribe.cur_notify;
                if (is_notify_enabled) {
                    ESP_LOGI(TAG, ">>> SUBSRYPCJA NOTIFY WŁĄCZONA DLA TELEFONU! <<<");
                } else {
                    ESP_LOGI(TAG, ">>> SUBSRYPCJA NOTIFY WYŁĄCZONA! <<<");
                }
            }
            break;

        default:
            break;
    }
    return 0;
}


//sending streaming data
static void ble_data_tx_task(void *pvParameters)
{
    global_data_t sensor_data;

    while (1) {
        
        if (xQueueReceive(data_queue, &sensor_data, portMAX_DELAY)) {
            
            if (is_phone_connected && is_notify_enabled && active_conn_handle != BLE_HS_CONN_HANDLE_NONE) {

                struct os_mbuf *om = ble_hs_mbuf_from_flat(&sensor_data, sizeof(global_data_t));
                if (om != NULL) {
                    int rc = ble_gatts_notify_custom(active_conn_handle, gatt_data_char_val_handle, om);
                    if (rc != 0) {
                        os_mbuf_free_chain(om);
                    }
                } else {
                    ESP_LOGE(TAG, "Brak pamięci mbuf dla powiadomienia BLE");
                }
            }
        }
    }
}

//read, write callback - main way to communicate
static bool process_ble_command(const char *cmd)
{
    float f_val;
    int i_val;

    if (sscanf(cmd, "CMD:WAKE_THS:%f", &f_val) == 1) {
        ESP_LOGI(TAG, "[BLE] WAKE_THS: %.3f -> %.3f", config_wake_ths_g, f_val);
        config_wake_ths_g = f_val;
        return true;
    }
    if (sscanf(cmd, "CMD:HIT_THS:%f", &f_val) == 1) {
        ESP_LOGI(TAG, "[BLE] HIT_THS: %.3f -> %.3f", crash_threshold_g, f_val);
        crash_threshold_g = f_val;
        return true;
    }
    if (sscanf(cmd, "CMD:IDLE_TIME:%d", &i_val) == 1) {
        ESP_LOGI(TAG, "[BLE] IDLE_TIME: %d -> %d", config_idle_time_s, i_val);
        config_idle_time_s = i_val;
        return true;
    }
    if (sscanf(cmd, "CMD:SLEEP_THS:%f", &f_val) == 1) {
        ESP_LOGI(TAG, "[BLE] SLEEP_THS: %.3f -> %.3f", config_sleep_ths_g, f_val);
        config_sleep_ths_g = f_val;
        return true;
    }
    if (strcmp(cmd, "CMD:PLAY") == 0) {
        ESP_LOGI(TAG, "[BLE] Komenda PLAY");
        play_raw("/spiffs/dzwonek.raw");
        return false; 
    }
    if (strcmp(cmd, "CMD:STOP") == 0) {
        ESP_LOGI(TAG, "[BLE] Komenda STOP");
        stop_raw();
        return false;
    }

    ESP_LOGE(TAG, "[BLE] NIEZNANA KOMENDA: '%s'", cmd);
    return false;
}

static int handle_config_write(struct ble_gatt_access_ctxt *ctxt)
{
    char rx_buffer[128];
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

    if (len >= sizeof(rx_buffer)) {
        len = sizeof(rx_buffer) - 1;
    }

    int rc = ble_hs_mbuf_to_flat(ctxt->om, rx_buffer, len, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "[BLE] Błąd ble_hs_mbuf_to_flat: %d", rc);
        return BLE_ATT_ERR_UNLIKELY;
    }

    // Ucinanie znaków nowej linii i zakończenie stringa
    rx_buffer[len] = '\0';
    for (int i = 0; i < len; i++) {
        if (rx_buffer[i] == '\r' || rx_buffer[i] == '\n') {
            rx_buffer[i] = '\0';
            break;
        }
    }

    ESP_LOGI(TAG, "[BLE] ODEBRANY TEKST: '%s'", rx_buffer);

    // Przekazanie do warstwy decyzyjnej
    bool should_save = process_ble_command(rx_buffer);
    if (should_save) {
        ESP_LOGI(TAG, "[BLE] Zapis konfiguracji do NVS...");
        save_config_to_nvs();
    }

    return 0;
}

static int handle_config_read(struct ble_gatt_access_ctxt *ctxt)
{
    char tx_buffer[128];
    snprintf(tx_buffer, sizeof(tx_buffer), "CFG:%.2f:%.3f:%d:%.1f\n",
             config_wake_ths_g, config_sleep_ths_g, config_idle_time_s, crash_threshold_g);

    ESP_LOGI(TAG, "[BLE] Wysyłam konfigurację: %s", tx_buffer);

    int rc = os_mbuf_append(ctxt->om, tx_buffer, strlen(tx_buffer));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            return handle_config_read(ctxt);

        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            if (attr_handle != gatt_char_val_handle) {
                ESP_LOGE(TAG, "[BLE] Odrzucono WRITE (nieprawidłowy attr_handle: %d)", attr_handle);
                return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
            }
            return handle_config_write(ctxt);

        default:
            ESP_LOGW(TAG, "[BLE] Nieobsługiwana operacja GATT: %d", ctxt->op);
            return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    }
}

//pairing functions

static void pairing_timer_cb(TimerHandle_t xTimer) {
    ESP_LOGI(TAG, "Czas otwartego parowania (60s) minął. Zamykam Bluetooth.");
    is_pairing_mode = false;

    //restart bluetooth
    ble_gap_adv_stop();
    ble_app_advertise();
}

void ble_enable_pairing_mode(void) {
    if (is_pairing_mode) return; 

    is_pairing_mode = true;
 
    //reset radio
    ble_gap_adv_stop(); 
    ble_app_advertise(); 

    // Timer (60 sekund)
    if (pairing_timer == NULL) {
        pairing_timer = xTimerCreate("pair_timer", pdMS_TO_TICKS(60000), pdFALSE, (void *)0, pairing_timer_cb);
    }
    xTimerStart(pairing_timer, 0);
}


static void ble_app_on_reset(int reason) {
    ESP_LOGE(TAG, "Reset stosu BLE, powód: %d", reason);
}



