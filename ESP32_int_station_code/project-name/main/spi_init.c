#include "spi_init.h"
#include "driver/spi_master.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"
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
        .clock_speed_hz = 1*1000*1000,           //Clock out at 1 MHz
        .mode = 0,                                //SPI mode 0
        .spics_io_num = GPIO_NUM_22
    };
        spi_device_interface_config_t lcdcfg = { //TRZEBA USTAWIĆ WŁAŚCIWE PARAMETRY DLA LCD
        .clock_speed_hz = 1*1000*1000,           //Clock out at 1 MHz
        .mode = 0,                                //SPI mode 0
        .spics_io_num = GPIO_NUM_21
    };
    spi_bus_initialize(VSPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    spi_device_handle_t nrf_handle; //wskaźnik do urządzenia SPI
    spi_device_handle_t lcd_handle; //wskaźnik do urządzenia SPI
    spi_bus_add_device(VSPI_HOST, &nrfcfg, &nrf_handle);
    spi_bus_add_device(VSPI_HOST, &lcdcfg, &lcd_handle);
    spi_transaction_t nrf_read; //struktura danych dla nrf
    spi_transaction_t lcd_write; //struktura danych dla lcd do wysłania
}
void spi_read(spi_device_handle_t slave)
{

}

void spi_write(spi_device_handle_t slave)
{

}
