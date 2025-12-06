idf_component_register(SRCS "main.c" "NRF24L01.c"
                    INCLUDE_DIRS "."
                    REQUIRES ili9340 driver spiffs)