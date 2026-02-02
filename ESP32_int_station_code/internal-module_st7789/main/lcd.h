#pragma once
#include <stdint.h>
#include "gpio_init.h"
#include "driver/spi_master.h"
#include "spi_init.h"
#include "fontx.h"
#include "ili9340.h"

void lcd_init();
void ili9341_draw_image(TFT_t *dev, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *bitmap);
extern TFT_t lcd; 
