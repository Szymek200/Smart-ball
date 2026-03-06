idf_component_register(
    SRCS "L3G4200D.c"
    INCLUDE_DIRS "."
    REQUIRES
        # Wymagane przez L3G4200D.c (i2cdev)
        driver 
        log
)