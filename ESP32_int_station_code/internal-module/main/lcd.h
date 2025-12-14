#pragma once
#include <stdint.h>
#include "gpio_init.h"
#include "driver/spi_master.h"
#include "spi_init.h"
#include "fontx.h"
#include "ili9340.h"

void lcd_init();
extern TFT_t lcd; 
