#include <avr/io.h>
#include <util/delay.h>

int main(void) {
    // 1. KONFIGURACJA ZEGARA
    // ATtiny414 ma zabezpieczone rejestry kluczowe (CCP - Configuration Change Protection).
    // Aby zmienić ustawienia zegara, musimy użyć makra _PROTECTED_WRITE.

    // Wyłączamy preskaler zegara (MCLKCTRLB = 0), 
    // dzięki temu procesor działa na pełnym 20 MHz.
    _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 0);

    // 2. KONFIGURACJA PINÓW
    // W nowej serii AVR (0/1/2-series) używamy struktur PORTx.
    // DIRSET to rejestr, który ustawia bity na 1 (wyjście) bez ruszania innych.
    // PIN1_bm to "Bit Mask" dla pinu 1 (czyli wartość 0b00000010).

    PORTA.DIRSET = PIN1_bm; // Ustaw PA1 jako WYJŚCIE

    // 3. GŁÓWNA PĘTLA
    while (1) {
        // OUTTGL (Output Toggle) to sprzętowa funkcja zmiany stanu na przeciwny.
        // Nie musisz sprawdzać czy jest 1 czy 0, procesor sam to odwróci.
        PORTA.OUTTGL = PIN1_bm;

        _delay_ms(500); // Czekaj 500ms
    }

    return 0;
}
