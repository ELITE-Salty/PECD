/*
 * Smart Plant Monitoring System - ESP32 sensor controller
 * ESP-IDF >= 5.3, classic ESP32 (GPIO34 = ADC1 channel 6)
 *
 * Reads the assignment's threshold rules from SPIFFS, measures a DHT11 and
 * analog moisture probe, logs every measurement, and beeps once on each
 * transition into an alarm state. Uses light sleep between samples.
 *
 * Correct soil calibration values before relying on alarms!
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "dht.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DHT_GPIO               GPIO_NUM_26
#define BUZZER_GPIO            GPIO_NUM_27
#define MOISTURE_CHANNEL       ADC_CHANNEL_6    /* GPIO34 on classic ESP32 */

/* IMPORTANT: these are example ADC calibration values, NOT universal. */
#define SOIL_RAW_DRY           3200
#define SOIL_RAW_WET           1300

#define BUZZER_MODE            LEDC_LOW_SPEED_MODE
#define BUZZER_CHANNEL         LEDC_CHANNEL_0
#define BUZZER_FREQ_HZ         800
#define BEEP_ON_MS             180
#define BEEP_OFF_MS            200

#define SAMPLE_FAST_SEC        60
#define SAMPLE_STABLE_SEC      300
#define STABLE_COUNT_TO_SLOW   3
#define LOG_ROTATE_BYTES       (128 * 1024)
#define MAX_RULES              12

#define CONFIG_PATH            "/spiffs/configuration.txt"
#define LOG_PATH               "/spiffs/events_log.txt"
#define OLD_LOG_PATH           "/spiffs/events_prev.txt"

static const char *TAG = "smart_plant";
static adc_oneshot_unit_handle_t s_adc;
static bool s_fs_ok = false;

typedef enum {
    SENSOR_AIR_HUMIDITY,
    SENSOR_AIR_TEMPERATURE,
    SENSOR_SOIL_HUMIDITY
} sensor_t;

typedef enum {
    COMP_GT, COMP_LT, COMP_GE, COMP_LE, COMP_EQ, COMP_NE
} comparison_t;

typedef struct {
    int beeps;
    sensor_t sensor;
    comparison_t comparison;
    char sensor_name[32];
    char op[3];
    float threshold;
    bool previously_active;
} rule_t;

typedef struct {
    bool air_valid;
    bool soil_valid;
    float air_humidity;
    float air_temperature;
    float soil_humidity; /* normalized 0 .. 1000 */
    int soil_raw;
} measurement_t;

static rule_t s_rules[MAX_RULES];
static int s_rule_count = 0;

/* Use a real wall-clock timestamp when set via RTC/NTP. Otherwise explicitly
 * log an uptime stamp; never pretend uptime is a calendar date. */
static void make_timestamp(char *out, size_t capacity)
{
    time_t now = time(NULL);
    if (now >= 1735689600) { /* Jan 1, 2025 UTC */
        struct tm broken_down;
        if (localtime_r(&now, &broken_down) != NULL) {
            strftime(out, capacity, "%a %b %d %H:%M:%S %Z %Y", &broken_down);
            return;
        }
    }
    snprintf(out, capacity, "uptime=%llds",
             (long long)(esp_timer_get_time() / 1000000LL));
}

static void log_event(const char *format, ...)
{
    char detail[160];
    char stamp[64];
    va_list args;
    va_start(args, format);
    vsnprintf(detail, sizeof(detail), format, args);
    va_end(args);
    make_timestamp(stamp, sizeof(stamp));

    ESP_LOGI(TAG, "%s: %s", stamp, detail);
    if (!s_fs_ok) return;

    /* Keep flash usage bounded. Rotating avoids an indefinitely growing log. */
    struct stat info;
    if (stat(LOG_PATH, &info) == 0 && info.st_size >= LOG_ROTATE_BYTES) {
        remove(OLD_LOG_PATH);
        if (rename(LOG_PATH, OLD_LOG_PATH) != 0) {
            ESP_LOGW(TAG, "Cannot rotate log; continuing");
        }
    }

    FILE *file = fopen(LOG_PATH, "a");
    if (file == NULL) {
        ESP_LOGW(TAG, "Cannot open %s for append", LOG_PATH);
        return;
    }
    fprintf(file, "%s: %s\n", stamp, detail);
    fclose(file);
}

