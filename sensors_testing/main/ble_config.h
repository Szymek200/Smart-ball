#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

#include <stdbool.h>
#include "audio_player.h"

extern bool is_phone_connected;

void ble_config_init(void);

void ble_enable_pairing_mode(void);

#endif // BLE_CONFIG_H