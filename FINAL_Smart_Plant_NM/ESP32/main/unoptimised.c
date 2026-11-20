#include <stdio.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"

/*PI wakeup pin*/
#define WAKE_PIN GPIO_NUM_25 // wired to Pi pin 5 (GPIO3)

/*DHT 11*/
#include "dht.h"
#define DHT_GPIO GPIO_NUM_26
#define READ_INTERVAL_MS 2500

/*Moisture*/
#define MOISTURE_CHANNEL ADC_CHANNEL_6
#define MOISTURE_WET 2560
#define MOISTURE_DRY 0

/*Buzzer*/
#define BUZZER_GPIO GPIO_NUM_27
#define TONE_HZ 800
#define BUZZER_MODE LEDC_LOW_SPEED_MODE
#define BUZZER_CHANNEL LEDC_CHANNEL_0
#define BEEP_ON_MS 200
#define BEEP_OFF_MS 800
//Buzzer
static const char *TAG = "buzzer";
//Moisture
static const char *TAGM = "moisture";
//DHT 11
static const char *TAGD = "dht11";


static void beep(int on_ms, int off_ms, int times)
{
    for (int i = 0; i < times; ++i) {
        ESP_ERROR_CHECK(ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 512));
        ESP_ERROR_CHECK(ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL));
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        ESP_ERROR_CHECK(ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0));
        ESP_ERROR_CHECK(ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL));
        if (off_ms > 0) vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}


void app_main(void)
{
    /*Pi wake part*/
    // Open-drain output: 1 = floating (released), 0 = pulled to ground
    gpio_set_level(WAKE_PIN, 1);
    gpio_config_t config = {
    .pin_bit_mask = 1ULL << WAKE_PIN,
    .mode = GPIO_MODE_OUTPUT_OD,
    };
    gpio_config(&config);
    gpio_set_level(WAKE_PIN, 0); // press
    vTaskDelay(pdMS_TO_TICKS(250));
    gpio_set_level(WAKE_PIN, 1); // release

    /* Beeper part */
    const ledc_timer_config_t timer = {
        .speed_mode = BUZZER_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = TONE_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
        const ledc_channel_config_t channel = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = BUZZER_MODE,
        .channel = BUZZER_CHANNEL,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    ESP_LOGI(TAG, "Three short beeps");
    //beep(BEEP_ON_MS, BEEP_OFF_MS, 3);
    ESP_LOGI(TAG, "One long beep");
    //beep(5000, 0, 1);
    ESP_LOGI(TAG, "Finished; press EN to repeat");

    /* Moisture */

    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &adc));
    adc_oneshot_chan_cfg_t channelMoist = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, MOISTURE_CHANNEL, &channelMoist));
    ESP_ERROR_CHECK(gpio_set_pull_mode(DHT_GPIO, GPIO_FLOATING));
    ESP_LOGI(TAGM, "GPIO34 / ADC1 CH6; raw 0..4095, NOT moisture percent");
    while (1) {
        int raw;
        esp_err_t result = adc_oneshot_read(adc, MOISTURE_CHANNEL, &raw);

        float moisture = 100.0f *
            (MOISTURE_DRY - raw) /
            (MOISTURE_DRY - MOISTURE_WET);

        if (moisture > 100.0f) moisture = 100.0f;
        if (moisture < 0.0f) moisture = 0.0f;

        ESP_LOGI(TAGM, "Raw=%d Moisture=%.1f%%", raw, moisture);

        float humidity, temperature;
        esp_err_t resultD = dht_read_float_data(DHT_TYPE_DHT11, DHT_GPIO,
            &humidity, &temperature);

        if (resultD == ESP_OK) {
            ESP_LOGI(TAGD, "temperature=%.1f C humidity=%.1f %%RH", temperature,
            humidity);
        } else {
            ESP_LOGW(TAGD, "Read failed: %s; no new measurement; retrying",
                esp_err_to_name(resultD));
        }
        vTaskDelay(pdMS_TO_TICKS(READ_INTERVAL_MS));
    }
}