static void init_filesystem(void)
{
    esp_vfs_spiffs_conf_t config = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = false /* Preserve existing data on errors. */,
    };
    esp_err_t result = esp_vfs_spiffs_register(&config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(result));
        return;
    }
    s_fs_ok = true;
    size_t total = 0, used = 0;
    if (esp_spiffs_info("storage", &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS used %u / %u bytes", (unsigned)used, (unsigned)total);
    }
}

static bool parse_sensor(const char *name, sensor_t *sensor)
{
    if (strcmp(name, "air_humidity") == 0) *sensor = SENSOR_AIR_HUMIDITY;
    else if (strcmp(name, "air_temperature") == 0) *sensor = SENSOR_AIR_TEMPERATURE;
    else if (strcmp(name, "soil_humidity") == 0) *sensor = SENSOR_SOIL_HUMIDITY;
    else return false;
    return true;
}

static bool parse_comparison(const char *op, comparison_t *comparison)
{
    if (strcmp(op, ">") == 0) *comparison = COMP_GT;
    else if (strcmp(op, "<") == 0) *comparison = COMP_LT;
    else if (strcmp(op, ">=") == 0) *comparison = COMP_GE;
    else if (strcmp(op, "<=") == 0) *comparison = COMP_LE;
    else if (strcmp(op, "==") == 0) *comparison = COMP_EQ;
    else if (strcmp(op, "!=") == 0) *comparison = COMP_NE;
    else return false;
    return true;
}

static void load_rules(void)
{
    if (!s_fs_ok) return;
    FILE *file = fopen(CONFIG_PATH, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Missing %s; alarms will be disabled", CONFIG_PATH);
        return;
    }
    char line[128];
    int line_number = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        ++line_number;
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;
        if (s_rule_count >= MAX_RULES) {
            ESP_LOGW(TAG, "Too many rules; ignoring line %d", line_number);
            continue;
        }

        rule_t parsed = {0};
        char tail = '\0';
        int matched = sscanf(p, " %d : %31s %2s %f %c", &parsed.beeps,
                             parsed.sensor_name, parsed.op, &parsed.threshold, &tail);
        if (matched != 4 || parsed.beeps < 1 || parsed.beeps > 10 ||
            !isfinite(parsed.threshold) ||
            !parse_sensor(parsed.sensor_name, &parsed.sensor) ||
            !parse_comparison(parsed.op, &parsed.comparison)) {
            ESP_LOGW(TAG, "Invalid configuration line %d: %s", line_number, line);
            continue;
        }
        s_rules[s_rule_count++] = parsed;
        ESP_LOGI(TAG, "Loaded rule %d: %s %s %.1f", parsed.beeps,
                 parsed.sensor_name, parsed.op, parsed.threshold);
    }
    fclose(file);
    if (s_rule_count == 0) ESP_LOGW(TAG, "No valid alarm rules loaded");
}

static void init_buzzer(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = BUZZER_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BUZZER_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t channel = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = BUZZER_MODE,
        .channel = BUZZER_CHANNEL,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

static void beep(int times)
{
    for (int i = 0; i < times; ++i) {
        ESP_ERROR_CHECK(ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 512));
        ESP_ERROR_CHECK(ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL));
        vTaskDelay(pdMS_TO_TICKS(BEEP_ON_MS));
        ESP_ERROR_CHECK(ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0));
        ESP_ERROR_CHECK(ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL));
        vTaskDelay(pdMS_TO_TICKS(BEEP_OFF_MS));
    }
}

static void init_adc(void)
{
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &s_adc));
    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, MOISTURE_CHANNEL, &channel_config));
}

static float soil_raw_to_1000(int raw)
{
    /* Many capacitive sensors have a larger ADC reading when dry. Confirm
     * this direction experimentally and set your own wet/dry calibration. */
    float normalized = 1000.0f * (SOIL_RAW_DRY - raw) /
                       (float)(SOIL_RAW_DRY - SOIL_RAW_WET);
    if (normalized < 0.0f) return 0.0f;
    if (normalized > 1000.0f) return 1000.0f;
    return normalized;
}

