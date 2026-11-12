#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "esp_sleep.h" //Library for sleeping
#include "esp_system.h" //Library to detect when out of sleeping
#include "freertos/task.h"
#define WAKE_PIN GPIO_NUM_25 // wired to Pi pin 5 (GPIO3)
void app_main(void)
{
// Open-drain output: 1 = floating (released), 0 = pulled to ground
gpio_set_level(WAKE_PIN, 1);
gpio_config_t config = {
.pin_bit_mask = 1ULL << WAKE_PIN,
.mode = GPIO_MODE_OUTPUT_OD,
};
gpio_config(&config);
if (esp_reset_reason() != ESP_RST_DEEPSLEEP) { //If the ES was DEAP asleap don't run the code
    gpio_set_level(WAKE_PIN, 0); // press
    vTaskDelay(pdMS_TO_TICKS(250));
    gpio_set_level(WAKE_PIN, 1); // release
}

esp_sleep_enable_timer_wakeup(30ULL * 1000000); // wake up again in 30 s
esp_deep_sleep_start();
}