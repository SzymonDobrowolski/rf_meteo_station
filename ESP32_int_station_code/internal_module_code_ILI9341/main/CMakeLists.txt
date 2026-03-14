idf_component_register(SRCS "main.c" "ui.c" "gpio_init.c" "spi_init.c" "nrf.c" "wifi_project.c" "lcd.c" "touch.c" "sntp.c" "weather_icons.c" "montserrat_pl_14.c" "put_logo.c"
                    INCLUDE_DIRS "." 
                    REQUIRES driver spiffs nvs_flash esp_wifi esp_netif spi_flash lvgl esp_timer esp_http_client json)