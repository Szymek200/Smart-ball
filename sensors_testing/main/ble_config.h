#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

#include <stdbool.h>

// Flaga określająca, czy telefon jest obecnie połączony z urządzeniem przez BLE
extern bool is_phone_connected;

// Inicjalizacja stosu NimBLE oraz profilu GATT dla konfiguracji
void ble_config_init(void);

#endif // BLE_CONFIG_H