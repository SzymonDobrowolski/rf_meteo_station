#include <stdio.h>
#include "spi_init.h"
#include "driver/spi_master.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

spi_device_handle_t nrf_handle = NULL;
spi_device_handle_t lcd_handle = NULL;
spi_transaction_t nrf_rx;
spi_transaction_t nrf_tx;
uint8_t nrf_tx_buffer[33];
uint8_t nrf_rx_buffer[33];

void spi_init(void)
{
    spi_bus_config_t buscfg = {
        .miso_io_num = GPIO_NUM_19,
        .mosi_io_num = GPIO_NUM_23,
        .sclk_io_num = GPIO_NUM_18,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };
    spi_device_interface_config_t nrfcfg = { //TRZEBA USTAWIĆ WŁAŚCIWE PARAMETRY DLA NRF
        .clock_speed_hz = 1*5000*1000,           //Clock out at 5 MHz
        .mode = 0,                                //SPI mode 0
        .spics_io_num = GPIO_NUM_22
    };
        spi_device_interface_config_t lcdcfg = { //TRZEBA USTAWIĆ WŁAŚCIWE PARAMETRY DLA LCD
        .clock_speed_hz = 1*1000*1000,           //Clock out at 1 MHz
        .mode = 0,                                //SPI mode 0
        .spics_io_num = GPIO_NUM_21
    };
    spi_bus_initialize(VSPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    spi_bus_add_device(VSPI_HOST, &nrfcfg, &nrf_handle);
    spi_bus_add_device(VSPI_HOST, &lcdcfg, &lcd_handle);

    ESP_ERROR_CHECK(spi_bus_initialize(VSPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(VSPI_HOST, &nrfcfg, &nrf_handle));
    ESP_ERROR_CHECK(spi_bus_add_device(VSPI_HOST, &lcdcfg, &lcd_handle));

    ESP_LOGI("SPI", "SPI initialized");

}

static const char *TAG = "SPI_NRF";

esp_err_t spi_read_write(spi_device_handle_t slave, spi_transaction_t *trans)
{
    // Używamy spi_device_polling_transmit, który czeka na zakończenie transferu.
    esp_err_t ret = spi_device_polling_transmit(slave, trans);
    
    // Sprawdzenie i logowanie błędu, jeśli wystąpił
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd transmisji SPI (Kod: 0x%x)", ret);
    } else {
        // ESP_LOGI(TAG, "Transakcja SPI zakończona sukcesem.");
    }

    return ret;
}

uint8_t CMD_R_RX_PAYLOAD = 0x61;

void nrf_read_payload(void)
{
    nrf_tx_buffer[0] = CMD_R_RX_PAYLOAD; // komenda do TX

    spi_transaction_t t = {
        .length = (1 + 32) * 8,
        .tx_buffer = nrf_tx_buffer,
        .rx_buffer = nrf_rx_buffer,
    };

    spi_read_write(nrf_handle, &t);

    // teraz dane są w nrf_rx_buffer[1..32]
}
void nrf_write_payload(uint8_t *data, size_t len)
{
    if (len > 32) len = 32; // maksymalna długość payloadu to 32 bajty

    nrf_tx_buffer[0] = 0xA0; // komenda do zapisu payloadu
    memcpy(&nrf_tx_buffer[1], data, len);

    spi_transaction_t t = {
        .length = (1 + len) * 8,
        .tx_buffer = nrf_tx_buffer,
        .rx_buffer = NULL,
    };

    spi_read_write(nrf_handle, &t);
}

