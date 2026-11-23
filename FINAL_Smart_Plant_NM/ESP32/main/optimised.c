#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"

/*PI wakeup pin*/
#define WAKE_PIN GPIO_NUM_25 // wired to Pi pin 5 (GPIO3)
#define PI_UART UART_NUM_2
#define PI_TX_PIN GPIO_NUM_17 // TX2 -> Pi pin 10 (RX)
#define PI_RX_PIN GPIO_NUM_16 // RX2 <- Pi pin 8 (TX)
#define PI_READ_TIMEOUT_MS 2000

/*DHT 11*/
#include "dht.h"
#define DHT_GPIO GPIO_NUM_26
/* Build with -DPLANT_VARIANT=light or -DPLANT_VARIANT=deep. */
#define SLEEP_INTERVAL_MS 10000
#define LOG_ACCESS_WINDOW_MS 15000
#ifndef PLANT_DEEP_SLEEP
#define PLANT_DEEP_SLEEP 0
#endif
#if PLANT_DEEP_SLEEP
#define SLEEP_MODE_NAME "deep"
#else
#define SLEEP_MODE_NAME "light"
#endif
/* Deep sleep restarts app_main; RTC memory preserves the cycle number. */
RTC_DATA_ATTR static unsigned long cycle_number;

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

/* Logs live in the ESP32's built-in flash, using the SPIFFS filesystem. */
#define SPIFFS_BASE "/spiffs"
#define LOG_PATH SPIFFS_BASE "/events_log.txt"
#define LOG_PREV_PATH SPIFFS_BASE "/events_prev.txt"
#define LOG_ROTATE_BYTES (256 * 1024)
static const char *TAGL = "plant_log";
static bool storage_ready;

static void storage_init(void)
{
    const esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE,
        .partition_label = "storage",
        .max_files = 3,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    storage_ready = (err == ESP_OK);
    if (!storage_ready) {
        ESP_LOGE(TAGL, "SPIFFS mount failed (%s); initialise storage with storage-flash; console logging continues",
                 esp_err_to_name(err));
    }
}

