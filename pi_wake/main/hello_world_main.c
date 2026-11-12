
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define WAKE_PIN GPIO_NUM_25 // to Pi pin 5 (wake)
#define PI_UART UART_NUM_2 // the UART used for the Pi
#define PI_TX_PIN GPIO_NUM_17 // board label TX2, to Pi pin 10
#define PI_RX_PIN GPIO_NUM_16 // board label RX2, to Pi pin 8

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


// 2. Set up UART2: 115200 baud, 8 data bits, no parity, 1 stop bit
uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
};

uart_driver_install(PI_UART, 1024, 0, 0, NULL, 0);
uart_param_config(PI_UART, &uart_config);
uart_set_pin(PI_UART, PI_TX_PIN, PI_RX_PIN,
    UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

// 3. Talk to the Pi forever
const char *hello = "Hello from ESP32\n";
char buf[128];
while (1) {
        // Send one line to the Pi
        uart_write_bytes(PI_UART, hello, strlen(hello));
        printf("sent: %s", hello);
        // Listen for up to 2 seconds
        int len = uart_read_bytes(PI_UART, buf, sizeof(buf) - 1,
                                    pdMS_TO_TICKS(2000));
        if (len > 0) {
            buf[len] = '\0';
            printf("received: %s", buf);
        }
    }
}