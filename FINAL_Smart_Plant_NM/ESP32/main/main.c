/*
 * Smart Plant Monitoring System - ESP32 baseline firmware
 *
 * Integrates the three previously tested ESP-IDF modules (passive buzzer,
 * soil-moisture ADC, DHT11) into one application, together with the
 * configuration.txt alarm rules and events_log.txt logging introduced in the
 * first integrated version (see reference/integrated_v0_NM_main.c).
 *
 * Baseline scope - intentionally simple so it can serve as the reference
 * point for the energy experiments:
 *   - one app_main() and one main loop, no additional FreeRTOS tasks;
 *   - fixed sampling intervals, configurable independently per sensor;
 *   - soil moisture kept as RAW ADC counts (0..4095) until calibrated;
 *   - no sleep modes and no adaptive sampling (planned experiments).
 *
 * Hardware (classic ESP32):
 *   soil moisture sensor  -> GPIO34 (ADC1 channel 6)
 *   DHT11 data            -> GPIO26
 *   passive buzzer        -> GPIO27 (LEDC PWM, 800 Hz)
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>

#include "dht.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ---------------- Pins and peripherals ---------------- */
#define SOIL_ADC_UNIT       ADC_UNIT_1
#define SOIL_ADC_CHANNEL    ADC_CHANNEL_6        /* GPIO34 on classic ESP32 */
#define DHT_GPIO            GPIO_NUM_26
#define BUZZER_GPIO         GPIO_NUM_27
#define BUZZER_MODE         LEDC_LOW_SPEED_MODE
#define BUZZER_TIMER        LEDC_TIMER_0
#define BUZZER_CHANNEL      LEDC_CHANNEL_0
#define BUZZER_FREQ_HZ      800
#define BUZZER_DUTY_ON      512                  /* 50 % of the 10-bit range */

/* ---------------- Timing (milliseconds) ----------------
 * Sampling intervals are taken from the individual modules. They are kept
 * separate because the sensors have different constraints: the DHT11 must not
 * be polled faster than about once per second, while the ADC has no such limit.
 * These values are the "fixed sampling" reference for Experiment 2. */
#define SOIL_INTERVAL_MS    1000
#define DHT_INTERVAL_MS     2500
#define DHT_STARTUP_MS      2000                 /* DHT11 settling after power-up */
#define BEEP_ON_MS          200
#define BEEP_OFF_MS         250
#define BEEP_GROUP_GAP_MS   600                  /* keeps "1 beep" and "2 beeps" distinguishable
                                                    when two rules trigger together */

/* ---------------- Storage ---------------- */
#define SPIFFS_BASE         "/spiffs"
#define SPIFFS_LABEL        "storage"
#define CONFIG_PATH         SPIFFS_BASE "/configuration.txt"
#define LOG_PATH            SPIFFS_BASE "/events_log.txt"
#define LOG_PREV_PATH       SPIFFS_BASE "/events_prev.txt"
/* The SPIFFS partition is 1 MiB. Rotating at 256 KiB bounds the total log
 * size to about 512 KiB (current + previous file), leaving margin for SPIFFS
 * overhead. At 1 s soil sampling this holds only a few hours of data. */
#define LOG_ROTATE_BYTES    (256 * 1024)
#define MAX_RULES           8

static const char *TAG = "plant";

/* ---------------- Alarm rules (configuration.txt) ---------------- */
typedef enum {
    SENSOR_AIR_HUMIDITY,
    SENSOR_AIR_TEMPERATURE,
    SENSOR_SOIL_HUMIDITY,
    SENSOR_COUNT
} sensor_id_t;

/* Names used both in configuration.txt and in events_log.txt. */
static const char *const SENSOR_NAMES[SENSOR_COUNT] = {
    "air_humidity", "air_temperature", "soil_humidity"
};

typedef struct {
    int beeps;          /* number of buzzer signals (1..10) */
    sensor_id_t sensor;
    char op;            /* '<' or '>' - the comparisons used in the assignment */
    float threshold;
    bool active;        /* condition was true at the last VALID reading */
} rule_t;

static rule_t s_rules[MAX_RULES];
static int s_rule_count = 0;

static adc_oneshot_unit_handle_t s_adc;
static bool s_storage_ready = false;

/* ======================================================================
 * Storage and event log
 * ====================================================================== */

static bool storage_init(void)
{
    const esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE,
        .partition_label = SPIFFS_LABEL,
        .max_files = 3,
        /* Never format automatically: a mount error must not silently erase
         * configuration.txt and previously collected logs. */
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed (%s); was the storage partition flashed?",
                 esp_err_to_name(err));
        return false;
    }
    return true;
}

/* Appends one line to events_log.txt.
 * No wall-clock time source exists yet (no Wi-Fi/NTP, no RTC), so the line is
 * prefixed with the uptime instead of pretending to be a calendar date. */
