#include "lcd.h"
#include "ili9340.h"
#include "spi_init.h"
#include "fontx.h"
#include "driver/gpio.h" // Dodano brakujący nagłówek
#include "esp_log.h"     // Dodano do logowania
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Config.h"

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
        .clock_speed_hz = 40 * 1000 * 1000, 
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
    lcdInit(&lcd, LCD_TYPE, LCD_WIDTH, LCD_HEIGHT, 0, 0);
    lcdBacklightOn(&lcd);
    spi_master_write_comm_byte(&lcd, LCD_ORIENTATION_1); //ustawienie orientacji poziomej
    spi_master_write_data_byte(&lcd, LCD_ORIENTATION_2); //ustawienie orientacji poziomej
    ESP_LOGI(TAG, "LCD Initialized");

    
}

// Wklej to do lcd.c (zastąp starą wersję)

void ili9341_draw_image(TFT_t *dev, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *bitmap) {
    
    // 1. Pobieramy uchwyt SPI i pin DC bezpośrednio z Twojej struktury lcd
    spi_device_handle_t spi = dev->_TFT_Handle;
    int dc_pin = dev->_dc;

    // --- Funkcje pomocnicze lokalne (żeby nie polegać na globalnych makrach) ---
    void local_send_cmd(uint8_t cmd) {
        gpio_set_level(dc_pin, 0); // DC Low = Command
        spi_transaction_t t = {0};
        t.length = 8;
        t.tx_buffer = &cmd;
        spi_device_polling_transmit(spi, &t);
    }

    void local_send_data(const uint8_t *data, size_t len) {
        if (len == 0) return;
        gpio_set_level(dc_pin, 1); // DC High = Data
        spi_transaction_t t = {0};
        t.length = len * 8;
        t.tx_buffer = data;
        spi_device_polling_transmit(spi, &t);
    }
    // -------------------------------------------------------------------------

    // 2. Ustawienie obszaru rysowania (Okno adresowe)
    
    // --- KOLUMNA (X) ---
    local_send_cmd(0x2A); 
    uint8_t data_x[] = { 
        (uint8_t)(x >> 8), (uint8_t)(x & 0xFF), 
        (uint8_t)((x + w - 1) >> 8), (uint8_t)((x + w - 1) & 0xFF) 
    };
    local_send_data(data_x, 4);

    // --- WIERSZ (Y) ---
    local_send_cmd(0x2B);
    uint8_t data_y[] = { 
        (uint8_t)(y >> 8), (uint8_t)(y & 0xFF), 
        (uint8_t)((y + h - 1) >> 8), (uint8_t)((y + h - 1) & 0xFF) 
    };
    local_send_data(data_y, 4);

    // 3. Wysłanie pikseli
    local_send_cmd(0x2C); // Memory Write
    
    size_t buffer_size = w * h * 2;
    local_send_data((const uint8_t*)bitmap, buffer_size);
}