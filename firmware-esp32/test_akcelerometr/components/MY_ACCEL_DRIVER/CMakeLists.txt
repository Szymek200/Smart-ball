# components/h3lis331dl_driver/CMakeLists.txt
idf_component_register(
    SRCS 
        "h3lis331dl_reg.c"
        "platform_adapter.c" # Twój nowy plik
    
    INCLUDE_DIRS 
        "."
        
    REQUIRES 
        driver # Dodaje zależności do sterowników GPIO, I2C, SPI itd.
)