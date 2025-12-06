#include <stdio.h>
#include "gpio_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "spi_init.h"
#include "uart_init.h"
float temperature;
float humidity;
float pressure;

void read_nrf_data_task(void *pvParameters)
{
    while(1) {
        // Tutaj dodaj kod do odczytu danych z NRF
        vTaskDelay((1000*60*10) / portTICK_PERIOD_MS); // Opóźnienie 10 minut
        nrf_read_payload();
        uint8_t *data = nrf_rx_buffer;
        memcpy(&temperature, data, sizeof(float));
        memcpy(&humidity, data + sizeof(float), sizeof(float));
        memcpy(&pressure, data + 2*sizeof(float), sizeof(float));
    }
}

void app_main(void)
{
    gpio_init();
    spi_init();
    uart_init();
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    xTaskCreate(&read_nrf_data_task, "read_nrf_data_task", 4096, NULL, 5, NULL);
    printf("TEMPERATURE: %f",temperature);
    printf("HUMMIDITY: %f",humidity);
    printf("PRESSURE: %f",pressure);   
}