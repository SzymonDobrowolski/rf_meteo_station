/*
 * KOMPLETNY PLIK DIAGNOSTYCZNY DLA ATTINY414 + NRF24L01
 * Funkcje: Nadawanie + Wykrywanie Resetu Radia (Brown-out detection)
 */

#define F_CPU 20000000UL // 20 MHz

#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#include <string.h>

#include "SPI.h"
#include "NRF24L01.h"
#include "TWI.h"
#include "BME280.h"

#define LED_PIN PIN6_bm
#define SCL_PIN PIN3_bm
#define SDA_PIN PIN2_bm

// Makra LED
#define LED_ON()   (PORTA.OUTSET = LED_PIN)
#define LED_OFF()  (PORTA.OUTCLR = LED_PIN)

// Funkcja pomocnicza do mrugania (ilość razy, czas ms)
void blink_led(uint8_t count, uint16_t ms) {
    for (uint8_t i = 0; i < count; i++) {
        LED_ON();
        for(uint16_t d=0; d<ms; d++) _delay_ms(1);
        LED_OFF();
        for(uint16_t d=0; d<ms; d++) _delay_ms(1);
    }
}

int main(void) {
    _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 0); 

    PORTA.DIRSET = LED_PIN | PIN4_bm | PIN5_bm; // LED, CSN, CE jako wyjścia
    LED_OFF();

    SPI_init();
    TWI_Init();
    NRF_init();
    
    NRF_set_tx_mode(); //ustawienie nrf jako nadajnik
    

    while (1) {
        sensor_packet_t pkt;
        pkt.temperature = 25.50;
        pkt.humidity    = 40.00;
        pkt.pressure    = 1000.00;
        NRF_write_reg(0x07, 0x70); 

        NRF_send_packet(&pkt); //wyślij dane
        blink_led(3, 100); //sygnalizacja wysłania danych
        _delay_ms(10000); //odczekaj sekundę przed kolejnym pomiarem
    }
}