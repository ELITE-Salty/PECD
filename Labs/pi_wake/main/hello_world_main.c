
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WAKE_PIN GPIO_NUM_25
#define PI_UART UART_NUM_2
#define PI_TX_PIN GPIO_NUM_17
#define PI_RX_PIN GPIO_NUM_16

// Read one complete line from the Pi
static bool read_pi_line(char *buf, size_t size, int timeout_ms)
{
    size_t pos = 0;
    TickType_t start = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start) <
           pdMS_TO_TICKS(timeout_ms)) {

        uint8_t ch;
        int n = uart_read_bytes(
            PI_UART, &ch, 1, pdMS_TO_TICKS(100)
        );

        if (n != 1) continue;

        if (ch == '\n') {
            buf[pos] = '\0';
            return true;
        }

        if (ch == '\r') continue;

        if (pos < size - 1) {
            buf[pos++] = (char)ch;
        }
    }

    buf[pos] = '\0';
    return false;
}

void app_main(void)
{
    // 1. Configure Pi wake pin
    gpio_set_level(WAKE_PIN, 1);

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << WAKE_PIN,
        .mode = GPIO_MODE_OUTPUT_OD,
    };

    ESP_ERROR_CHECK(gpio_config(&config));

    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        gpio_set_level(WAKE_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(250));
        gpio_set_level(WAKE_PIN, 1);
    }

    // 2. Configure UART
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    ESP_ERROR_CHECK(uart_driver_install(
        PI_UART, 1024, 0, 0, NULL, 0
    ));
    ESP_ERROR_CHECK(uart_param_config(
        PI_UART, &uart_config
    ));
    ESP_ERROR_CHECK(uart_set_pin(
        PI_UART,
        PI_TX_PIN,
        PI_RX_PIN,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    ));

    // 3. Wait for Pi to become ready
    bool pi_ready = false;
    char buf[128];

    const char *ping = "PING\n";
    const char *hello = "Hello from ESP32\n";

    while (1) {

        if (!pi_ready) {
            printf("Waiting for Raspberry Pi...\n");

            uart_write_bytes(
                PI_UART, ping, strlen(ping)
            );

            if (read_pi_line(buf, sizeof(buf), 1500)
                && strcmp(buf, "READY") == 0) {

                pi_ready = true;
                printf("Raspberry Pi is READY!\n");
            } else {
                vTaskDelay(pdMS_TO_TICKS(500));
            }

            continue;
        }

        // 4. Pi is ready: send actual data
        uart_write_bytes(
            PI_UART, hello, strlen(hello)
        );

        printf("sent: %s", hello);

        // 5. Confirm Pi received the message
        if (read_pi_line(buf, sizeof(buf), 2000)
            && strcmp(buf, "ACK") == 0) {

            printf("Pi acknowledged message\n");
        } else {
            printf("Pi not responding. Reconnecting...\n");
            pi_ready = false;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
