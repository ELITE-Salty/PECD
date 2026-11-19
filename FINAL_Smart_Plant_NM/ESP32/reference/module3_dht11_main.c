#include <stdio.h>
#include "dht.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define DHT_GPIO GPIO_NUM_26
#define READ_INTERVAL_MS 2500
static const char *TAG = "dht11";

void app_main(void)
{
    ESP_ERROR_CHECK(gpio_set_pull_mode(DHT_GPIO, GPIO_FLOATING));
    vTaskDelay(pdMS_TO_TICKS(2000));
    while (1) {
        float humidity, temperature;
        esp_err_t result = dht_read_float_data(DHT_TYPE_DHT11, DHT_GPIO,
            &humidity, &temperature);

        if (result == ESP_OK) {
            ESP_LOGI(TAG, "temperature=%.1f C humidity=%.1f %%RH", temperature,
            humidity);
        } else {
            ESP_LOGW(TAG, "Read failed: %s; no new measurement; retrying",
                esp_err_to_name(result));
        }
        vTaskDelay(pdMS_TO_TICKS(READ_INTERVAL_MS));
    }
}
