#include <stdio.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>

extern spi_device_handle_t nrf_spi;

void nrf_spi_init();
uint8_t nrf_spi_transfer(uint8_t data);
void nrf_write_reg(uint8_t reg, uint8_t value);
uint8_t nrf_read_reg(uint8_t reg);
uint8_t nrf_read_payload(uint8_t *buf, uint8_t len);
bool nrf_data_ready();
void nrf_set_rx_mode();
