#include <avr/io.h>
#include <util/delay.h>

#include "TWI.h"
#include "SPI.h"
#include "NRF24L01.h"

// Makra LED
#define LED_HIGH()  (PORTA.OUTSET = PIN6_bm)
#define LED_LOW()   (PORTA.OUTCLR = PIN6_bm)

int main(void) {
    _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 0); // 20 MHz

    TWI_Init();
    SPI_init();
    
    // Inicjalizacja Pinów
    PORTA.DIRSET = PIN6_bm; 
    
    // Inicjalizacja Radia
    NRF_init();
    NRF_set_tx_mode();

    // ==========================================
    // TEST SPRZĘTOWY (CZY ATtiny WIDZI RADIO?)
    // ==========================================
    
    uint8_t check_ch = NRF_read_reg(0x05); // Odczytaj kanał (powinno być 76)

    if (check_ch == 76) {
        // SUKCES: Radio podłączone poprawnie
        // 3 wolne mrugnięcia na powitanie
        for(int i=0; i<3; i++) {
            LED_HIGH(); _delay_ms(500);
            LED_LOW();  _delay_ms(500);
        }
    } else {
        // BŁĄD KRYTYCZNY: Brak komunikacji z NRF!
        // check_ch to pewnie 0xFF (255) lub 0x00.
        // Mrugaj bardzo szybko w nieskończoność - STÓJ TUTAJ
        while(1) {
            LED_HIGH(); _delay_ms(50);
            LED_LOW();  _delay_ms(50);
        }
    }
    // ==========================================

    while (1) {
        sensor_packet_t pkt;
        pkt.temperature = 12.34;
        pkt.humidity    = 56.78;
        pkt.pressure    = 999.99;

        // Wyślij
        NRF_send_packet(&pkt);
        
        // Diagnostyka statusu (tak jak wcześniej)
        _delay_ms(10);
        uint8_t status = NRF_read_reg(0x07);
        
        // Dodatkowe zabezpieczenie: ignoruj status 0xFF (błąd SPI)
        if (status == 0xFF) {
             // Jeśli podczas pracy urwie się kabel -> szybkie mruganie
             for(int i=0; i<10; i++) { LED_HIGH(); _delay_ms(20); LED_LOW(); _delay_ms(20); }
        }
        else if (status & (1 << 5)) {
            // Prawdziwy sukces
            LED_HIGH(); _delay_ms(1000); LED_LOW();
        } 
        else {
             // Cisza/Błąd wysyłania
             LED_HIGH(); _delay_ms(20); LED_LOW();
        }
        
        _delay_ms(1000);
    }
}