static measurement_t read_measurements(void)
{
    measurement_t current = {0};
    esp_err_t err = dht_read_float_data(DHT_TYPE_DHT11, DHT_GPIO,
                                        &current.air_humidity, &current.air_temperature);
    if (err == ESP_OK) {
        current.air_valid = true;
        log_event("air_humidity = %.1f", current.air_humidity);
        log_event("air_temperature = %.1f", current.air_temperature);
    } else {
        log_event("DHT11 read failed: %s", esp_err_to_name(err));
    }

    err = adc_oneshot_read(s_adc, MOISTURE_CHANNEL, &current.soil_raw);
    if (err == ESP_OK) {
        current.soil_valid = true;
        current.soil_humidity = soil_raw_to_1000(current.soil_raw);
        log_event("soil_humidity = %.0f (ADC raw = %d)",
                  current.soil_humidity, current.soil_raw);
    } else {
        log_event("Soil ADC read failed: %s", esp_err_to_name(err));
    }
    return current;
}

static bool matches_rule(const rule_t *rule, const measurement_t *current,
                         bool *has_reading)
{
    float value;
    if (rule->sensor == SENSOR_AIR_HUMIDITY) {
        *has_reading = current->air_valid;
        value = current->air_humidity;
    } else if (rule->sensor == SENSOR_AIR_TEMPERATURE) {
        *has_reading = current->air_valid;
        value = current->air_temperature;
    } else {
        *has_reading = current->soil_valid;
        value = current->soil_humidity;
    }
    if (!*has_reading) return false;
    switch (rule->comparison) {
        case COMP_GT: return value > rule->threshold;
        case COMP_LT: return value < rule->threshold;
        case COMP_GE: return value >= rule->threshold;
        case COMP_LE: return value <= rule->threshold;
        case COMP_EQ: return value == rule->threshold;
        case COMP_NE: return value != rule->threshold;
    }
    return false;
}

static bool check_alarms(const measurement_t *current)
{
    bool any_alarm = false;
    for (int i = 0; i < s_rule_count; ++i) {
        rule_t *rule = &s_rules[i];
        bool has_reading = false;
        bool active = matches_rule(rule, current, &has_reading);
        if (!has_reading) continue; /* Don't clear state after a failed read. */
        if (active) {
            any_alarm = true;
            if (!rule->previously_active) {
                log_event("buzzer %d (rule %s %s %.1f)", rule->beeps,
                          rule->sensor_name, rule->op, rule->threshold);
                beep(rule->beeps);
                vTaskDelay(pdMS_TO_TICKS(400));
            }
        } else if (rule->previously_active) {
            log_event("alarm cleared: %s %s %.1f", rule->sensor_name,
                      rule->op, rule->threshold);
        }
        rule->previously_active = active;
    }
    return any_alarm;
}

static bool measurements_stable(const measurement_t *current,
                                const measurement_t *previous)
{
    if (!(current->air_valid && previous->air_valid &&
          current->soil_valid && previous->soil_valid)) return false;
    return fabsf(current->air_temperature - previous->air_temperature) <= 0.5f &&
           fabsf(current->air_humidity - previous->air_humidity) <= 2.0f &&
           fabsf(current->soil_humidity - previous->soil_humidity) <= 20.0f;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Smart Plant ESP32 controller");
    init_filesystem();
    load_rules();
    init_adc();
    init_buzzer();
    ESP_ERROR_CHECK(gpio_set_pull_mode(DHT_GPIO, GPIO_PULLUP_ONLY));
    vTaskDelay(pdMS_TO_TICKS(2000));
    log_event("ESP32 sensor monitoring started; %d rules", s_rule_count);

    measurement_t previous = {0};
    bool have_previous = false;
    int stable_samples = 0;
    while (true) {
        measurement_t current = read_measurements();
        bool any_alarm = check_alarms(&current);

        if (!any_alarm && have_previous &&
            measurements_stable(&current, &previous)) {
            if (stable_samples < STABLE_COUNT_TO_SLOW) ++stable_samples;
        } else {
            stable_samples = 0;
        }
        if (current.air_valid && current.soil_valid) {
            previous = current;
            have_previous = true;
        } else {
            have_previous = false;
        }

        int interval_sec = (!any_alarm && stable_samples >= STABLE_COUNT_TO_SLOW)
                           ? SAMPLE_STABLE_SEC : SAMPLE_FAST_SEC;
        ESP_LOGI(TAG, "Next measurement after %ds (light sleep)", interval_sec);
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)interval_sec * 1000000ULL));
        esp_err_t sleep_result = esp_light_sleep_start();
        if (sleep_result != ESP_OK) {
            ESP_LOGW(TAG, "Light sleep failed: %s", esp_err_to_name(sleep_result));
            vTaskDelay(pdMS_TO_TICKS(interval_sec * 1000));
        }
    }
}
