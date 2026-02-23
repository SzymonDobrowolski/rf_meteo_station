#include <stdio.h>
#include "spi_init.h"
#include "driver/spi_master.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

#include "Config.h"

static const char *TAG = "SPI";

void spi_init(void)
{
    esp_err_t ret; // Zmienna do przechowywania kodów błędów

    spi_bus_config_t buscfg = {
        .miso_io_num = SPI_MISO,
        .mosi_io_num = SPI_MOSI,
        .sclk_io_num = SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 320 * 240 * 2 + 100
    };

    
    ret = spi_bus_initialize(VSPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);


    ESP_LOGI(TAG, "SPI initialized");
}

esp_err_t spi_read_write(spi_device_handle_t slave, spi_transaction_t *trans)
{
    // Używamy spi_device_polling_transmit, który czeka na zakończenie transferu.
    esp_err_t ret = spi_device_polling_transmit(slave, trans);
    
    // Sprawdzenie i logowanie błędu, jeśli wystąpił
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd transmisji SPI (Kod: 0x%x)", ret);
    }
    return ret;
}