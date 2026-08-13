
/*
 * ble_config.c
 * Skeleton prepared for ESP-IDF 6.x + NimBLE.
 * TODO: full implementation continues.
 */
#include "ble_config.h"
#include "measure.h"
#include "esp_log.h"
#include "nvs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG="BLE";
bool is_phone_connected=false;

void save_config_to_nvs(void)
{
    nvs_handle_t h;
    if(nvs_open("nvs",NVS_READWRITE,&h)!=ESP_OK) return;

    nvs_set_blob(h,"wake_ths",&config_wake_ths_g,sizeof(config_wake_ths_g));
    nvs_set_blob(h,"sleep_ths",&config_sleep_ths_g,sizeof(config_sleep_ths_g));
    nvs_set_blob(h,"idle_time",&config_idle_time_s,sizeof(config_idle_time_s));
    nvs_set_blob(h,"crash_ths",&CRASH_THRESHOLD_G,sizeof(CRASH_THRESHOLD_G));

    nvs_commit(h);
    nvs_close(h);
}

void ble_send_text(const char *text)
{
    (void)text;
}

void ble_config_init(void)
{
    ESP_LOGI(TAG,"Placeholder BLE init");
}
