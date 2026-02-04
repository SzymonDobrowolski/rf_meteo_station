#pragma once
#define BUTTON_1 GPIO_NUM_36
#define BUTTON_2 GPIO_NUM_39

#define LED1 GPIO_NUM_13
#define LED2 GPIO_NUM_21

// SPI Pins
#define SPI_MISO GPIO_NUM_19
#define SPI_MOSI GPIO_NUM_23
#define SPI_SCLK GPIO_NUM_18

// NRF Pins
#define NRF_CE_PIN GPIO_NUM_22
#define NRF_CSN_PIN GPIO_NUM_16
//NRF Settings
#define RX_ADDRESS_VAL {'A', 'T', '4', '1', '4'}
#define Channel 76
#define NRF_SPEED 0x07 //2Mbps, 0dBm

// --- STRUKTURA DANYCH (12 bajtów) ---
typedef struct __attribute__((packed)) {
    int32_t temp_hundredths;
    uint32_t pressure_pa;
    uint32_t hum_x1024;
} SensorData;

// LCD Pins
#define CS_PIN GPIO_NUM_4
#define DC_PIN GPIO_NUM_26
#define RESET_PIN GPIO_NUM_25
#define BL_PIN GPIO_NUM_27
//LCD Settings
#define LCD_TYPE 0x7789 //lub 0x9341
#define LCD_WIDTH  320
#define LCD_HEIGHT 240
#define LCD_ORIENTATION_1 0x36 // Portrait  //dla ili9341 0x36
#define LCD_ORIENTATION_2 0xE8 // Landscape //dla ili9341 0xA8
//Optional LCD
#define T_CS GPIO_NUM_14
#define SD_CS GPIO_NUM_17

#define NRF_IRQ GPIO_NUM_34
#define T_IRQ GPIO_NUM_35

// NTC
#define SSID "POCO X3 NFC"
#define PASSWORD "Rafik123"
#define SNTP_SERVER "pool.ntp.org"
