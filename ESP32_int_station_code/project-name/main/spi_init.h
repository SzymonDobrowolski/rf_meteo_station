#include <stdio.h>
#include "driver/spi_master.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#pragma once
extern spi_device_handle_t nrf_handle; //wskaźnik na urządzenie NRF
extern spi_device_handle_t lcd_handle;
void spi_init();
esp_err_t spi_read_write(spi_device_handle_t slave, spi_transaction_t *trans);