static void log_event(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void log_event(const char *format, ...)
{
    if (!storage_ready) return;

    struct stat info;
    if (stat(LOG_PATH, &info) == 0 && info.st_size >= LOG_ROTATE_BYTES) {
        if (remove(LOG_PREV_PATH) != 0 && errno != ENOENT) {
            ESP_LOGE(TAGL, "Could not remove previous log; skipping write");
            return;
        }
        if (rename(LOG_PATH, LOG_PREV_PATH) != 0) {
            ESP_LOGE(TAGL, "Log rotation failed; skipping write to bound storage use");
            return;
        }
    }

    FILE *file = fopen(LOG_PATH, "a");
    if (file == NULL) {
        ESP_LOGE(TAGL, "Could not open %s", LOG_PATH);
        return;
    }
    /* uptime restarts after deep sleep; mode/cycle identify each record. */
    bool failed = fprintf(file, "uptime=%llds: mode=%s cycle=%lu: ",
                          (long long)(esp_timer_get_time() / 1000000),
                          SLEEP_MODE_NAME, cycle_number) < 0;
    va_list args;
    va_start(args, format);
    if (vfprintf(file, format, args) < 0) failed = true;
    va_end(args);
    if (fputc('\n', file) == EOF) failed = true;
    if (fclose(file) != 0) failed = true;
    if (failed) ESP_LOGE(TAGL, "Log write failed (storage may be full)");
}

/* Basic lab exchange: send one greeting, then receive for up to two seconds.
 * No READY/ACK handshake or retry loop; sensing continues if the Pi is absent. */
static void pi_uart_init(void)
{
    const uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(PI_UART, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(PI_UART, &config));
    ESP_ERROR_CHECK(uart_set_pin(PI_UART, PI_TX_PIN, PI_RX_PIN,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void pi_exchange(void)
{
    static const char hello[] = "Hello from ESP32\n";
    char buf[128];
    int sent = uart_write_bytes(PI_UART, hello, sizeof(hello) - 1);
    if (sent != (int)sizeof(hello) - 1) {
        ESP_LOGW("pi_uart", "Send failed (%d bytes)", sent);
        log_event("Pi UART send failed (%d bytes)", sent);
        return;
    }
    ESP_LOGI("pi_uart", "sent: Hello from ESP32");
    log_event("Pi UART sent: Hello from ESP32");

    int len = uart_read_bytes(PI_UART, buf, sizeof(buf) - 1,
                             pdMS_TO_TICKS(PI_READ_TIMEOUT_MS));
    if (len > 0) {
        /* Keep replies on one printable log line, including CR/LF replies. */
        for (int i = 0; i < len; ++i) {
            if ((unsigned char)buf[i] < 32 || (unsigned char)buf[i] > 126) buf[i] = ' ';
        }
        buf[len] = '\0';
        ESP_LOGI("pi_uart", "received: %s", buf);
        log_event("Pi UART received: %s", buf);
    } else if (len == 0) {
        ESP_LOGI("pi_uart", "No data received in %d ms", PI_READ_TIMEOUT_MS);
        log_event("Pi UART: no data received in %d ms", PI_READ_TIMEOUT_MS);
    } else {
        ESP_LOGW("pi_uart", "Read failed");
        log_event("Pi UART read failed");
    }
}

/* Optional: display saved logs in the USB serial monitor. */
static void print_log(const char *path)
{
    char line[180];
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        if (errno != ENOENT) ESP_LOGE(TAGL, "Could not read %s", path);
        return;
    }
    printf("--- %s ---\n", path);
    unsigned lines = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        fputs(line, stdout);
        /* Let the idle task feed the watchdog while exporting large files. */
        if (++lines % 16 == 0) vTaskDelay(1);
    }
    if (ferror(file)) ESP_LOGE(TAGL, "Log read failed");
    fclose(file);
}

static void check_log_command(void)
{
    static char command[16];
    static size_t length;
    static bool overflow;
    uint8_t c;
    while (uart_read_bytes(UART_NUM_0, &c, 1, 0) == 1) {
        if (c == '\r' || c == '\n') {
            command[length] = '\0';
            if (overflow) {
                puts("Command too long; type logs and press Enter.");
            } else if (strcmp(command, "logs") == 0) {
                if (storage_ready) {
                    print_log(LOG_PREV_PATH);
                    print_log(LOG_PATH);
                    puts("--- End of saved logs ---");
                } else {
                    puts("ESP32 log storage is not mounted.");
                }
            } else if (length) {
                puts("Type logs and press Enter to view saved logs.");
            }
            fflush(stdout);
            length = 0;
            overflow = false;
        } else if (length < sizeof(command) - 1) {
            command[length++] = (char)c;
        } else {
            overflow = true;
        }
    }
}

static void beep(int on_ms, int off_ms, int times)
{
    log_event("buzzer %d (on=%d ms, off=%d ms)", times, on_ms, off_ms);
    for (int i = 0; i < times; ++i) {
        ESP_ERROR_CHECK(ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 512));
        ESP_ERROR_CHECK(ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL));
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        ESP_ERROR_CHECK(ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0));
        ESP_ERROR_CHECK(ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL));
        if (off_ms > 0) vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

/* Keep a simple USB download opportunity after EN/power-on, never on timer wakes. */
static void log_access_window(void)
{
    puts("Log access: type logs now (15 seconds), then measurement starts.");
    fflush(stdout);
    int64_t deadline = esp_timer_get_time() + LOG_ACCESS_WINDOW_MS * 1000LL;
    while (esp_timer_get_time() < deadline) {
        check_log_command();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* All file writes are closed before this point. Wake on the timer only. */
static void sleep_between_samples(void)
{
    log_event("entering %s sleep for %d ms", SLEEP_MODE_NAME, SLEEP_INTERVAL_MS);
    ESP_LOGI("plant_sleep", "Entering %s sleep for %d ms (cycle %lu)",
             SLEEP_MODE_NAME, SLEEP_INTERVAL_MS, cycle_number);
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_MS * 1000ULL));
    fflush(stdout);
    ESP_ERROR_CHECK(uart_wait_tx_done(PI_UART, pdMS_TO_TICKS(1000)));
    ESP_ERROR_CHECK(uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(1000)));

    /* Keep the Pi wake line released and the buzzer silent during sleep. */
    ESP_ERROR_CHECK(gpio_hold_en(WAKE_PIN));
    ESP_ERROR_CHECK(gpio_hold_en(BUZZER_GPIO));
#if PLANT_DEEP_SLEEP
    gpio_deep_sleep_hold_en();
    esp_deep_sleep_start();  /* Next timer wake starts app_main again. */
