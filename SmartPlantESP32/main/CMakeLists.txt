idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_adc esp_driver_gpio esp_driver_ledc esp_timer spiffs vfs esp_hw_support esp-idf-lib__dht
)
# Put configuration.txt into a dedicated SPIFFS partition.
# Note: `idf.py flash` overwrites this partition, including its log files.
spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
