#include "ble_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "measure.h"


#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_CONFIG";

static uint16_t gatt_char_val_handle;
static uint8_t ble_addr_type;

// Zmienne zewnętrzne z measure.c
extern float config_wake_ths_g;
extern float config_sleep_ths_g;
extern int config_idle_time_s;
extern float CRASH_THRESHOLD_G;

bool is_phone_connected = false;


void ble_store_config_init(void);


static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0x78, 0x56, 0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12);

static const ble_uuid128_t gatt_svr_chr_uuid =
    BLE_UUID128_INIT(0x87, 0x09, 0x21, 0x43, 0x65, 0x87, 0x21, 0x43, 0x89, 0x67, 0x21, 0x43, 0x21, 0x43, 0x65, 0x87);

static void ble_app_advertise(void);
static int ble_app_gap_event(struct ble_gap_event *event, void *arg);
static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);


static const struct ble_gatt_chr_def gatt_svr_chrs[] = {
    {
        .uuid = &gatt_svr_chr_uuid.u,
        .access_cb = gatt_svr_chr_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
        .val_handle = &gatt_char_val_handle,
    },
    {
        0, /* Terminator */
    }
};

/* Tablica usług GATT */
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

void save_config_to_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("nvs", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_blob(my_handle, "wake_ths", &config_wake_ths_g, sizeof(float));
        nvs_set_blob(my_handle, "sleep_ths", &config_sleep_ths_g, sizeof(float));
        nvs_set_blob(my_handle, "idle_time", &config_idle_time_s, sizeof(int));
        nvs_set_blob(my_handle, "crash_ths", &CRASH_THRESHOLD_G, sizeof(float));
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Konfiguracja zapisana w NVS.");
    }
}

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    char tx_buffer[128];
    
    if (ctxt->op == BLE_ATT_ACCESS_OP_READ) {
        snprintf(tx_buffer, sizeof(tx_buffer), "CFG:%.2f:%.3f:%d:%.1f\n", 
                 config_wake_ths_g, config_sleep_ths_g, config_idle_time_s, CRASH_THRESHOLD_G);
        int rc = os_mbuf_append(ctxt->om, tx_buffer, strlen(tx_buffer));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op == BLE_ATT_ACCESS_OP_WRITE) {
        char rx_buffer[128];
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len >= sizeof(rx_buffer)) len = sizeof(rx_buffer) - 1;
        
        ble_hs_mbuf_to_flat(ctxt->om, rx_buffer, len, NULL);
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
        return 0;
    }
    
    return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
}

static int ble_app_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "Połączono z telefonem! status=%d", event->connect.status);
            if (event->connect.status == 0) {
                is_phone_connected = true;
            } else {
                ble_app_advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Rozłączono z telefonem. Wznawiam rozgłaszanie...");
            is_phone_connected = false;
            ble_app_advertise();
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(TAG, "Zakończono rozgłaszanie, wznawiam...");
            ble_app_advertise();
            break;

        default:
            break;
    }
    return 0;
}

static void ble_app_advertise(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name = "SmartBall-Config";

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Błąd ble_gap_adv_set_fields: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_app_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Błąd ble_gap_adv_start: %d", rc);
    }
}

static void ble_app_on_sync(void) {
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

static void ble_app_on_reset(int reason) {
    ESP_LOGE(TAG, "Reset stosu BLE, powód: %d", reason);
}

static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE Host Task uruchomiony.");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_config_init(void) {
    int rc;


    rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "Błąd nimble_port_init: %d", rc);
        return;
    }

    
    ble_hs_cfg.reset_cb = ble_app_on_reset;
    ble_hs_cfg.sync_cb = ble_app_on_sync;

 
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
}