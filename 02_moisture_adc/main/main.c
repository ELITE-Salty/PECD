#include <stdio.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define MOISTURE_CHANNEL ADC_CHANNEL_6
static const char *TAG = "moisture";

void app_main(void)
{
    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &adc));
    adc_oneshot_chan_cfg_t channel = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, MOISTURE_CHANNEL, &channel));
    ESP_LOGI(TAG, "GPIO34 / ADC1 CH6; raw 0..4095, NOT moisture percent");
    while (1) {
        int raw;
        esp_err_t result = adc_oneshot_read(adc, MOISTURE_CHANNEL, &raw);
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "raw=%d normalized=%.3f", raw, raw / 4095.0f);
        } else {
            ESP_LOGW(TAG, "ADC read failed: %s", esp_err_to_name(result));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}