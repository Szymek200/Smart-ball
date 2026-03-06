# main/CMakeLists.txt (Poprawiona nomenklatura dla ESP-IDF v5.x)

idf_component_register(
    SRCS "main.c" 
    INCLUDE_DIRS "." 
    
    REQUIRES 
        # Podstawowe zależności IDF
        "driver" 
        "esp_system"
        "freertos"
        "log"
        
        # Zależności do obsługi Wi-Fi / UDP 
        "nvs_flash"   # Poprawna nazwa
        "esp_wifi"    # ZMIANA: Zamiast "wifi" używamy "esp_wifi"
        # "esp_event" nie jest potrzebny, jest częścią esp_system/esp_event
        "lwip" 
        "esp_timer"
        "motion_logic"
        
        # Sterowniki urządzeń
        "MY_ACCEL_DRIVER" 
        "L3G4200D_DRIVER"
)