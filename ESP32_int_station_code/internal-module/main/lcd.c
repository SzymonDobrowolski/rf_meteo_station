#include "lcd.h"
#include "ili9340.h"
#include "spi_init.h"
#include "fontx.h"
#include "driver/gpio.h" // Dodano brakujący nagłówek
#include "esp_log.h"     // Dodano do logowania
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CS_PIN 21
#define DC_PIN 26
#define RESET_PIN 25
#define BL_PIN 27
// MOSI i SCLK są zdefiniowane w spi_init.c, tu są tylko informacyjnie
// #define MOSI_PIN 23 
// #define SCLK_PIN 18

static const char *TAG = "LCD";

void lcd_init() {
    esp_err_t ret;

    // 1. Konfiguracja Pinów Sterujących (DC, RESET, BL)
    // Bez tego biblioteka nie może sterować ekranem
    gpio_reset_pin(DC_PIN);
    gpio_set_direction(DC_PIN, GPIO_MODE_OUTPUT);
    
    gpio_reset_pin(RESET_PIN);
    gpio_set_direction(RESET_PIN, GPIO_MODE_OUTPUT);

    gpio_reset_pin(BL_PIN);
    gpio_set_direction(BL_PIN, GPIO_MODE_OUTPUT);

    // 2. Przypisanie pinów do struktury TFT_t
    // Biblioteka ili9340.c używa tych zmiennych do komunikacji!
    lcd._dc = DC_PIN;
    lcd._bl = BL_PIN;
    lcd._width = 240;  // Wstępne ustawienie
    lcd._height = 320; // Wstępne ustawienie

    // 3. Hardware Reset ekranu (Kluczowe dla ILI9341)
    // Ekran musi zostać zresetowany przed inicjalizacją
    gpio_set_level(RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    // 4. Konfiguracja SPI
    spi_device_interface_config_t lcdcfg = {
        .clock_speed_hz = 20 * 1000 * 1000, // ZWIĘKSZONO: 1 MHz to bardzo wolno dla ekranu, 20 MHz jest bezpieczne
        .mode = 0,
        .spics_io_num = CS_PIN,             // CS na GPIO 21
        .queue_size = 7,
        .flags = SPI_DEVICE_NO_DUMMY,       // Często potrzebne dla wyświetlaczy
    };

    // Dodanie urządzenia do magistrali zainicjalizowanej w spi_init.c
    ret = spi_bus_add_device(VSPI_HOST, &lcdcfg, &lcd._TFT_Handle);
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "LCD SPI Device Added");

    // 5. Właściwa inicjalizacja sterownika
    // Teraz, gdy piny w strukturze są ustawione, ta funkcja zadziała poprawnie
    lcdInit(&lcd, 0x9341, 240, 320, 0, 0);
    
    ESP_LOGI(TAG, "LCD Initialized");
    
    lcdBacklightOn(&lcd);
    lcdSetFontDirection(&lcd, DIRECTION0);
}