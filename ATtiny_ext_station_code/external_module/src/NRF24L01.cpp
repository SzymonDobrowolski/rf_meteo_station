#include <avr/io.h>
#include <util/delay.h>

#include "Config.h"
#include "SPI.h"
#include "NRF24L01.h"

//Tutaj są makra do włączania/wyłączania pinów bo sam już się myliłem jak pisałem po portch
#define NRF_CE_HIGH()   (SPI_PORT.OUTSET = NRF_CE_PIN)
#define NRF_CE_LOW()    (SPI_PORT.OUTCLR = NRF_CE_PIN)

#define NRF_CSN_HIGH()  (SPI_PORT.OUTSET = NRF_CSN_PIN)
#define NRF_CSN_LOW()   (SPI_PORT.OUTCLR = NRF_CSN_PIN)

uint8_t NRF_read_reg(uint8_t reg)
{
    NRF_CSN_LOW();
    SPI_transfer(reg & 0x1F);      //0x1F = maska READ
    uint8_t v = SPI_transfer(0xFF);
    NRF_CSN_HIGH();
    return v;
}
void NRF_write_reg(uint8_t reg, uint8_t value)
{
    NRF_CSN_LOW();
    SPI_transfer(0x20 | (reg & 0x1F));   //0x20 → WRITE
    SPI_transfer(value);
    NRF_CSN_HIGH();
}

void NRF_set_tx_mode(void)
{
    // 1. Konfiguracja: PWR_UP=1, PRIM_RX=0 (Nadajnik)
    NRF_write_reg(0x00, 0x0A); 

    // 2. Ustawienia radiowe (Muszą być identyczne jak w ESP32)
    NRF_write_reg(0x05, Channel);     // Kanał 76 (2476 MHz)
    NRF_write_reg(0x06, NRF_SPEED);   // 2 Mbps, 0 dBm

    // 3. Włączenie Auto-ACK (Ważne dla stabilności)
    NRF_write_reg(0x01, 0x01);   // EN_AA na Pipe 0
    NRF_write_reg(0x02, 0x01);   // EN_RXADDR na Pipe 0
    NRF_write_reg(0x04, 0x2F);   // Retransmisja: co 750us, 15 razy

    // 4. Ustawienie adresu TX (Dokąd wysyłamy) - "AT414"
    NRF_CSN_LOW();
    SPI_transfer(0x20 | 0x10);   // Rejestr TX_ADDR
    SPI_SEND_ADDRESS();
    NRF_CSN_HIGH();

    // 5. Ustawienie adresu RX_P0 (Żeby odebrać potwierdzenie ACK od ESP)
    // Musi być taki sam jak TX_ADDR!
    NRF_CSN_LOW();
    SPI_transfer(0x20 | 0x0A);   // Rejestr RX_ADDR_P0
    SPI_SEND_ADDRESS();
    NRF_CSN_HIGH();

    // CE niski - radio w trybie Standby-I, gotowe do strzału (funkcja send_packet zrobi impuls)
    NRF_CE_LOW();
}

void NRF_set_rx_mode(void) //a tu odbior, no i trzeba będzie to przepisać dla ESP-IDF
{
    NRF_write_reg(0x00,(1 << 1) | (1 << 0)); // PWR_UP  // PRIM_RX = RX

    NRF_write_reg(0x05, Channel);  // ten sam kanał
    NRF_write_reg(0x06, 0x0F);

    // RX address same as TX
    NRF_CSN_LOW();
    SPI_transfer(0x20 | 0x0A);
    SPI_SEND_ADDRESS();
    NRF_CSN_HIGH();

    NRF_CE_HIGH();  // włącz odbiór
}

void NRF_send_packet(sensor_packet_t *pkt)
{
    // --- KROK 1: ODBLOKOWANIE RADIA ---
    // Zapisz 1 na bitach: RX_DR(6), TX_DS(5), MAX_RT(4) w rejestrze STATUS (0x07)
    // To kasuje flagi przerwań. Jeśli MAX_RT wisiało, to teraz zniknie.
    NRF_write_reg(0x07, 0x70); 

    // Opcjonalnie: Wyczyść bufor TX, jeśli coś tam utknęło (FLUSH_TX = 0xE1)
    NRF_CSN_LOW();
    SPI_transfer(0xE1);
    NRF_CSN_HIGH();
    // ----------------------------------

    NRF_CE_LOW();
    NRF_CSN_LOW();

    SPI_transfer(0xA0);  // W_TX_PAYLOAD

    uint8_t *p = (uint8_t*)pkt;
    for(uint8_t i = 0; i < sizeof(sensor_packet_t); i++)
        SPI_transfer(p[i]);

    NRF_CSN_HIGH();

    // Impuls CE wysyłający pakiet
    NRF_CE_HIGH();
    _delay_us(20); // Minimum 10us
    NRF_CE_LOW();
}

void NRF_init(void)
{
    //CSN, CE jako wyjścia
    SPI_PORT.DIRSET = NRF_CSN_PIN | NRF_CE_PIN;
    NRF_CE_LOW();
    NRF_CSN_HIGH();
    SPI_init();     //inicjalizacja SPI
    // Wyłącz urządzenie
    NRF_write_reg(0x00, 0x00);   //CONFIG = 0
}
