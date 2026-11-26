/*
 * Smart Plant Monitoring System - final (adaptive) ESP32 firmware
 *
 * Build:  idf.py -B build-adaptive -DPLANT_VARIANT=adaptive build flash
 *
 * This is the version we propose as the final system. It combines:
 *   - the alarm rules (configuration.txt) and events_log.txt of main.c;
 *   - deep sleep between readings (light sleep with -DPLANT_DEEP_SLEEP=0).
 *     Our energy model says deep sleep is ~10 % better than light sleep
 *     once the interval is 60 s or more (break-even ~10 s), see
 *     Tests/energy_measurements/energy_model.py;
 *   - adaptive sensing interval, 60 s .. 900 s (adaptive_sampling.h);
 *   - the Raspberry Pi is woken only for a daily photo or after a
 *     significant change / new alarm (adaptive_sampling.h);
 *   - UART to the Pi is only served while the Pi is actually awake.
 *     optimised.c waited 2 s for the Pi in EVERY cycle, which was 80 % of the
 *     time the ESP32 spent awake (measured: 2.5 s awake per cycle).
 *
 * What changed compared with the measured variants is described in
 * ESP32/README.md. Hardware (classic ESP32 DevKit):
 *   soil moisture sensor -> GPIO34 (ADC1 CH6)
 *   DHT11 data           -> GPIO26
 *   passive buzzer       -> GPIO27 (LEDC PWM, 800 Hz)
 *   Pi wake              -> GPIO25 open-drain -> Pi GPIO3 (pin 5)
 *   Pi UART              -> UART2, TX GPIO17 / RX GPIO16, 115200 baud
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "dht.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "adaptive_sampling.h"

/* ---------------- Pins ---------------- */
#define SOIL_ADC_CHANNEL    ADC_CHANNEL_6        /* GPIO34 */
#define DHT_GPIO            GPIO_NUM_26
#define BUZZER_GPIO         GPIO_NUM_27
#define BUZZER_MODE         LEDC_LOW_SPEED_MODE
#define BUZZER_TIMER        LEDC_TIMER_0
#define BUZZER_CHANNEL      LEDC_CHANNEL_0
#define BUZZER_FREQ_HZ      800
#define BUZZER_DUTY_ON      512
#define PI_WAKE_GPIO        GPIO_NUM_25
#define PI_UART             UART_NUM_2
#define PI_TX_GPIO          GPIO_NUM_17
#define PI_RX_GPIO          GPIO_NUM_16

/* ---------------- Timing ---------------- */
#define BEEP_ON_MS          200
#define BEEP_OFF_MS         250
#define BEEP_GROUP_GAP_MS   600
#define PI_WAKE_PULSE_MS    250
/* The Pi needed 74 s from the wake pulse until it was online (wake test,
 * Results/pi_wake_test_2026-11-19.json), then photo + vision +
 * log transfer. 5 min is a safe upper bound; the session normally ends
 * earlier, as soon as the Pi has fetched the logs. */
#define PI_SESSION_MAX_MS   (5 * 60 * 1000)
#define LOG_ACCESS_WINDOW_MS 15000
#define DHT_STARTUP_MS      2000

/* ---------------- Soil calibration ----------------
 * Two-point calibration of our sensor (Tests/sensor_calibration):
 * probe in air reads 0, saturated soil reads ~2350, sensor saturates
 * around 2560. Higher = wetter. Mapped to the assignment's 0..1000 scale. */
#define SOIL_RAW_DRY        0
#define SOIL_RAW_WET        2560

/* ---------------- Storage ---------------- */
#define SPIFFS_BASE         "/spiffs"
#define CONFIG_PATH         SPIFFS_BASE "/configuration.txt"
#define LOG_PATH            SPIFFS_BASE "/events_log.txt"
#define LOG_PREV_PATH       SPIFFS_BASE "/events_prev.txt"
#define LOG_ROTATE_BYTES    (256 * 1024)
#define MAX_RULES           8

#ifndef PLANT_DEEP_SLEEP
#define PLANT_DEEP_SLEEP 1
#endif

static const char *TAG = "plant";

