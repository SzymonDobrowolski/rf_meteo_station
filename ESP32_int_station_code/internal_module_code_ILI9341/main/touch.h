#pragma once

#include "driver/spi_master.h"

// Dodajemy argument spi_host_device_t
void lcd_touch_init(spi_host_device_t host);