#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdbool.h>

void config_manager_init(void);

void load_config_from_nvs(void);

void save_config_to_nvs(void);

#endif // CONFIG_MANAGER_H