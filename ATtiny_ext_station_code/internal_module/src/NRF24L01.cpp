#include <avr/io.h>
#include <util/delay.h>

#include "SPI.h"
#include "NRF24L01.h"

//Tutaj są makra do włączania/wyłączania pinów bo sam już się myliłem jak pisałem po portch
#define NRF_CE_HIGH()   (PORTA.OUTSET = PIN5_bm)
#define NRF_CE_LOW()    (PORTA.OUTCLR = PIN5_bm)

#define NRF_CSN_HIGH()  (PORTA.OUTSET = PIN4_bm)
#define NRF_CSN_LOW()   (PORTA.OUTCLR = PIN4_bm)

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

void NRF_set_tx_mode(void) //wysyłanie ze znaczikiem AT414 taki ot bajer fajny
{
    //CONFIG:
    //Bit 0: PRIM_RX = 0 → TX mode 
    //Bit 1: PWR_UP = 1
    NRF_write_reg(0x00, (1 << 1)); 

    NRF_write_reg(0x05, 76);     //RF channel = 2476 MHz Ważne ten sam kanał w odbiorniku!!!!
    NRF_write_reg(0x06, 0x0F);   //0 dBm, 2 Mbps

    // TX address (5 bytes)
    NRF_CSN_LOW();
    SPI_transfer(0x20 | 0x10);   // WRITE_REGISTER + TX_ADDR
    SPI_transfer('A');
    SPI_transfer('T');
    SPI_transfer('4');
    SPI_transfer('1');
    SPI_transfer('4');
    NRF_CSN_HIGH();

    NRF_CE_LOW();
}

void NRF_set_rx_mode(void) //a tu odbior, no i trzeba będzie to przepisać dla ESP-IDF
{
    NRF_write_reg(0x00,(1 << 1) | (1 << 0)); // PWR_UP  // PRIM_RX = RX

    NRF_write_reg(0x05, 76);  // ten sam kanał
    NRF_write_reg(0x06, 0x0F);

    // RX address same as TX
    NRF_CSN_LOW();
    SPI_transfer(0x20 | 0x0A);
    SPI_transfer('A');
    SPI_transfer('T');
    SPI_transfer('4');
    SPI_transfer('1');
    SPI_transfer('4');
    NRF_CSN_HIGH();

    NRF_CE_HIGH();  // włącz odbiór
}

void NRF_send_packet(sensor_packet_t *pkt)
{
    NRF_CE_LOW();
    NRF_CSN_LOW();

    SPI_transfer(0xA0);  //W_TX_PAYLOAD

    uint8_t *p = (uint8_t*)pkt;
    for(uint8_t i = 0; i < sizeof(sensor_packet_t); i++)
        SPI_transfer(p[i]);

    NRF_CSN_HIGH();

    //impuls CE wysyłający pakiet
    NRF_CE_HIGH();
    _delay_us(20);
    NRF_CE_LOW();
}

void NRF_init(void)
{
    //CSN, CE jako wyjścia
    PORTA.DIRSET = PIN4_bm | PIN5_bm;
    NRF_CE_LOW();
    NRF_CSN_HIGH();
    SPI_init();     //inicjalizacja SPI
    // Wyłącz urządzenie
    NRF_write_reg(0x00, 0x00);   //CONFIG = 0
}
