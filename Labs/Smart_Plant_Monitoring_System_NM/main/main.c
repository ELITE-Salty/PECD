/*
 * Simple Smart Plant Monitoring System
 * The three original examples are combined into ONE app_main().
 * ESP32: DHT11 GPIO26, soil sensor GPIO34, buzzer GPIO27.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include "dht.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DHT_GPIO          GPIO_NUM_26
#define MOISTURE_CHANNEL  ADC_CHANNEL_6   // GPIO34, classic ESP32
#define BUZZER_GPIO       GPIO_NUM_27
#define BUZZER_MODE       LEDC_LOW_SPEED_MODE
#define BUZZER_CHANNEL    LEDC_CHANNEL_0
#define READ_INTERVAL_MS  5000           // Debug: sensor readings every 5s

// REPLACE these with raw ADC measurements from YOUR soil sensor.
#define SOIL_RAW_DRY      3200
#define SOIL_RAW_WET      1300

static const char *TAG = "plant";
static adc_oneshot_unit_handle_t adc;
static bool storage_ready = false;

// One rule looks like: "3: soil_humidity < 300"
typedef struct {
    int beeps;
    char sensor[32];
    char op[3];
    float threshold;
    bool was_active;
} rule_t;
static rule_t rules[3];
static int rule_count = 0;

// Same beep() idea as in the original buzzer program.
static void beep(int times)
{
    for (int i = 0; i < times; i++) {
        ESP_ERROR_CHECK(ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 512));
        ESP_ERROR_CHECK(ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL));
        vTaskDelay(pdMS_TO_TICKS(200));
        ESP_ERROR_CHECK(ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0));
        ESP_ERROR_CHECK(ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL));
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

// Save a line to events_log.txt. Until we add a real clock, use uptime.
static void save_log(const char *format, ...)
{
    if (!storage_ready) return;
    FILE *file = fopen("/spiffs/events_log.txt", "a");
    if (!file) {
        ESP_LOGW(TAG, "Could not write events_log.txt");
        return;
    }
    fprintf(file, "uptime=%llds: ", (long long)(esp_timer_get_time() / 1000000));
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fprintf(file, "\n");
    fclose(file);
}

// Read the three alarm conditions from configuration.txt.
static void load_rules(void)
{
    if (!storage_ready) return;
    FILE *file = fopen("/spiffs/configuration.txt", "r");
    if (!file) {
        ESP_LOGE(TAG, "Missing configuration.txt! No alarms will be checked.");
        return;
    }
    char line[100];
    while (fgets(line, sizeof(line), file) && rule_count < 3) {
        rule_t rule = {0};
        if (sscanf(line, " %d : %31s %2s %f", &rule.beeps,
                   rule.sensor, rule.op, &rule.threshold) != 4) continue;
        if (rule.beeps < 1 || rule.beeps > 10) continue;
        // This first version supports the comparisons in your assignment: < and >.
        if (strcmp(rule.op, "<") != 0 && strcmp(rule.op, ">") != 0) continue;
        if (strcmp(rule.sensor, "air_humidity") &&
            strcmp(rule.sensor, "air_temperature") &&
            strcmp(rule.sensor, "soil_humidity")) continue;
        rules[rule_count++] = rule;
        ESP_LOGI(TAG, "Loaded rule: %d: %s %s %.1f", rule.beeps,
                 rule.sensor, rule.op, rule.threshold);
    }
    fclose(file);
}

// Send the saved text file over USB serial so the PC can download it.
static void send_file(const char *name)
{
    char path[70];
    snprintf(path, sizeof(path), "/spiffs/%s", name);
    FILE *file = fopen(path, "r");
    if (!file) {
        printf("@@PLANT|MISSING|%s\n", name);
        return;
    }
    printf("@@PLANT|BEGIN|%s\n", name);
    char line[180];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        printf("@@PLANT|LINE|%s|%s\n", name, line);
    }
    fclose(file);
    printf("@@PLANT|END|%s\n", name);
}

// In 'idf.py monitor', type 'logs' + Enter to see all saved log lines.
// tools/export_logs.py also uses this command to save them on your PC.
static void check_serial_command(void)
{
    static char command[16];
    static int length = 0;
    unsigned char c;
    while (uart_read_bytes(UART_NUM_0, &c, 1, 0) == 1) {
        if (c == '\n' || c == '\r') {
            command[length] = '\0';
            if (strcmp(command, "logs") == 0) {
                if (storage_ready) {
                    send_file("events_prev.txt");
                    send_file("events_log.txt");
                } else {
                    printf("@@PLANT|ERROR|SPIFFS not mounted\n");
                }
                printf("@@PLANT|DONE\n");
                fflush(stdout);
            }
            length = 0;
        } else if (length < (int)sizeof(command) - 1) {
            command[length++] = (char)c;
        }
    }
}

static void check_rule(rule_t *rule, float value)
{
    bool active = (strcmp(rule->op, ">") == 0) ?
                  (value > rule->threshold) : (value < rule->threshold);
    ESP_LOGI(TAG, "Rule %d (%s %s %.1f): %s", rule->beeps,
             rule->sensor, rule->op, rule->threshold, active ? "ALARM" : "OK");
    // Beep only when the condition becomes true, not every 5 seconds.
    if (active && !rule->was_active) {
        ESP_LOGW(TAG, "BUZZER: %d beep(s)", rule->beeps);
        save_log("buzzer %d", rule->beeps);
        beep(rule->beeps);
    }
    rule->was_active = active;
}

void app_main(void)
{
    // Storage for the simple configuration.txt and events_log.txt files.
    esp_vfs_spiffs_conf_t fs = {
        .base_path = "/spiffs", .partition_label = "storage",
        .max_files = 3, .format_if_mount_failed = false
    };
    storage_ready = (esp_vfs_spiffs_register(&fs) == ESP_OK);
    if (!storage_ready) ESP_LOGE(TAG, "SPIFFS not ready: check README first-time setup");
    load_rules();

    // Original buzzer configuration.
    ledc_timer_config_t timer = {
        .speed_mode = BUZZER_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = 800, .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t channel = {
        .gpio_num = BUZZER_GPIO, .speed_mode = BUZZER_MODE,
        .channel = BUZZER_CHANNEL, .timer_sel = LEDC_TIMER_0,
        .duty = 0, .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));

    // Original ADC configuration.
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &adc));
    adc_oneshot_chan_cfg_t adc_channel = {
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, MOISTURE_CHANNEL, &adc_channel));

    // Read commands from the serial monitor (same USB cable).
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(gpio_set_pull_mode(DHT_GPIO, GPIO_PULLUP_ONLY));
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "Started. Monitoring sensors every 5 seconds.");
    ESP_LOGI(TAG, "To download saved logs: python tools/export_logs.py /dev/ttyUSB0");

    while (1) {
        float humidity = 0, temperature = 0;
        int raw = 0;
        esp_err_t dht_result = dht_read_float_data(
            DHT_TYPE_DHT11, DHT_GPIO, &humidity, &temperature);
        esp_err_t soil_result = adc_oneshot_read(adc, MOISTURE_CHANNEL, &raw);

        if (dht_result == ESP_OK) {
            ESP_LOGI(TAG, "AIR: temperature=%.1f C, humidity=%.1f %%", temperature, humidity);
            save_log("air_temperature = %.1f", temperature);
            save_log("air_humidity = %.1f", humidity);
        } else {
            ESP_LOGW(TAG, "DHT11 FAILED: %s", esp_err_to_name(dht_result));
        }

        if (soil_result == ESP_OK) {
            // Raw 0..4095 -> assignment scale 0..1000 (dry=0, wet=1000).
            float soil = 1000.0f * (SOIL_RAW_DRY - raw) /
                         (float)(SOIL_RAW_DRY - SOIL_RAW_WET);
            if (soil < 0) soil = 0;
            if (soil > 1000) soil = 1000;
            ESP_LOGI(TAG, "SOIL: raw=%d / 4095, moisture=%.0f / 1000", raw, soil);
            save_log("soil_humidity = %.0f (raw=%d)", soil, raw);

            for (int i = 0; i < rule_count; i++)
                if (strcmp(rules[i].sensor, "soil_humidity") == 0)
                    check_rule(&rules[i], soil);
        } else {
            ESP_LOGW(TAG, "SOIL FAILED: %s", esp_err_to_name(soil_result));
        }

        if (dht_result == ESP_OK) {
            for (int i = 0; i < rule_count; i++) {
                if (strcmp(rules[i].sensor, "air_humidity") == 0)
                    check_rule(&rules[i], humidity);
                if (strcmp(rules[i].sensor, "air_temperature") == 0)
                    check_rule(&rules[i], temperature);
            }
        }
        check_serial_command();
        vTaskDelay(pdMS_TO_TICKS(READ_INTERVAL_MS));
    }
}
