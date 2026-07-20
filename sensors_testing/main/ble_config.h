#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

#include <stdbool.h>

extern bool is_phone_connected;

// Inicjalizacja stosu BLE oraz profilu GATT dla konfiguracji
void ble_config_init(void);

#endif // BLE_CONFIG_H