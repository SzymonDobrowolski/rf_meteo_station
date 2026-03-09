#include <avr/io.h>
#define F_CPU 20000000UL // 20 MHz

#define SLEEP_SCALER RTC_PERIOD_CYC8192_gc //Co 8 sekund sygnał 
/* inne dzielniki czasu uspania:
    RTC_PERIOD_CYC1024_gc  = 1 sekunda
    RTC_PERIOD_CYC4096_gc  = 4 sekundy
    RTC_PERIOD_CYC8192_gc  = 8 sekund
    RTC_PERIOD_CYC16384_gc = 16 sekund
    RTC_PERIOD_CYC32768_gc = 32 sekundy

*/

/*w celu optymalizacji zużycia enrgii zalecamy zakomentować 
blink_led(3, 100); //sygnalizacja wysłania danych w pliku main.cpp
*/

#define LED_PORT  PORTA
#define LED_PIN   PIN7_bm //lub PIN6_bm w zależności od płytki

#define NRF_CE_PIN PIN5_bm
#define NRF_CSN_PIN PIN4_bm

#define I2C_PORT    PORTB
#define SCL_PIN     PIN0_bm  // Jeśli w teście działało odwrotnie, zamień na PIN1_bm
#define SDA_PIN     PIN1_bm  // Jeśli w teście działało odwrotnie, zamień na PIN0_bm

#define SPI_PORT PORTA
#define MOSI_PIN PIN1_bm
#define MISO_PIN PIN2_bm
#define SCK_PIN PIN3_bm

#define Channel 76
#define NRF_SPEED 0x07 //2Mbps, 0dBm
#define SPI_SEND_ADDRESS() do { \
    SPI_transfer('A'); \
    SPI_transfer('T'); \
    SPI_transfer('4'); \
    SPI_transfer('1'); \
    SPI_transfer('4'); \
} while(0) //wysyła 5 bajtów adresu

#define BME280_ADDR 0x76 //lub 0x77 zaleznie od plytki

typedef struct __attribute__((packed)) {//to jest struktura 14 bajtowa, nrf może przyjąc 32, dzieki struktorze packet wyslemy cale 12 bajtow za jednym zamachem
    int32_t temp_hundredths;
    uint32_t pressure_pa;
    uint32_t hum_x1024;
    uint16_t vcc_mv; // napięcie baterii w mV
} sensor_packet_t; //musi być taka sama na odbiorniku (ESP32)