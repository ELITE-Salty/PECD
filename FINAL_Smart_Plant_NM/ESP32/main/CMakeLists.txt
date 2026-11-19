idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_adc
        esp_driver_gpio
        esp_driver_ledc
        esp_driver_uart
        esp_timer
        spiffs
        esp-idf-lib__dht
)

# configuration.txt has a single source of truth: ../../Configuration/.
# It is copied into a build-local folder that becomes the SPIFFS image, so the
# Configuration/README.md is not packed into flash. configure_file() re-runs
# CMake automatically when configuration.txt changes.
set(SPIFFS_IMAGE_DIR "${CMAKE_BINARY_DIR}/spiffs_image")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/../../Configuration/configuration.txt"
    "${SPIFFS_IMAGE_DIR}/configuration.txt"
    COPYONLY)

# No FLASH_IN_PROJECT on purpose (same as the first integrated version):
# `idf.py flash` updates only the firmware and therefore does NOT erase the
# events_log.txt collected on the device. Flash the storage partition
# explicitly with `idf.py storage-flash` (first time, or after editing rules).
spiffs_create_partition_image(storage "${SPIFFS_IMAGE_DIR}")
