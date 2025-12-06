#pragma once
#include <stdio.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>

#include "NRF24L01.h"

#define NRF_PIN_CE   22
#define NRF_PIN_CSN   16
#define NRF_PIN_IRQ    34

#define NRF_CSN_LOW()  gpio_set_level(NRF_PIN_CSN, 0)
#define NRF_CSN_HIGH() gpio_set_level(NRF_PIN_CSN, 1)

#define NRF_CE_LOW()   gpio_set_level(NRF_PIN_CE, 0)
#define NRF_CE_HIGH()  gpio_set_level(NRF_PIN_CE, 1)

void nrf_spi_init()
{
    gpio_set_direction(NRF_PIN_CE, GPIO_MODE_OUTPUT);
    gpio_set_direction(NRF_PIN_CSN, GPIO_MODE_OUTPUT);

    gpio_set_level(NRF_PIN_CE, 0);
    gpio_set_level(NRF_PIN_CSN, 1);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 5*1000*1000, // 5 MHz
        .mode = 0,
        .spics_io_num = -1,  // używamy własnego CSN
        .queue_size = 2,
    };

    spi_bus_add_device(SPI3_HOST, &devcfg, &nrf_spi);
}

uint8_t nrf_spi_transfer(uint8_t data)
{
    uint8_t rx;
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &data,
        .rx_buffer = &rx
    };
    spi_device_transmit(nrf_spi, &t);
    return rx;
}

void nrf_write_reg(uint8_t reg, uint8_t value)
{
    NRF_CSN_LOW();
    nrf_spi_transfer(0x20 | (reg & 0x1F)); 
    nrf_spi_transfer(value);
    NRF_CSN_HIGH();
}

uint8_t nrf_read_reg(uint8_t reg)
{
    NRF_CSN_LOW();
    nrf_spi_transfer(reg & 0x1F);
    uint8_t val = nrf_spi_transfer(0xFF);
    NRF_CSN_HIGH();
    return val;
}

uint8_t nrf_read_payload(uint8_t *buf, uint8_t len)
{
    NRF_CSN_LOW();
    nrf_spi_transfer(0x61);   // R_RX_PAYLOAD

    for (int i = 0; i < len; i++)
        buf[i] = nrf_spi_transfer(0xFF);

    NRF_CSN_HIGH();

    // wyczyszczenie flagi RX_DR
    nrf_write_reg(0x07, 1 << 6);

    return len;
}

bool nrf_data_ready()
{
    uint8_t status = nrf_read_reg(0x07); // STATUS
    return status & (1 << 6); // RX_DR
}

void nrf_set_rx_mode()
{
    NRF_CE_LOW();

    nrf_write_reg(0x00, (1 << 1) | (1 << 0));    // PWR_UP + PRIM_RX

    nrf_write_reg(0x05, 76);    // channel
    nrf_write_reg(0x06, 0x0F);  // setup

    // RX address
    NRF_CSN_LOW();
    nrf_spi_transfer(0x20 | 0x0A);
    nrf_spi_transfer('A');
    nrf_spi_transfer('T');
    nrf_spi_transfer('4');
    nrf_spi_transfer('1');
    nrf_spi_transfer('4');
    NRF_CSN_HIGH();

    // flush RX FIFO
    NRF_CSN_LOW();
    nrf_spi_transfer(0xE2);
    NRF_CSN_HIGH();

    NRF_CE_HIGH(); // start listening
}
