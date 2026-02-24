#include "lcd.h"
#include "lvgl.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "Config.h"

static const char *TAG = "LCD_LVGL";

void lcd_init(void) {
    ESP_LOGI(TAG, "Inicjalizacja esp_lcd...");

    // 1. Piny sterujące
    gpio_reset_pin(DC_PIN);
    gpio_set_direction(DC_PIN, GPIO_MODE_OUTPUT);

    // 2. Podpięcie pod sprzętowe SPI
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = DC_PIN,
        .cs_gpio_num = CS_PIN,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)VSPI_HOST, &io_config, &io_handle));

    // 3. Konfiguracja sprzętowa sterownika ST7789
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = RESET_PIN,
        .color_space = ESP_LCD_COLOR_SPACE_RGB, 
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    // --- NAPRAWA KRZAKÓW I OBRACANIE EKRANU ---

    // Reset i inicjalizacja
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);

    // Inwersja kolorów (zostaw false lub daj true, by zlikwidować ew. negatyw)
    esp_lcd_panel_invert_color(panel_handle, true); 

    // Zerujemy ukryte marginesy pamięci
    esp_lcd_panel_set_gap(panel_handle, 0, 0);

    // Wymuszenie sprzętowego trybu Landscape (Poziomo)
    esp_lcd_panel_swap_xy(panel_handle, true);
    
    // Jeśli obraz będzie wyświetlał się "do góry nogami", 
    // zamień poniżej parametry na: (panel_handle, false, true)
    esp_lcd_panel_mirror(panel_handle, false, true); 
    
    // Włączamy ekran
    esp_lcd_panel_disp_on_off(panel_handle, true);

    // 4. Inicjalizacja środowiska LVGL
    ESP_LOGI(TAG, "Inicjalizacja LVGL...");
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    // 5. Podpięcie naszego panelu do LVGL
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_WIDTH * LCD_HEIGHT / 10,
        .double_buffer = true,
        .hres = LCD_WIDTH,  // U Ciebie 320
        .vres = LCD_HEIGHT, // U Ciebie 240
        .monochrome = false,
        .rotation = {
            // UWAGA: Skoro obróciliśmy ekran sprzętowo (swap_xy), LVGL już nie może tego robić drugi raz!
            .swap_xy = false,   
            .mirror_x = false, 
            .mirror_y = false,
        }
    };
    lvgl_port_add_disp(&disp_cfg);
    
    ESP_LOGI(TAG, "LVGL gotowe!");
}