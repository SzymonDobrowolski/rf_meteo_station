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

// --- ZADANIE ODBIORCZE Z LEPSZYM DEBUGOWANIEM ---
void nrf_receiver_task(void *pvParameters) {

    while(1)
    {
        nrf_receive_data(nrf_handle, &data); //odbieranie danych
        //printf("[ODEBRANO przed konerwsja] T: %d | H: %d | P: %d\n", //odczyt w terminalu
               // data.temp_hundredths, data.hum_x1024, data.pressure_pa);

        float temp_converted = data.temp_hundredths / 100.0f;
        float hum_converted = data.hum_x1024 / 1024.0f;
        float press_converted = data.pressure_pa / 100.0f;

        printf("[ODEBRANO po konwersji] T: %.2f | H: %.2f | P: %.2f\n", //odczyt w terminalu
                temp_converted, hum_converted, press_converted);

        
        vTaskDelay(pdMS_TO_TICKS(10)); //opóźnienie odbioru
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