/* ======================================================================
 * Alarm rules (same format and behaviour as main.c)
 * ====================================================================== */
typedef enum { S_AIR_HUM, S_AIR_TEMP, S_SOIL, S_COUNT } sensor_id_t;
static const char *const SENSOR_NAMES[S_COUNT] = {
    "air_humidity", "air_temperature", "soil_humidity"
};

typedef struct {
    int beeps;
    sensor_id_t sensor;
    char op;
    float threshold;
} rule_t;

static rule_t s_rules[MAX_RULES];
static int s_rule_count;
static bool s_storage_ready;
static adc_oneshot_unit_handle_t s_adc;

/* State that must survive deep sleep lives in RTC slow memory. In deep
 * sleep app_main() restarts on every wake-up, so everything else (rules,
 * drivers, SPIFFS) is simply set up again; that cost is part of the
 * measured deep-sleep wake time. */
RTC_DATA_ATTR static as_state_t s_as;
RTC_DATA_ATTR static bool s_rule_active[MAX_RULES];   /* rules re-load in file order */
RTC_DATA_ATTR static int64_t s_last_photo_us;

/* System time keeps running in light AND deep sleep (RTC timer), unlike
 * esp_timer_get_time(), which restarts after every deep-sleep wake. */
static int64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* ======================================================================
 * Storage and event log
 * ====================================================================== */
static bool storage_init(void)
{
    const esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE,
        .partition_label = "storage",
        .max_files = 3,
        .format_if_mount_failed = false,   /* never erase logs/config silently */
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed (%s)", esp_err_to_name(err));
        return false;
    }
    return true;
}

static void log_event(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* No NTP on the board, so lines carry the time since power-on, which stays
 * correct across light and deep sleep (now_us()). */
static void log_event(const char *format, ...)
{
    if (!s_storage_ready) return;
    struct stat info;
    if (stat(LOG_PATH, &info) == 0 && info.st_size >= LOG_ROTATE_BYTES) {
        remove(LOG_PREV_PATH);
        if (rename(LOG_PATH, LOG_PREV_PATH) != 0) {
            ESP_LOGE(TAG, "log rotation failed; write skipped");
            return;
        }
    }
    FILE *file = fopen(LOG_PATH, "a");
    if (file == NULL) return;
    fprintf(file, "uptime=%llds: ", (long long)(now_us() / 1000000));
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fputc('\n', file);
    fclose(file);
}

static void load_rules(void)
{
    FILE *file = fopen(CONFIG_PATH, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "%s not found: no alarms", CONFIG_PATH);
        return;
    }
    char line[128];
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        const char *p = line + strspn(line, " \t");
        if (*p == '\0' || *p == '#') continue;
        int beeps;
        char name[32], op[3];
        float threshold;
        if (sscanf(p, "%d : %31s %2s %f", &beeps, name, op, &threshold) != 4) continue;
        int sensor = -1;
        for (int s = 0; s < S_COUNT; s++) {
            if (strcmp(name, SENSOR_NAMES[s]) == 0) sensor = s;
        }
        bool op_ok = (strcmp(op, "<") == 0 || strcmp(op, ">") == 0);
        if (sensor < 0 || !op_ok || beeps < 1 || beeps > 10 || s_rule_count >= MAX_RULES) {
            ESP_LOGW(TAG, "config line ignored: %s", line);
            continue;
        }
        s_rules[s_rule_count++] = (rule_t){ beeps, (sensor_id_t)sensor, op[0], threshold };
        ESP_LOGI(TAG, "rule: %d: %s %c %.1f", beeps, name, op[0], threshold);
    }
    fclose(file);
}

/* ======================================================================
 * Buzzer
 * ====================================================================== */
