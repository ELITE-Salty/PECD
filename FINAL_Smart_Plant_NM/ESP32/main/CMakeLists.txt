idf_component_register(
    SRCS "unoptimised.c" #If you want to run some other program like optimised or something just change the src to the name of the file
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_adc esp_driver_gpio esp_driver_ledc
)

