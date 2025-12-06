#include <stdio.h>
#include <string.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_spiffs.h>
#include <esp_log.h>

#include "ili9340.h"
#include "fontx.h"

#include "NRF24L01.h"


#define PIN_LCD_CS     4
#define PIN_LCD_DC     26
#define PIN_LCD_RST    25
#define PIN_LCD_LED    27

#define PIN_MISO 19
#define PIN_MOSI 23
#define PIN_SCK  18

typedef struct __attribute__((packed)) {
    float temperature_C;
    float humidity_pct;
    float pressure_hPa;
} sensor_packet_t;

spi_device_handle_t nrf_spi;
spi_device_handle_t spi_lcd;

void spi_bus_init()
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
}

TFT_t tft;
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

void lcd_init(void)
{
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40*1000*1000,  // 40 MHz
        .mode = 0,
        .spics_io_num = PIN_LCD_CS,
        .queue_size = 7,
        .pre_cb = NULL,
    };
    spi_bus_add_device(SPI3_HOST, &devcfg, &tft._TFT_Handle);

    lcdInit(&tft, 0x9341, 240, 320, 0, 0);
    lcdBacklightOn(&tft);

    lcdSetFontDirection(&tft, DIRECTION0);
}


void app_main(void)
{
    init_spiffs();
    spi_bus_init();
    nrf_spi_init();
    nrf_set_rx_mode();
    lcd_init();

    
    InitFontx(fx, "/spiflash/ILGH16XB.FNT", "");
     if (!OpenFontx(fx)) {
        ESP_LOGE("FONT", "Nie udało się otworzyć fontu");
    } else {
        ESP_LOGI("FONT", "Font otwarty poprawnie");
    }

    sensor_packet_t pkt;

    uint8_t rxbuf[32];

    while (1)
    {
        if(nrf_data_ready())
        {
            int len = nrf_read_payload(rxbuf, 32);
            printf("Len: %d\n", len);

            memcpy(&pkt.temperature_C, &rxbuf[0], 4);
            memcpy(&pkt.humidity_pct, &rxbuf[4], 4);
            memcpy(&pkt.pressure_hPa, &rxbuf[8], 4);

            char buf[64];
            lcdFillScreen(&tft, BLACK);

            // Temperatura
            sprintf(buf, "Temp: %.2f C", pkt.temperature_C);
            printf(buf);
            lcdDrawString(&tft, fx, 10, 40, (uint8_t*)buf, WHITE);

            // Wilgotność
            sprintf(buf, "Wilg: %.2f %%", pkt.humidity_pct);
            printf(buf);
            lcdDrawString(&tft, fx, 10, 70, (uint8_t*)buf, GREEN);

            // Ciśnienie
            sprintf(buf, "Cisn: %.2f hPa", pkt.pressure_hPa);
            printf(buf);
            lcdDrawString(&tft, fx, 10, 100, (uint8_t*)buf, CYAN);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
}