static void buzzer_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = BUZZER_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BUZZER_TIMER, .freq_hz = BUZZER_FREQ_HZ, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    const ledc_channel_config_t channel = {
        .gpio_num = BUZZER_GPIO, .speed_mode = BUZZER_MODE, .channel = BUZZER_CHANNEL,
        .timer_sel = BUZZER_TIMER, .duty = 0, .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

/* Short beeps on purpose: one 5 s tone cost 0.56 J in our baseline
 * measurement, as much as several minutes of sleep. */
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

/* Beeps only when a rule goes from false to true (as in main.c).
 * Returns true if at least one alarm started in this call. */
static bool evaluate_rules(sensor_id_t sensor, float value)
{
    bool new_alarm = false;
    for (int i = 0; i < s_rule_count; i++) {
        rule_t *rule = &s_rules[i];
        if (rule->sensor != sensor) continue;
        bool now_active = (rule->op == '>') ? (value > rule->threshold)
                                            : (value < rule->threshold);
        if (now_active && !s_rule_active[i]) {
            ESP_LOGW(TAG, "ALARM %s %c %.1f (value %.1f)", SENSOR_NAMES[sensor],
                     rule->op, rule->threshold, value);
            log_event("buzzer %d", rule->beeps);
            beep(rule->beeps);
            new_alarm = true;
        }
        s_rule_active[i] = now_active;
    }
    return new_alarm;
}

/* True when a value is within the "near" band of any rule for that sensor:
 * there we keep sampling at the minimum interval. */
static bool near_any_threshold(sensor_id_t sensor, float value)
{
    static const float band[S_COUNT] = { AS_HUM_NEAR, AS_TEMP_NEAR, AS_SOIL_NEAR };
    for (int i = 0; i < s_rule_count; i++) {
        if (s_rules[i].sensor != sensor) continue;
        float d = value - s_rules[i].threshold;
        if (d < 0) d = -d;
        if (d <= band[sensor]) return true;
    }
    return false;
}

/* ======================================================================
 * Sensors
 * ====================================================================== */
static void sensors_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &s_adc));
    const adc_oneshot_chan_cfg_t channel = {
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, SOIL_ADC_CHANNEL, &channel));
    ESP_ERROR_CHECK(gpio_set_pull_mode(DHT_GPIO, GPIO_PULLUP_ONLY));
}

static float soil_scale(int raw)
{
    float v = 1000.0f * (float)(raw - SOIL_RAW_DRY) / (float)(SOIL_RAW_WET - SOIL_RAW_DRY);
    if (v < 0.0f) v = 0.0f;
    if (v > 1000.0f) v = 1000.0f;
    return v;
}

/* One sensing round: reads both sensors, logs, checks the rules.
 * Fills *r and returns whether a new alarm started. */
static bool sense(as_reading_t *r, bool *near)
{
    bool new_alarm = false;
    *near = false;

    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc, SOIL_ADC_CHANNEL, &raw);
    r->soil_ok = (err == ESP_OK);
    if (r->soil_ok) {
        r->soil = soil_scale(raw);
        log_event("soil_humidity = %.0f (raw %d)", r->soil, raw);
        new_alarm |= evaluate_rules(S_SOIL, r->soil);
        *near |= near_any_threshold(S_SOIL, r->soil);
    } else {
        log_event("soil_humidity read failed: %s", esp_err_to_name(err));
    }

    float hum = 0, temp = 0;
    err = dht_read_float_data(DHT_TYPE_DHT11, DHT_GPIO, &hum, &temp);
    r->dht_ok = (err == ESP_OK);
    if (r->dht_ok) {
        r->temp = temp;
        r->hum = hum;
        log_event("air_temperature = %.1f", temp);
        log_event("air_humidity = %.1f", hum);
        new_alarm |= evaluate_rules(S_AIR_TEMP, temp);
        new_alarm |= evaluate_rules(S_AIR_HUM, hum);
        *near |= near_any_threshold(S_AIR_TEMP, temp) || near_any_threshold(S_AIR_HUM, hum);
    } else {
        log_event("dht11 read failed: %s", esp_err_to_name(err));
    }
    return new_alarm;
}

/* ======================================================================
 * Raspberry Pi: wake pulse + UART session (protocol of main.c)
 * ====================================================================== */
