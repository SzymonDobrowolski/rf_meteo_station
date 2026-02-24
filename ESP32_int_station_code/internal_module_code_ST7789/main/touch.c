#include "touch.h"
#include "lvgl.h"          // NAPRAWA: Brakowało tego nagłówka (błąd unknown type name)
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "Config.h"        // Tu masz definicje T_IRQ, T_CS, LCD_WIDTH itd.

static const char *TAG = "XPT2046";
static spi_device_handle_t touch_spi_handle;

// Komendy dla XPT2046
#define CMD_X_READ  0xD0
#define CMD_Y_READ  0x90

static uint16_t xpt2046_spi_read(uint8_t command) {
    uint8_t data[3] = { command, 0x00, 0x00 };
    spi_transaction_t t = {
        .length = 24,
        .tx_buffer = data,
        .flags = SPI_TRANS_USE_RXDATA,
    };
    
    if (spi_device_transmit(touch_spi_handle, &t) != ESP_OK) return 0;
    
    // Wynik 12-bitowy
    return ((t.rx_data[1] << 8) | t.rx_data[2]) >> 3;
}

// Funkcja odczytu dla LVGL
static void touchpad_read(lv_indev_drv_t * drv, lv_indev_data_t * data) {
    if (gpio_get_level(T_IRQ) == 0) {
        uint16_t x_raw = xpt2046_spi_read(CMD_X_READ);
        uint16_t y_raw = xpt2046_spi_read(CMD_Y_READ);

        // 1. Najpierw mapujemy surowe dane na zakres 0-320 i 0-240
        int32_t x = (x_raw - 200) * LCD_WIDTH / (3800 - 200);
        int32_t y = (y_raw - 200) * LCD_HEIGHT / (3800 - 200);

        // 2. KOREKTA ORIENTACJI (Landscape 0xE8)
        // Jeśli lewy górny klik aktywuje prawy dół, musimy odwrócić obie osie:
        data->point.x = LCD_WIDTH - x;
        data->point.y = LCD_HEIGHT - y;

        // Zabezpieczenie zakresu
        if(data->point.x < 0) data->point.x = 0;
        if(data->point.x >= LCD_WIDTH) data->point.x = LCD_WIDTH - 1;
        if(data->point.y < 0) data->point.y = 0;
        if(data->point.y >= LCD_HEIGHT) data->point.y = LCD_HEIGHT - 1;

        data->state = LV_INDEV_STATE_PR;
        
        // Loguj przeliczone punkty, żebyś widział gdzie LVGL "widzi" palec
        ESP_LOGI(TAG, "LVGL Point -> X: %d, Y: %d", data->point.x, data->point.y);
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void lcd_touch_init(spi_host_device_t host) {
    // 1. Konfiguracja T_IRQ
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << T_IRQ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);

    // 2. Konfiguracja SPI dla dotyku
    // NAPRAWA: Dodano brakującą definicję 'devcfg'
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1 * 1000 * 1000, 
        .mode = 0,
        .spics_io_num = T_CS,
        .queue_size = 7,
    };
    
    esp_err_t ret = spi_bus_add_device(host, &devcfg, &touch_spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device");
        return;
    }

   // 3. Rejestracja w LVGL
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;

    // NAPRAWA: Zmiana z lv_indev_register na lv_indev_drv_register
    lv_indev_t * indev = lv_indev_drv_register(&indev_drv);

    if(indev) {
        ESP_LOGI(TAG, "Touch initialized on CS: %d", T_CS);
    }
}