static void log_event(const char *format, ...)
{
    if (!s_storage_ready) return;

    struct stat info;
    if (stat(LOG_PATH, &info) == 0 && info.st_size >= LOG_ROTATE_BYTES) {
        remove(LOG_PREV_PATH);
        if (rename(LOG_PATH, LOG_PREV_PATH) != 0) {
            ESP_LOGW(TAG, "Log rotation failed; appending anyway");
        }
    }

    FILE *file = fopen(LOG_PATH, "a");
    if (file == NULL) {
        ESP_LOGW(TAG, "Could not open %s", LOG_PATH);
        return;
    }
    fprintf(file, "uptime=%llds: ", (long long)(esp_timer_get_time() / 1000000));
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fputc('\n', file);
    fclose(file);
}

/* Parses lines of the form "<beeps>: <sensor> <op> <threshold>".
 * Blank lines and lines starting with '#' are ignored; any other line that
 * does not match is reported, so configuration mistakes are visible. */
static void load_rules(void)
{
    FILE *file = fopen(CONFIG_PATH, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "%s not found: no alarms will be checked", CONFIG_PATH);
        return;
    }

    char line[128];
    int line_no = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        line_no++;
        line[strcspn(line, "\r\n")] = '\0';
        const char *p = line + strspn(line, " \t");
        if (*p == '\0' || *p == '#') continue;

        int beeps;
        char name[32];
        char op[3];
        float threshold;
        if (sscanf(p, "%d : %31s %2s %f", &beeps, name, op, &threshold) != 4) {
            ESP_LOGW(TAG, "config line %d ignored (format): %s", line_no, line);
            continue;
        }

        int sensor = -1;
        for (int s = 0; s < SENSOR_COUNT; s++) {
            if (strcmp(name, SENSOR_NAMES[s]) == 0) sensor = s;
        }
        bool op_ok = (strcmp(op, "<") == 0 || strcmp(op, ">") == 0);
        if (sensor < 0 || !op_ok || beeps < 1 || beeps > 10) {
            ESP_LOGW(TAG, "config line %d ignored (sensor/operator/beeps): %s",
                     line_no, line);
            continue;
        }
        if (s_rule_count >= MAX_RULES) {
            ESP_LOGW(TAG, "more than %d rules; line %d ignored", MAX_RULES, line_no);
            continue;
        }

        s_rules[s_rule_count++] = (rule_t){
            .beeps = beeps, .sensor = (sensor_id_t)sensor,
            .op = op[0], .threshold = threshold, .active = false,
        };
        ESP_LOGI(TAG, "rule loaded: %d: %s %c %.1f", beeps, name, op[0], threshold);
    }
    fclose(file);
}

/* ======================================================================
 * Buzzer (module 1)
 * ====================================================================== */

static void buzzer_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = BUZZER_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BUZZER_TIMER,
        .freq_hz = BUZZER_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    const ledc_channel_config_t channel = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = BUZZER_MODE,
        .channel = BUZZER_CHANNEL,
        .timer_sel = BUZZER_TIMER,
        .duty = 0,                       /* silent until an alarm */
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

/* Blocking: the main loop is paused for times * (ON + OFF) ms. Acceptable for
 * the baseline because alarms are rare (only on transitions, see below). */
static void beep(int times)
{
    for (int i = 0; i < times; i++) {
        ESP_ERROR_CHECK(ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, BUZZER_DUTY_ON));
        ESP_ERROR_CHECK(ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL));
        vTaskDelay(pdMS_TO_TICKS(BEEP_ON_MS));
        ESP_ERROR_CHECK(ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0));
        ESP_ERROR_CHECK(ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL));
        vTaskDelay(pdMS_TO_TICKS(BEEP_OFF_MS));
    }
    vTaskDelay(pdMS_TO_TICKS(BEEP_GROUP_GAP_MS));
}

/* Evaluates every rule for one sensor after a VALID reading.
 * The buzzer sounds only when a condition changes from false to true, so a
 * persistent problem does not beep at every sample. Rules are not evaluated
 * after a failed read, so a sensor failure cannot clear or re-arm an alarm. */
static void evaluate_rules(sensor_id_t sensor, float value)
{
    for (int i = 0; i < s_rule_count; i++) {
        rule_t *rule = &s_rules[i];
        if (rule->sensor != sensor) continue;

        bool now_active = (rule->op == '>') ? (value > rule->threshold)
                                            : (value < rule->threshold);
        if (now_active && !rule->active) {
            ESP_LOGW(TAG, "ALARM %s %c %.1f (value %.1f) -> buzzer %d",
                     SENSOR_NAMES[sensor], rule->op, rule->threshold, value, rule->beeps);
            log_event("buzzer %d", rule->beeps);
            beep(rule->beeps);
        } else if (!now_active && rule->active) {
            ESP_LOGI(TAG, "cleared %s %c %.1f", SENSOR_NAMES[sensor], rule->op,
                     rule->threshold);
        }
        rule->active = now_active;
    }
}

/* ======================================================================
 * Soil moisture (module 2)
 * ====================================================================== */

