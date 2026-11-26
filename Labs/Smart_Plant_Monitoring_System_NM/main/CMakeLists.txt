idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_adc
        esp_driver_gpio
        esp_driver_ledc
        spiffs
        esp-idf-lib__dht
        esp_driver_uart
)

spiffs_create_partition_image(storage ../spiffs)