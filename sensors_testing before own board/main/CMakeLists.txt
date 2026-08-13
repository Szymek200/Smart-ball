idf_component_register(
    SRCS
        "main.c"
        "measure.c"
        "normalize.c"
        "communicate.c"
        "gsm.c"
        "audio_player.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver          
        esp_event       
        esp_netif       
        nmea0183_parser_main  
        esp_driver_ledc
    PRIV_REQUIRES
        esp_driver_spi
        esp_driver_gpio
        esp_driver_i2s
        esp_wifi
        esp_modem
        mqtt          
        esp_http_client      
        spiffs            
        nvs_flash
        lis331
        lsm6
        esp_driver_i2s
        spiffs
)


#spiffs_register_flash_partition(storage "../spiffs_image" FLASH_IN_PROJECT)