static void pi_init(void)
{
    ESP_ERROR_CHECK(gpio_set_level(PI_WAKE_GPIO, 1));     /* 1 = released */
    const gpio_config_t conf = {
        .pin_bit_mask = 1ULL << PI_WAKE_GPIO, .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&conf));
    ESP_ERROR_CHECK(gpio_set_level(PI_WAKE_GPIO, 1));

    const uart_config_t uart = {
        .baud_rate = 115200, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(PI_UART, &uart));
    ESP_ERROR_CHECK(uart_set_pin(PI_UART, PI_TX_GPIO, PI_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(PI_UART, 1024, 0, 0, NULL, 0));
}

static uart_port_t s_reply_uart = UART_NUM_0;

static void serial_reply(const char *format, ...)
{
    char message[256];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (length < 0 || length >= (int)sizeof(message)) return;
    if (s_reply_uart == UART_NUM_0) fputs(message, stdout);
    else uart_write_bytes(s_reply_uart, message, length);
}

static void send_file(const char *name)
{
    char path[48];
    snprintf(path, sizeof(path), SPIFFS_BASE "/%s", name);
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        serial_reply("@@PLANT|MISSING|%s\n", name);
        return;
    }
    serial_reply("@@PLANT|BEGIN|%s\n", name);
    char line[180];
    unsigned n = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        serial_reply("@@PLANT|LINE|%s|%s\n", name, line);
        if (++n % 16 == 0) vTaskDelay(1);      /* keep the watchdog fed */
    }
    fclose(file);
    serial_reply("@@PLANT|END|%s\n", name);
}

/* Handles complete lines waiting on a port. Returns true after "logs":
 * plant_cycle.py fetches the logs as its last step before powering off, so
 * that is the signal that the Pi session is over. */
static bool handle_commands(uart_port_t port, int index)
{
    static char commands[2][32];
    static int lengths[2];
    bool finished = false;
    uint8_t c;
    while (uart_read_bytes(port, &c, 1, 0) == 1) {
        if (c != '\n' && c != '\r') {
            if (lengths[index] < (int)sizeof(commands[index]) - 1) {
                commands[index][lengths[index]++] = (char)c;
            }
            continue;
        }
        char *command = commands[index];
        command[lengths[index]] = '\0';
        lengths[index] = 0;
        if (command[0] == '\0') continue;
        s_reply_uart = port;
        int count = 0;
        if (strncmp(command, "BEEP ", 5) == 0) count = atoi(command + 5);
        if (strcmp(command, "PING") == 0) {
            serial_reply("@@PLANT|READY\n");
        } else if (count >= 1 && count <= 10) {
            log_event("vision alarm: buzzer %d", count);
            beep(count);
            serial_reply("@@PLANT|BEEP|%d|DONE\n", count);
        } else if (strcmp(command, "logs") == 0) {
            if (s_storage_ready) {
                send_file("events_prev.txt");
                send_file("events_log.txt");
            } else {
                serial_reply("@@PLANT|ERROR|SPIFFS not mounted\n");
            }
            serial_reply("@@PLANT|DONE\n");
            finished = true;
        } else if (strcmp(command, "READY") != 0) {
            serial_reply("@@PLANT|ERROR|Unknown command\n");
        }
        fflush(stdout);
    }
    return finished;
}

/* Wakes the Pi and stays awake to answer it. This is the only time the ESP32
 * listens on UART2, so no time is wasted waiting for an absent Pi. */
static void pi_session(const char *reason)
{
    log_event("Pi wake (%s)", reason);
    ESP_LOGI(TAG, "waking Pi (%s)", reason);
    uart_flush_input(PI_UART);
    ESP_ERROR_CHECK(gpio_set_level(PI_WAKE_GPIO, 0));
    vTaskDelay(pdMS_TO_TICKS(PI_WAKE_PULSE_MS));
    ESP_ERROR_CHECK(gpio_set_level(PI_WAKE_GPIO, 1));

    int64_t deadline = esp_timer_get_time() + (int64_t)PI_SESSION_MAX_MS * 1000;
    bool done = false;
    while (!done && esp_timer_get_time() < deadline) {
        done = handle_commands(PI_UART, 1);
        handle_commands(UART_NUM_0, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    log_event("Pi session %s", done ? "finished" : "timed out");
}

/* ======================================================================
 * Sleep
 * ====================================================================== */
/* All files are closed here. Deep sleep never returns: the next timer wake
 * starts app_main() again. Light sleep returns and the loop continues. */
static void sleep_s(uint32_t seconds)
{
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL));
    fflush(stdout);
    uart_wait_tx_done(PI_UART, pdMS_TO_TICKS(1000));
    uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(1000));
    /* Keep the Pi wake line released and the buzzer silent while asleep. */
    gpio_hold_en(PI_WAKE_GPIO);
    gpio_hold_en(BUZZER_GPIO);
