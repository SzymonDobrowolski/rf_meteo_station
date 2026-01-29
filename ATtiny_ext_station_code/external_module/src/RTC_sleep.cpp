#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>

#include "Config.h"
#include "RTC_sleep.h"

void RTC_Init(void) {
    //Czekaj aż RTC będzie gotowe
    while (RTC.STATUS > 0);

    RTC.CLKSEL = RTC_CLKSEL_INT1K_gc; // 1.024 kHz (wewnętrzny 32k / 32)
    while (RTC.STATUS > 0);

    //Ustaw okres i włącz licznik (PITEN)
    RTC.PITCTRLA = SLEEP_SCALER | RTC_PITEN_bm; 

    //KLUCZOWE: WŁĄCZ PRZERWANIE OD PIT !!!
    //Bez tej linii procesor nigdy się nie obudzi!
    RTC.PITINTCTRL = RTC_PI_bm; 
}

void GoToSleep(void) {
    //Ustaw tryb Power Down (Najgłębszy sen)
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    
    //Odblokuj możliwość uśpienia
    sleep_enable();
    
    //Włącz przerwania (bez tego budzik nie zadziała!)
    sei();
    
    //Dobranoc (Procesor zatrzymuje się tutaj)
    sleep_cpu();
    
    // ... TU SIĘ BUDZI PO PRZERWANIU ...
    
    //Zablokuj sen (dla bezpieczeństwa)
    sleep_disable();
}