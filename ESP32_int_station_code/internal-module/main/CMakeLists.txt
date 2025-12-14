idf_component_register(SRCS "nrf.c" "main.c" "gpio_init.c" "uart_init.c" "spi_init.c" "nrf.c" "lcd.c" 
                    INCLUDE_DIRS "." REQUIRES ili9340 driver spiffs)