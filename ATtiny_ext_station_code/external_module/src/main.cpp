#include <avr/io.h>
#include <util/delay.h>

#include "TWI.h"
#include "SPI.h"
#include "BME280.h"
#include "NRF24L01.h"


#define MAIN_CLK 20000000 //20MHz

//makra do LED
#define LED_HIGH()  (PORTA.OUTSET = PIN6_bm)
#define LED_LOW()   (PORTA.OUTCLR = PIN6_bm)

int main(void) {
    // 1. KONFIGURACJA ZEGARA
    // ATtiny414 ma zabezpieczone rejestry kluczowe (CCP - Configuration Change Protection).
    // Aby zmienić ustawienia zegara, musimy użyć makra _PROTECTED_WRITE.

    // Wyłączamy preskaler zegara (MCLKCTRLB = 0), 
    // dzięki temu procesor działa na pełnym 20 MHz. ale pozniej mozna zmienic na mniejszy zegar
    _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 0);

    // 2. KONFIGURACJA PINÓW
    // W nowej serii AVR (0/1/2-series) używamy struktur PORTx.
    // DIRSET to rejestr, który ustawia bity na 1 (wyjście) bez ruszania innych.
    // PIN1_bm to "Bit Mask" dla pinu 1 (czyli wartość 0b00000010).

    TWI_Init();
    SPI_init();

    NRF_init();

    //BME280_Init();
    //BME280_ReadCalibration(); //pamietac o tym przy inicjacji

    PORTA.DIRSET = PIN6_bm;
    LED_LOW();

    // 3. GŁÓWNA PĘTLA
    while (1) {
                                    // OUTTGL (Output Toggle) to sprzętowa funkcja zmiany stanu na przeciwny.
                                    // Nie musisz sprawdzać czy jest 1 czy 0, procesor sam to odwróci.
                                    //PORTA.OUTTGL = PIN7_bm;
        
        ///odczytywane dane, ale trzeba je potem skompensowac
        
        //uint8_t data[8];
       // BME280_ReadBytes(0xF7, data, 8);

        // ciśnienie (20 bit)
        //uint32_t raw_press = (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);

        // temperatura (20 bit)
        //uint32_t raw_temp  = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);

        // wilgotność (16 bit)
        //uint32_t raw_hum   = (data[6] << 8)  | (data[7]);
        

        //Tutaj jest przyklzd kompensacji
        
       // int32_t temp_hundredths = BME280_Compensate_T(raw_temp); //0.01°C
       // uint32_t pressure_pa    = BME280_Compensate_P(raw_press); //Pa
       // uint32_t hum_x1024      = BME280_Compensate_H(raw_hum);  //humidity * 1024

        //konwersje użytkowe, zapis do strukrury 
        sensor_packet_t pkt;//nie wiem flash prawie cały zawalony, ale wysyłamy za jednym zamachem chociaż to ma 12 bajtów, więc nie wiem
        pkt.temperature = 36.0;//temp_hundredths / 100.0f;
        pkt.humidity    = 12.4;//hum_x1024 / 1024.0f;
        pkt.pressure    = 3.76;//pressure_pa / 100.0f;
        
        LED_HIGH();//do debugowania, czy jest cos wysylane
        NRF_send_packet(&pkt);
        _delay_ms(5000);
        LED_LOW();//do debugowania, czy jest cos wysylane
        
        _delay_ms(5000); //Tx co 30 sekund
    }

    return 0;
}
