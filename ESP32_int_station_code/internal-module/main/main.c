#include <stdio.h>
#include "gpio_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "spi_init.h"
#include "uart_init.h"
#include "nrf.h"
#include "ili9340.h"
#include "fontx.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "lcd.h"
#include "logo.h"


TFT_t lcd; //odnośnik do naszego ekranu
SensorData data; //sturktura danych z czujnika
spi_device_handle_t nrf_handle = NULL;
spi_device_handle_t lcd_handle = NULL;

FontxFile fx[2];

void init_spiffs()
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiflash",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE("SPIFFS", "Błąd montowania SPIFFS");
    } else {
        ESP_LOGI("SPIFFS", "SPIFFS zamontowany pod /spiflash");
    }
}

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

void lcd_update_task(void *pvParameters) {
    char buffer[64];
    lcdFillScreen(&lcd, WHITE);
    lcdDrawRect(&lcd, 0, 0, 200, 100, BLACK); //obszar dancyh
    lcdDrawRect(&lcd, 201, 0, 320, 100, BLACK); //obszar zegara
    lcdDrawString(&lcd, fx, 240, 25, (uint8_t *)"Data", BLACK);
    lcdDrawString(&lcd, fx, 240, 65, (uint8_t *)"Czas", BLACK);
    lcdDrawRect(&lcd, 0, 101, 320, 240, BLACK);  //obszar wykresu
    //lcdDrawString(&lcd, fx, 0, 150, (uint8_t *)"Tutaj bedzie wykres", BLACK);
    ili9341_draw_image(&lcd,110,120,LOGO_WIDTH,LOGO_HEIGHT,image_data_logo_pp_100px);
    while(1)
    {
        lcdSetFontDirection(&lcd, DIRECTION0);
        lcdDrawFillRect(&lcd,0,0,199,99,WHITE);
        lcdDrawString(&lcd, fx, 5, 25, (uint8_t *)"Dane z czujnika", BLACK);
        sprintf(buffer, "Temp: %.2f C", data.temp_hundredths / 100.0f);
        lcdDrawString(&lcd, fx, 5, 45, (uint8_t*)buffer, BLACK);

        sprintf(buffer, "Hum: %.2f %%", data.hum_x1024 / 1024.0f);
        lcdDrawString(&lcd, fx, 5, 65, (uint8_t*)buffer, BLACK);

        sprintf(buffer, "Press: %.2f hPa", data.pressure_pa / 100.0f);
        lcdDrawString(&lcd, fx, 5, 85, (uint8_t*)buffer, BLACK);

        vTaskDelay(pdMS_TO_TICKS(10000)); //odświeżanie co 500 ms
    }
}

void app_main(void)
{
    gpio_init();
    spi_init();
    uart_init();
    nrf_init(&nrf_handle); //inicjalizacja NRF
    init_spiffs();
    lcd_init(); //inicjalizacja LCD

    InitFontx(fx, "/spiflash/ILMH24XB.FNT", "");
         if (!OpenFontx(fx)) {
        ESP_LOGE("FONT", "Nie udało się otworzyć fontu");
        } else {
        ESP_LOGI("FONT", "Font otwarty poprawnie");
        }

    xTaskCreate(nrf_receiver_task, "NRF Receiver Task", 4096, NULL, 5, NULL); //odbieranie danych z NRF
    xTaskCreate(lcd_update_task, "LCD Update Task", 8192, NULL, 5, NULL); //aktualizacja LCD
}