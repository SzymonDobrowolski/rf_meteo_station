#include "touch.h"
#include "lvgl.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "Config.h"

static const char *TAG = "XPT2046";
static spi_device_handle_t touch_spi_handle;

// Komendy dla XPT2046
#define CMD_X_READ  0xD0
#define CMD_Y_READ  0x90

static uint16_t xpt2046_spi_read(uint8_t command) {
    uint8_t tx_data[3] = { command, 0x00, 0x00 };
    uint8_t rx_data[3] = { 0 };
    spi_transaction_t t = {
        .length = 24,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    
    if (spi_device_transmit(touch_spi_handle, &t) != ESP_OK) return 0;
    
    // XPT2046 zwraca 12-bitowy wynik w 24 cyklach.
    // Dane znajdują się w 2 i 3 bajcie odpowiedzi.
    return ((rx_data[1] << 8) | rx_data[2]) >> 4;
}

static void touchpad_read(lv_indev_drv_t * drv, lv_indev_data_t * data) {
    if (gpio_get_level(T_IRQ) == 0) {
        
        // Ponieważ już zamieniłeś komendy odczytu (X z Y), zakładam że:
        // x_raw reaguje na ruch Lewo-Prawo
        // y_raw reaguje na ruch Góra-Dół
        uint16_t x_raw = xpt2046_spi_read(CMD_Y_READ); 
        uint16_t y_raw = xpt2046_spi_read(CMD_X_READ);

        // Wyliczone limity z Twoich pomiarów
        int32_t x_min = 200;
        int32_t x_max = 1930;
        int32_t y_min = 164;
        int32_t y_max = 1910;

        // Przeskalowanie surowych danych na rozdzielczość ekranu (0-320 i 0-240)
        int32_t x = (x_raw - x_min) * LCD_WIDTH / (x_max - x_min);
        int32_t y = (y_raw - y_min) * LCD_HEIGHT / (y_max - y_min);

        // --- ZABEZPIECZENIE (Clamping) ---
        // Przywracamy je! Jeśli naciśniesz mocniej w samym rogu, x_raw może wynieść np. 190.
        // Wtedy 'x' wyszłoby ujemne, a LVGL by zwariował. To ucina wartości do krawędzi.
        if(x < 0) x = 0;
        if(x >= LCD_WIDTH) x = LCD_WIDTH - 1;
        if(y < 0) y = 0;
        if(y >= LCD_HEIGHT) y = LCD_HEIGHT - 1;

        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;

        // Możesz to odkomentować do testów, żeby sprawdzić precyzję
         ESP_LOGI(TAG, "MAPPED: X=%d, Y=%d", data->point.x, data->point.y);
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void lcd_touch_init(spi_host_device_t host) {
    // 1. Konfiguracja T_IRQ
    gpio_reset_pin(T_IRQ);
    gpio_set_direction(T_IRQ, GPIO_MODE_INPUT);
    gpio_set_pull_mode(T_IRQ, GPIO_PULLUP_ONLY);

    // 2. Konfiguracja SPI dla dotyku
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1 * 1000 * 1000, 
        .mode = 0,
        .spics_io_num = T_CS,
        .queue_size = 7,
    };
    
    ESP_ERROR_CHECK(spi_bus_add_device(host, &devcfg, &touch_spi_handle));

    // 3. Rejestracja w LVGL
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;

    lv_indev_drv_register(&indev_drv);
    ESP_LOGI(TAG, "Touch initialized successfully");
}