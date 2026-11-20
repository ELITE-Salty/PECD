idf_component_register(
    SRCS "unoptimised.c" #If you want to run some other program like optimised or something just change the src to the name of the file
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_adc esp_driver_gpio esp_driver_ledc esp_driver_uart esp_timer spiffs
)

# Explicit storage-flash only: ordinary firmware flashes preserve collected logs.
spiffs_create_partition_image(storage "${CMAKE_CURRENT_LIST_DIR}/../../Configuration")

