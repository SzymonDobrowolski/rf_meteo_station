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
    printf("\n=== START ODBIORNIKA NRF24L01 ===\n");
    printf("Oczekiwanie na pakiety z ATtiny414...\n");

    uint8_t led_state = 0;
    TickType_t last_log_time = xTaskGetTickCount();

    while(1)
    {
        // 1. Próba odbioru danych (funkcja zwraca true tylko jak coś przyszło)
        if (nrf_receive_data(nrf_handle, &data)) {
            
            // --- SUKCES! Otrzymano pakiet ---
            
            // a) Mrugnij diodą (zmień stan na przeciwny)
            led_state = !led_state;
            gpio_set_level(GPIO_NUM_17, led_state);

            // b) Wypisz dane czytelnie
            printf("[ODEBRANO] Temp: %.2f C | Wilg: %.2f %% | Cisn: %.2f hPa\n", 
                   data.temp, data.hum, data.press);

            // c) Reset licznika logów "czuwania"
            last_log_time = xTaskGetTickCount();
        }
        else {
            // --- BRAK DANYCH (CISZA) ---
            
            // Co 2 sekundy wypisz status rejestru, żebyś wiedział, że ESP żyje
            if ((xTaskGetTickCount() - last_log_time) > pdMS_TO_TICKS(2000)) {
                uint8_t status = nrf_read_register(nrf_handle, REG_STATUS);
                printf("... Czuwanie (Status Reg: 0x%02X) ...\n", status);
                
                // Diagnostyka: Jeśli status to 0x0E lub 0x00 cały czas, to znaczy że radio śpi/nie ma zasilania
                if (status == 0x00 || status == 0xFF) {
                    printf("!!! UWAGA: Podejrzany status radia (może brak zasilania/kabli?) !!!\n");
                }
                
                last_log_time = xTaskGetTickCount();
            }
        }

        // Krótka przerwa, żeby nie "zajechać" procesora, ale reagować szybko
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void app_main(void)
{
    gpio_init();
    spi_init();
    uart_init();
    
    // Inicjalizacja NRF i zapisanie uchwytu
    nrf_init(&nrf_handle);

    // Konfiguracja diody LED do debugowania
    gpio_set_direction(GPIO_NUM_17, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_17, 0); // Start od zgaszonej

    // Uruchomienie zadania
    xTaskCreate(nrf_receiver_task, "NRF Receiver Task", 4096, NULL, 5, NULL);
}