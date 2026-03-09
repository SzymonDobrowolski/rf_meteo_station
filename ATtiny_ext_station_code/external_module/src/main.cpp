/*
 * KOMPLETNY PLIK DIAGNOSTYCZNY DLA ATTINY414 + NRF24L01
 * Funkcje: Nadawanie + Wykrywanie Resetu Radia (Brown-out detection)
 */

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <string.h>

#include "Config.h"
#include "SPI.h"
#include "NRF24L01.h"
#include "SoftwareI2C.h"
#include "BME280.h"
#include "RTC_sleep.h"

// Makra LED
#define LED_ON()   (LED_PORT.OUTSET = LED_PIN)
#define LED_OFF()  (LED_PORT.OUTCLR = LED_PIN)

// Funkcja pomocnicza do mrugania (ilość razy, czas ms)
void blink_led(uint8_t count, uint16_t ms) {
    for (uint8_t i = 0; i < count; i++) {
        LED_ON();
        for(uint16_t d=0; d<ms; d++) _delay_ms(1);
        LED_OFF();
        for(uint16_t d=0; d<ms; d++) _delay_ms(1);
    }
}

    // --- FUNKCJA DO POMIARU BATERII (VDD) ---
uint16_t measure_battery_mv(void) {
    // 1. Skonfiguruj wewnętrzne źródło odniesienia (VREF) na 1.5V (standard dla ATtiny414)
    VREF.CTRLA = VREF_ADC0REFSEL_1V5_gc;
    
    // 2. Referencja dla ADC to napięcie zasilania (VDD). Preskaler /16.
    ADC0.CTRLC = ADC_PRESC_DIV16_gc | ADC_REFSEL_VDDREF_gc;
    
    // 3. Jako wejście dla ADC wybieramy wewnętrzne napięcie (INTREF)
    ADC0.MUXPOS = ADC_MUXPOS_INTREF_gc;
    
    // 4. Włącz ADC
    ADC0.CTRLA = ADC_ENABLE_bm;
    
    // 5. Odrzuć pierwszy pomiar (zalecane przy starcie ADC i zmianie VREF)
    ADC0.COMMAND = ADC_STCONV_bm;
    while (!(ADC0.INTFLAGS & ADC_RESRDY_bm));
    ADC0.INTFLAGS = ADC_RESRDY_bm; // Czyszczenie flagi
    
    // 6. Właściwy pomiar
    ADC0.COMMAND = ADC_STCONV_bm;
    while (!(ADC0.INTFLAGS & ADC_RESRDY_bm));
    ADC0.INTFLAGS = ADC_RESRDY_bm;
    uint16_t adc_res = ADC0.RES;
    
    // 7. Wyłącz ADC przed pójściem spać, żeby oszczędzać prąd
    ADC0.CTRLA &= ~ADC_ENABLE_bm;
    
    // 8. Obliczenia:
    // Wzór: ADC = (Vref / VDD) * 1024
    // Przekształcamy to do postaci miliwoltów: VDD_mV = (1.5V * 1024 * 1000) / ADC
    // 1500 * 1024 = 1536000
    if (adc_res == 0) return 0;
    return (uint16_t)(1536000UL / adc_res);
}

int main(void) {
    _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 0); 

    LED_PORT.DIRSET = LED_PIN; // LED jako wyjście
    LED_OFF();

    SPI_init();
    TWI_Init();
    NRF_init();
    RTC_Init();

    sei();//włączamy globalne przerwania
    
    NRF_set_tx_mode(); //ustawienie nrf jako nadajnik
    
    BME280_Init();
    BME280_ReadCalibration();
    sensor_packet_t pkt;

    while (1) {
        uint8_t data[8];
        BME280_ReadBytes(0xF7, data, 8);

        // ciśnienie (20 bit)
        uint32_t raw_press = ((uint32_t)data[0] << 12) | ((uint32_t)data[1] << 4) | ((uint32_t)data[2] >> 4);

        // temperatura (20 bit)
        uint32_t raw_temp  = ((uint32_t)data[3] << 12) | ((uint32_t)data[4] << 4) | ((uint32_t)data[5] >> 4);

        // wilgotność (16 bit)
        uint32_t raw_hum   = ((uint32_t)data[6] << 8)  | ((uint32_t)data[7]);

        NRF_write_reg(0x00, 0x0A); //na nowo włączamy zasilanie radia na czas transmisji
        _delay_ms(10); //uśpienie potrzebne na uruchomienie się oscylatora układu

        //Tutaj jest przyklzd kompensacji, potem wstawimy prawdziwe funkcje
        
        pkt.temp_hundredths = BME280_Compensate_T(raw_temp); //0.01°C
        pkt.pressure_pa    = BME280_Compensate_P(raw_press); //Pa
        pkt.hum_x1024      = BME280_Compensate_H(raw_hum);
        pkt.vcc_mv         = measure_battery_mv(); //pomiar napięcia baterii w mV
        
        NRF_write_reg(0x07, 0x70); //wyczyszczenie flag statusu

        NRF_send_packet(&pkt); //wyślij dane
        //blink_led(3, 100); //sygnalizacja wysłania danych
        _delay_ms(10); //odkomentowac w przypaku zakomentowania blink_led();
        NRF_write_reg(0x00,0x00); //całkowite uśpienie radia nrf na czas braku transmisji

        //_delay_ms(5000); //uśpienie tylko w trakcie testu komunikacji
        //zamiast delay go to sleep
        GoToSleep(); 

    }
}

// Obsługa przerwania - budzik zadzwonił
ISR(RTC_PIT_vect) {
    // Wyczyść flagę przerwania, żeby nie wpaść w pętlę
    RTC.PITINTFLAGS = RTC_PI_bm;
}