static void soil_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit = { .unit_id = SOIL_ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &s_adc));

    const adc_oneshot_chan_cfg_t channel = {
        .atten = ADC_ATTEN_DB_12,        /* full ~0..3.1 V input range */
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, SOIL_ADC_CHANNEL, &channel));
}

/* The value is logged and compared as RAW ADC counts. The assignment's
 * 0..1000 moisture scale requires a dry/wet calibration of this specific
 * sensor (Experiment 1); until then no conversion is applied, so thresholds
 * for soil_humidity in configuration.txt are also in raw counts. */
static void sample_soil(void)
{
    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc, SOIL_ADC_CHANNEL, &raw);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "soil ADC read failed: %s", esp_err_to_name(err));
        log_event("soil_humidity read failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "soil raw=%d / 4095", raw);
    log_event("soil_humidity = %d (raw ADC)", raw);
    evaluate_rules(SENSOR_SOIL_HUMIDITY, (float)raw);
}

/* ======================================================================
 * DHT11 temperature and humidity (module 3)
 * ====================================================================== */

static void dht_init(void)
{
    /* Internal pull-up kept from the first integrated version; it is harmless
     * in parallel with an external module pull-up (to be confirmed). */
    ESP_ERROR_CHECK(gpio_set_pull_mode(DHT_GPIO, GPIO_PULLUP_ONLY));
}

static void sample_dht(void)
{
    float humidity = 0.0f, temperature = 0.0f;
    esp_err_t err = dht_read_float_data(DHT_TYPE_DHT11, DHT_GPIO, &humidity, &temperature);
    if (err != ESP_OK) {
        /* Failures are logged because the DHT11 failure rate is itself a
         * metric of interest for the sampling experiments. */
        ESP_LOGW(TAG, "DHT11 read failed: %s", esp_err_to_name(err));
        log_event("dht11 read failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "temperature=%.1f C humidity=%.1f %%RH", temperature, humidity);
    log_event("air_temperature = %.1f", temperature);
    log_event("air_humidity = %.1f", humidity);
    evaluate_rules(SENSOR_AIR_TEMPERATURE, temperature);
    evaluate_rules(SENSOR_AIR_HUMIDITY, humidity);
}

/* ======================================================================
 * Serial log export (kept from the first integrated version)
 * Typing "logs" + Enter in `idf.py monitor` dumps both log files, each line
 * prefixed with "@@PLANT|" so a PC-side script can extract them.
 * ====================================================================== */

static void send_file(const char *name)
{
    char path[48];
    snprintf(path, sizeof(path), SPIFFS_BASE "/%s", name);
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        printf("@@PLANT|MISSING|%s\n", name);
        return;
    }
    printf("@@PLANT|BEGIN|%s\n", name);
    char line[180];
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        printf("@@PLANT|LINE|%s|%s\n", name, line);
    }
    fclose(file);
    printf("@@PLANT|END|%s\n", name);
}

/* Non-blocking: consumes whatever bytes are already in the UART RX buffer. */
static void check_serial_command(void)
{
    static char command[16];
    static int length = 0;
    uint8_t c;
    while (uart_read_bytes(UART_NUM_0, &c, 1, 0) == 1) {
        if (c == '\n' || c == '\r') {
            command[length] = '\0';
            if (strcmp(command, "logs") == 0) {
                if (s_storage_ready) {
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

/* ======================================================================
 * Main
 * ====================================================================== */

void app_main(void)
{
    /* Every peripheral is initialised exactly once, here. */
    s_storage_ready = storage_init();
    if (s_storage_ready) load_rules();
    buzzer_init();
    soil_init();
    dht_init();
    /* RX buffer for the "logs" command; the console itself keeps using UART0. */
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0));

    vTaskDelay(pdMS_TO_TICKS(DHT_STARTUP_MS));
    ESP_LOGI(TAG, "started: soil every %d ms, DHT11 every %d ms, %d rule(s)",
             SOIL_INTERVAL_MS, DHT_INTERVAL_MS, s_rule_count);
    log_event("system started, %d rule(s) loaded", s_rule_count);

    /* Simple deadline scheduler: each sensor has its own next-sample time,
     * and the task blocks until the earliest one. Both sample immediately. */
    int64_t next_soil_us = 0;
    int64_t next_dht_us = 0;

    while (true) {
        int64_t now_us = esp_timer_get_time();
        if (now_us >= next_soil_us) {
            sample_soil();
            next_soil_us = now_us + (int64_t)SOIL_INTERVAL_MS * 1000;
        }
        if (now_us >= next_dht_us) {
            sample_dht();
            next_dht_us = now_us + (int64_t)DHT_INTERVAL_MS * 1000;
        }
        check_serial_command();

        int64_t next_us = (next_soil_us < next_dht_us) ? next_soil_us : next_dht_us;
        int64_t wait_ms = (next_us - esp_timer_get_time()) / 1000;
        TickType_t ticks = pdMS_TO_TICKS(wait_ms > 0 ? wait_ms : 0);
        vTaskDelay(ticks > 0 ? ticks : 1);   /* always yield at least one tick */
    }
}
