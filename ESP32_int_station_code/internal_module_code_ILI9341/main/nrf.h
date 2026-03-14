#pragma once 
#include <stdint.h>
#include "driver/spi_master.h"

#include "Config.h"

// --- KOMENDY NRF24L01 ---
#define CMD_R_RX_PAYLOAD 0x61
#define CMD_W_REGISTER   0x20
#define CMD_FLUSH_RX     0xE2
#define CMD_FLUSH_TX     0xE1
// --- REJESTRY ---
#define REG_CONFIG      0x00
#define REG_EN_AA       0x01
#define REG_EN_RXADDR   0x02
#define REG_RF_CH       0x05
#define REG_RF_SETUP    0x06
#define REG_STATUS      0x07
#define REG_RX_PW_P0    0x11
#define REG_RX_ADDR_P0  0x0A

void nrf_init();
void nrf_read_payload(); //odczyt danych
void nrf_write_register(spi_device_handle_t spi , uint8_t reg, uint8_t data); //zapis do rejestru
uint8_t nrf_read_register(spi_device_handle_t spi, uint8_t reg); //odczyt rejestru
void nrf_read_buf(spi_device_handle_t spi, uint8_t cmd, uint8_t *buffer, uint8_t len);
bool nrf_receive_data(spi_device_handle_t spi, SensorData *data_out);
void nrf_write_buf(spi_device_handle_t spi, uint8_t reg, const uint8_t *data, uint8_t len);
extern uint8_t nrf_rx_buffer[32];

