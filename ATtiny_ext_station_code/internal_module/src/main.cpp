#include <avr/io.h>
#include <util/delay.h>

#include "TWI.h"
#include "BME280.h"


#define MAIN_CLK 20000000 //20MHz

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

    PORTA.DIRSET = PIN7_bm; // Ustaw PA1 jako WYJŚCIE
    PORTA.OUTSET = PIN7_bm; //Ustaw 1 na PA1

    TWI_Init();

    BME280_Init();
    BME280_ReadCalibration(); //pamietac o tym przy inicjacji

    // 3. GŁÓWNA PĘTLA
    while (1) {
        // OUTTGL (Output Toggle) to sprzętowa funkcja zmiany stanu na przeciwny.
        // Nie musisz sprawdzać czy jest 1 czy 0, procesor sam to odwróci.
        //PORTA.OUTTGL = PIN7_bm;
        
        ///odczytywac dane, ale trzeba je potem skompensowac
        
        uint8_t data[8];
        BME280_ReadBytes(0xF7, data, 8);

        // ciśnienie (20 bit)
        uint32_t raw_press = (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);

        // temperatura (20 bit)
        uint32_t raw_temp  = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);

        // wilgotność (16 bit)
        uint32_t raw_hum   = (data[6] << 8)  | (data[7]);
        

        //Tutaj jest przyklzd kompensacji
        
        uint8_t data[8];
        BME280_ReadBytes(0xF7, data, 8);

        int32_t raw_pres = ( (int32_t)data[0] << 12 ) | ( (int32_t)data[1] << 4 ) | ( data[2] >> 4 );
        int32_t raw_temp = ( (int32_t)data[3] << 12 ) | ( (int32_t)data[4] << 4 ) | ( data[5] >> 4 );
        int32_t raw_hum  = ( (int32_t)data[6] << 8 )  | ( (int32_t)data[7] );

        //kompensacja
        int32_t temp_hundredths = BME280_Compensate_T(raw_temp); //0.01°C
        uint32_t pressure_pa    = BME280_Compensate_P(raw_pres); //Pa
        uint32_t hum_x1024      = BME280_Compensate_H(raw_hum);  //humidity * 1024

        //konwersje użytkowe
        float temperature_C = temp_hundredths / 100.0f;
        float humidity_pct  = hum_x1024 / 1024.0f;
        float pressure_hPa  = pressure_pa / 100.0f;
        
        //_delay_ms(500); // Czekaj 500ms
    }

    return 0;
}
