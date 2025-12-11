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
    printf("--- TRYB DIAGNOSTYCZNY ---\n");
    vTaskDelay(pdMS_TO_TICKS(100));

    while(1)
    {
        // Odczytujemy kluczowe rejestry diagnostyczne
        uint8_t status = nrf_read_register(nrf_handle, REG_STATUS);       // Czy są przerwania?
        uint8_t fifo_status = nrf_read_register(nrf_handle, 0x17);        // Czy bufor jest pusty?
        uint8_t rx_pw = nrf_read_register(nrf_handle, REG_RX_PW_P0);      // Ile bajtów oczekuje radio?
        uint8_t feature = nrf_read_register(nrf_handle, 0x1D);            // Czy włączone są dynamiczne długości?

        printf("STATUS: 0x%02X | FIFO: 0x%02X | Oczekiwany Rozmiar: %d | Feature: 0x%02X\n", 
               status, fifo_status, rx_pw, feature);

        // Próba odczytu "na ślepo" (jeśli FIFO nie jest puste)
        // Bit 0 w FIFO_STATUS (RX_EMPTY): 1=Puste, 0=Są dane
        if ((fifo_status & 1) == 0) {
            printf("!!! ALARM: FIFO NIE JEST PUSTE, ALE STATUS MILCZY !!!\n");
            uint8_t raw[12];
            nrf_read_buf(nrf_handle, CMD_R_RX_PAYLOAD, raw, 12);
            printf("ODCZYTANO NA SIŁĘ: %02X %02X %02X ...\n", raw[0], raw[1], raw[2]);
            
            // Czyszczenie flagi po odczycie
            nrf_write_register(nrf_handle, REG_STATUS, 0x70);
        }

        vTaskDelay(pdMS_TO_TICKS(500)); 
    }
}


void app_main(void)
{
    gpio_init();
    spi_init();
    uart_init();
    nrf_init(&nrf_handle);
    uint8_t check_ch = nrf_read_register(nrf_handle, REG_RF_CH);
    printf("DEBUG: Odczytany kanał z NRF: %d\n", check_ch);
    gpio_set_direction(GPIO_NUM_17, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_17, 1); // Wskaźnik aktywności (LED)
    xTaskCreate(nrf_receiver_task, "NRF Receiver Task", 4096, NULL, 5, NULL);
}