#if PLANT_DEEP_SLEEP
    gpio_deep_sleep_hold_en();
    esp_deep_sleep_start();
#else
    esp_err_t result = esp_light_sleep_start();
    gpio_hold_dis(PI_WAKE_GPIO);
    gpio_hold_dis(BUZZER_GPIO);
    if (result != ESP_OK) {
        log_event("light sleep failed: %s", esp_err_to_name(result));
        vTaskDelay(pdMS_TO_TICKS(seconds * 1000));
    }
#endif
}

/* USB log download right after power-on / EN, as in optimised.c. */
static void log_access_window(void)
{
    puts("Log access: type logs now (15 seconds).");
    fflush(stdout);
    int64_t deadline = esp_timer_get_time() + LOG_ACCESS_WINDOW_MS * 1000LL;
    while (esp_timer_get_time() < deadline) {
        handle_commands(UART_NUM_0, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ======================================================================
 * Main
 * ====================================================================== */
/* One wake-up: read, decide about the Pi, choose the next interval. */
static uint32_t one_cycle(void)
{
    as_reading_t r = {0};
    bool near = false;
    bool new_alarm = sense(&r, &near);

    s_as.since_photo_s = (uint32_t)((now_us() - s_last_photo_us) / 1000000);
    bool routine = s_as.since_photo_s >= AS_PI_ROUTINE_S;
    if (as_should_wake_pi(&s_as, new_alarm)) {
        pi_session(routine ? "daily photo" : "new alarm");
        s_last_photo_us = now_us();
    }

    uint32_t interval = as_next_interval(&s_as, &r, near);
    log_event("next reading in %u s%s", interval, near ? " (near threshold)" : "");
    handle_commands(UART_NUM_0, 0);
    return interval;
}

void app_main(void)
{
    const bool deep_wake = esp_reset_reason() == ESP_RST_DEEPSLEEP;

    s_storage_ready = storage_init();
    if (s_storage_ready) load_rules();
    buzzer_init();
    sensors_init();
    pi_init();
    /* Pins are configured again (released / silent), now drop the holds. */
    gpio_hold_dis(PI_WAKE_GPIO);
    gpio_hold_dis(BUZZER_GPIO);
    gpio_deep_sleep_hold_dis();
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0));
#if !PLANT_DEEP_SLEEP
    /* GPIO17 (UART TX idles high) is powered from VDD_SDIO on this ESP32:
     * keep it on in light sleep so the Pi does not see a false start bit. */
    ESP_ERROR_CHECK(esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_ON));
    ESP_ERROR_CHECK(gpio_sleep_sel_dis(PI_TX_GPIO));
#endif

    if (!deep_wake) {
        /* Power-on / EN: fresh state, USB log window, sensor settling. */
        as_init(&s_as);
        memset(s_rule_active, 0, sizeof(s_rule_active));
        /* Take one reference photo at installation, then follow the policy. */
        s_last_photo_us = now_us() - (int64_t)AS_PI_ROUTINE_S * 1000000;
        log_event("system started (adaptive, %s sleep), %d rule(s), interval %u..%u s",
                  PLANT_DEEP_SLEEP ? "deep" : "light", s_rule_count,
                  AS_MIN_INTERVAL_S, AS_MAX_INTERVAL_S);
        log_access_window();
        vTaskDelay(pdMS_TO_TICKS(DHT_STARTUP_MS));
    }

    while (true) {
        sleep_s(one_cycle());          /* deep sleep: does not return */
    }
}
