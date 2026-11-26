# Each build directory remembers its selected firmware variant.
set(PLANT_VARIANT "unoptimised" CACHE STRING "Plant firmware: unoptimised, light, deep, or adaptive")
set_property(CACHE PLANT_VARIANT PROPERTY STRINGS unoptimised light deep adaptive)

if(PLANT_VARIANT STREQUAL "unoptimised")
    set(plant_source "unoptimised.c")
elseif(PLANT_VARIANT STREQUAL "light" OR PLANT_VARIANT STREQUAL "deep")
    set(plant_source "optimised.c")
elseif(PLANT_VARIANT STREQUAL "adaptive")
    set(plant_source "adaptive.c")
else()
    message(FATAL_ERROR "Unknown PLANT_VARIANT='${PLANT_VARIANT}'. Choose unoptimised, light, deep, or adaptive.")
endif()

idf_component_register(
    SRCS "${plant_source}"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_adc esp_driver_gpio esp_driver_ledc esp_driver_uart
                  esp_hw_support esp_system esp_timer spiffs
)

if(PLANT_VARIANT STREQUAL "deep")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PLANT_DEEP_SLEEP=1)
elseif(PLANT_VARIANT STREQUAL "light")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PLANT_DEEP_SLEEP=0)
endif()
message(STATUS "Plant firmware variant: ${PLANT_VARIANT} (${plant_source})")

# Explicit storage-flash only: ordinary firmware flashes preserve collected logs.
spiffs_create_partition_image(storage "${CMAKE_CURRENT_LIST_DIR}/../../Configuration")
