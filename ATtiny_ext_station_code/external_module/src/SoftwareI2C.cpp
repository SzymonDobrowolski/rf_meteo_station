#include "SoftwareI2C.h" // Upewnij się, że masz plik TWI.h z deklaracjami funkcji
#include <avr/io.h>
#include <util/delay.h>
#include "Config.h"

// Opóźnienie dla ~100kHz (4-5us półokresu)
#define TWI_DELAY() _delay_us(4)

// --- MAKRA STERUJĄCE (SYMULACJA OPEN-DRAIN) ---
// I2C nigdy nie wymusza stanu wysokiego (VCC). 
// Stan wysoki to wejście (INPUT) + Pull-up. Stan niski to wyjście (OUTPUT) + 0.

void SCL_H(void) { I2C_PORT.DIRCLR = SCL_PIN; } // Input (Pull-up ciągnie do góry)
void SCL_L(void) { I2C_PORT.OUTCLR = SCL_PIN; I2C_PORT.DIRSET = SCL_PIN; } // Output Low

void SDA_H(void) { I2C_PORT.DIRCLR = SDA_PIN; }
void SDA_L(void) { I2C_PORT.OUTCLR = SDA_PIN; I2C_PORT.DIRSET = SDA_PIN; }

uint8_t SDA_Read(void) { return (I2C_PORT.IN & SDA_PIN) ? 1 : 0; }

// --- FUNKCJA WEWNĘTRZNA (Nie widoczna w .h) ---
// Wysyła 8 bitów bez generowania Start/Stop
void _soft_write_byte(uint8_t data) {
    for(uint8_t i=0; i<8; i++) {
        if(data & 0x80) SDA_H(); else SDA_L();
        data <<= 1;
        TWI_DELAY();
        SCL_H(); TWI_DELAY(); // Zegar góra (Slave czyta)
        SCL_L(); TWI_DELAY(); // Zegar dół
    }
    
    // Odbierz ACK/NACK (Clock Pulse)
    SDA_H(); // Zwolnij linię dla Slave'a
    TWI_DELAY();
    SCL_H(); TWI_DELAY(); // Tu Slave ustawia ACK
    // (W tej prostej implementacji ignorujemy czy Slave dał ACK, bo return jest void)
    SCL_L(); TWI_DELAY();
}

// --- FUNKCJA WEWNĘTRZNA ---
// Czyta 8 bitów
uint8_t _soft_read_byte(uint8_t ack) {
    uint8_t data = 0;
    SDA_H(); // Upewnij się, że linia jest wejściem
    
    for(uint8_t i=0; i<8; i++) {
        data <<= 1;
        TWI_DELAY();
        SCL_H(); TWI_DELAY();
        if(SDA_Read()) data |= 1;
        SCL_L(); TWI_DELAY();
    }
    
    // Wyślij ACK (Low) lub NACK (High)
    if(ack) SDA_L(); else SDA_H();
    TWI_DELAY();
    SCL_H(); TWI_DELAY();
    SCL_L(); TWI_DELAY();
    SDA_H(); // Zwolnij linię na koniec
    
    return data;
}

// ==========================================
// PUBLICZNE API (Zgodne ze starym TWI.h)
// ==========================================

void TWI_Init(void)
{
    // Ustaw piny w stan bezpieczny (Input/High-Z)
    I2C_PORT.DIRCLR = SDA_PIN | SCL_PIN;
    I2C_PORT.OUTCLR = SDA_PIN | SCL_PIN; // Przygotuj 0 w rejestrze OUT
}

void TWI_Start(uint8_t addressrw)
{
    // Sekwencja START: SDA High->Low kiedy SCL jest High
    SDA_H(); SCL_H(); TWI_DELAY();
    SDA_L(); TWI_DELAY();
    SCL_L(); TWI_DELAY();
    
    // W sprzętowym TWI, funkcja Start od razu wysyła adres.
    // Musimy to zasymulować ręcznie:
    _soft_write_byte(addressrw);
}

void TWI_Stop(void)
{
    // Sekwencja STOP: SDA Low->High kiedy SCL jest High
    SDA_L(); TWI_DELAY();
    SCL_H(); TWI_DELAY();
    SDA_H(); TWI_DELAY();
}

void TWI_Write(uint8_t data)
{
    _soft_write_byte(data);
}

uint8_t TWI_Read_ACK(void)
{
    return _soft_read_byte(1); // 1 = Wyślij ACK
}

uint8_t TWI_Read_NACK(void)
{
    return _soft_read_byte(0); // 0 = Wyślij NACK (Koniec transmisji)
}