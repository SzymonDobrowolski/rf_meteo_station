#include <stdio.h>
#include "gpio_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "spi_init.h"
#include "uart_init.h"
#include "nrf.h"

SensorData data;
spi_device_handle_t nrf_handle = NULL;
spi_device_handle_t lcd_handle = NULL;

void nrf_receiver_task(void *pvParameters) {
    while(1)
    {
        
        nrf_receive_data(nrf_handle, &data); //odbieranie danych
        float temp_converted = data.temp_hundredths / 100.0f;
        float hum_converted = data.hum_x1024 / 1024.0f;
        float press_converted = data.pressure_pa / 100.0f;
        //printf("Temperature: %.2f C, Humidity: %.2f %%, Pressure: %.2f hPa\n", 
               //temp_converted, hum_converted, press_converted);
        vTaskDelay(pdMS_TO_TICKS(100)); //opóźnienie

        
    }
}

void app_main(void)
{
    gpio_init();
    spi_init();
    uart_init();
    nrf_init(&nrf_handle); //inicjalizacja NRF

    xTaskCreate(nrf_receiver_task, "NRF Receiver Task", 4096, NULL, 5, NULL); //odbieranie danych z NRF
}