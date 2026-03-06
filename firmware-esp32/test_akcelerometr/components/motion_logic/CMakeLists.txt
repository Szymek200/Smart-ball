idf_component_register(
    SRCS
        "circular_buffer.c"  # Właściwa implementacja struktur bufora
        "normalize.c"        # Implementacja logiki Quaternionów i fuzji
        "stats.c"          
        
    INCLUDE_DIRS "."      # Włącz nagłówki z tego katalogu (.h)
    
    REQUIRES
        "esp_system"      # Podstawowe funkcje IDF
        "freertos"        # Wymagane przez Taski
        "log"             # Dla logowania
        "driver"          # Dla esp_timer_get_time()
        "newlib"          # Dla funkcji matematycznych (math.h)
        "esp_timer"
        
)