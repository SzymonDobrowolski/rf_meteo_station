#include <stdio.h>
#include "spi_init.h"
#include "driver/spi_master.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

static const char *TAG = "SPI";

void spi_init(void)
{
    esp_err_t ret; // Zmienna do przechowywania kodów błędów

    spi_bus_config_t buscfg = {
        .miso_io_num = GPIO_NUM_19,
        .mosi_io_num = GPIO_NUM_23,
        .sclk_io_num = GPIO_NUM_18,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };

    spi_device_interface_config_t nrfcfg = {
        .clock_speed_hz = 5*1000*1000,   // 5 MHz jest OK dla NRF
        .mode = 0,                       
        .spics_io_num = GPIO_NUM_22,
        .queue_size = 7                  // POPRAWKA 3: Wymagane > 0
    };

    spi_device_interface_config_t lcdcfg = {
        .clock_speed_hz = 1*1000*1000,   // 1 MHz
        .mode = 0,                       
        .spics_io_num = GPIO_NUM_21,
        .queue_size = 7                  // POPRAWKA 3: Wymagane > 0
    };

    // POPRAWKA 2: Usunięto podwójne wywołania. 
    // Wywołujemy raz i od razu sprawdzamy błąd.
    
    ret = spi_bus_initialize(VSPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    ret = spi_bus_add_device(VSPI_HOST, &nrfcfg, &nrf_handle);
    ESP_ERROR_CHECK(ret);

    ret = spi_bus_add_device(VSPI_HOST, &lcdcfg, &lcd_handle);
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