#else
    esp_err_t result = esp_light_sleep_start();
    ESP_ERROR_CHECK(gpio_hold_dis(WAKE_PIN));
    ESP_ERROR_CHECK(gpio_hold_dis(BUZZER_GPIO));
    if (result != ESP_OK) {
        ESP_LOGE("plant_sleep", "Light sleep failed: %s", esp_err_to_name(result));
        log_event("light sleep failed: %s", esp_err_to_name(result));
        /* Avoid a rapid sampling loop if sleep is rejected. */
        vTaskDelay(pdMS_TO_TICKS(SLEEP_INTERVAL_MS));
    } else {
        log_event("woke from light sleep");
    }
#endif
}

void app_main(void)
{
    const bool deep_wake = esp_reset_reason() == ESP_RST_DEEPSLEEP;
    if (!deep_wake) cycle_number = 0;
    storage_init();
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0));
    if (deep_wake) {
        log_event("woke from deep sleep");
    } else {
        log_event("system started (optimised %s), sleep = %d ms",
                  SLEEP_MODE_NAME, SLEEP_INTERVAL_MS);
        log_access_window();
    }
    /*Pi wake part*/
    // Open-drain output: 1 = floating (released), 0 = pulled to ground
    ESP_ERROR_CHECK(gpio_set_level(WAKE_PIN, 1));
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << WAKE_PIN,
        .mode = GPIO_MODE_OUTPUT_OD,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    /* Restore the released output before removing a previous deep-sleep hold. */
    ESP_ERROR_CHECK(gpio_hold_dis(WAKE_PIN));
    if (!deep_wake) {
        ESP_ERROR_CHECK(gpio_set_level(WAKE_PIN, 0)); // press
        vTaskDelay(pdMS_TO_TICKS(250));
        ESP_ERROR_CHECK(gpio_set_level(WAKE_PIN, 1)); // release
        log_event("Pi wake pulse sent (GPIO25)");
    }
    pi_uart_init();
    if (!deep_wake) {
        log_event("Pi UART enabled, 115200 baud, receive timeout = %d ms", PI_READ_TIMEOUT_MS);
    }
#if !PLANT_DEEP_SLEEP
    /* GPIO17 (UART TX idle high) uses VDD_SDIO on this ESP32. */
    ESP_ERROR_CHECK(esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_ON));
    ESP_ERROR_CHECK(gpio_sleep_sel_dis(PI_TX_PIN));
#endif

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
    ESP_ERROR_CHECK(gpio_hold_dis(BUZZER_GPIO));
    gpio_deep_sleep_hold_dis();
    if (!deep_wake) {
        ESP_LOGI(TAG, "Three short beeps");
        beep(BEEP_ON_MS, BEEP_OFF_MS, 3);
        ESP_LOGI(TAG, "One long beep");
        beep(5000, 0, 1);
        ESP_LOGI(TAG, "Finished; press EN to repeat");
    }

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
        ++cycle_number;
        ESP_LOGI("plant_sleep", "Sampling: mode=%s cycle=%lu", SLEEP_MODE_NAME, cycle_number);
        int raw = 0;
        esp_err_t result = adc_oneshot_read(adc, MOISTURE_CHANNEL, &raw);

        if (result == ESP_OK) {
            float moisture = 100.0f *
                (MOISTURE_DRY - raw) /
                (MOISTURE_DRY - MOISTURE_WET);

            if (moisture > 100.0f) moisture = 100.0f;
            if (moisture < 0.0f) moisture = 0.0f;

            ESP_LOGI(TAGM, "Raw=%d Moisture=%.1f%%", raw, moisture);
            log_event("soil_humidity = %d (raw ADC), moisture = %.1f %%", raw, moisture);
        } else {
            ESP_LOGW(TAGM, "Read failed: %s; no new measurement", esp_err_to_name(result));
            log_event("soil_humidity read failed: %s", esp_err_to_name(result));
        }

        float humidity, temperature;
        esp_err_t resultD = dht_read_float_data(DHT_TYPE_DHT11, DHT_GPIO,
            &humidity, &temperature);

        if (resultD == ESP_OK) {
            ESP_LOGI(TAGD, "temperature=%.1f C humidity=%.1f %%RH", temperature,
            humidity);
            log_event("air_temperature = %.1f", temperature);
            log_event("air_humidity = %.1f", humidity);
        } else {
            ESP_LOGW(TAGD, "Read failed: %s; no new measurement; retrying",
                esp_err_to_name(resultD));
            log_event("dht11 read failed: %s", esp_err_to_name(resultD));
        }
        pi_exchange();
        check_log_command();
        sleep_between_samples();
    }
}
