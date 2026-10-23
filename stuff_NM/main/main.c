
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_err.h"

#define MCP_MISO 19
#define MCP_MOSI 23
#define MCP_CLK  18
#define MCP_CS   5

static const char *TAG = "MCP3008";
static spi_device_handle_t mcp;

// Read one MCP3008 analog channel (0-7)
static int mcp3008_read(int channel)
{
    if (channel < 0 || channel > 7) {
        return -1;
    }

    uint8_t tx[3] = {
        0x01,
        (uint8_t)(0x80 | (channel << 4)),
        0x00
    };

    uint8_t rx[3] = {0};

    spi_transaction_t transaction = {
        .length = 24,
        .tx_buffer = tx,
        .rx_buffer = rx
    };

    ESP_ERROR_CHECK(
        spi_device_transmit(mcp, &transaction)
    );

    return ((rx[1] & 0x03) << 8) | rx[2];
}

void app_main(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = MCP_MOSI,
        .miso_io_num = MCP_MISO,
        .sclk_io_num = MCP_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };

    spi_device_interface_config_t device = {
        .clock_speed_hz = 1000000,
        .mode = 0,
        .spics_io_num = MCP_CS,
        .queue_size = 1
    };

    ESP_ERROR_CHECK(
        spi_bus_initialize(
            SPI2_HOST, &bus, SPI_DMA_DISABLED
        )
    );

    ESP_ERROR_CHECK(
        spi_bus_add_device(
            SPI2_HOST, &device, &mcp
        )
    );

    ESP_LOGI(TAG, "MCP3008 ready!");

    while (1) {
        int value = mcp3008_read(0);
        float voltage = value * 3.3f / 1023.0f;

        ESP_LOGI(TAG,
                 "CH0: %d | Voltage: %.3f V",
                 value, voltage);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
