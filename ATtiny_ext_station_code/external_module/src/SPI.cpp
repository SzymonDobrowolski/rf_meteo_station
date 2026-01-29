#include <avr/io.h>
#include <util/delay.h>

#include "Config.h"

void SPI_init(void)
{
    //PA1 = MOSI → output *zgodnie ze schematem naszym*
    //PA3 = SCK  → output
    SPI_PORT.DIRSET = MOSI_PIN | SCK_PIN;
    //PA2 = MISO → input
    SPI_PORT.DIRCLR = MISO_PIN;
    //Enable SPI in master mode, clock prescaler = /4
    SPI0.CTRLA = SPI_ENABLE_bm | SPI_MASTER_bm | SPI_PRESC_DIV4_gc; //tutaj można dać większy przeskaler, jak 5Mhz będzie za dużo jak na nasze sciezki
    //MODE 0 (CPOL=0, CPHA=0)
    SPI0.CTRLB = 0; //tryb 0, może zadziała z naszym NRF????
}
uint8_t SPI_transfer(uint8_t data)
{
    SPI0.DATA = data;
    while(!(SPI0.INTFLAGS & SPI_IF_bm));
    return SPI0.DATA;
}