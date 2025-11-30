#include <TWI.h>
#include <avr/io.h>
#include <util/delay.h>

#define MAIN_CLK 20000000 //20MHz

#define TWI_CLK 100000 //100KHz

void TWI_Init()
{
    TWI0_MBAUD = (MAIN_CLK/(2*TWI_CLK) - 5); //TWI na 100kHz

    TWI0.MCTRLA = TWI_ENABLE_bm; //wlacz tryb master 
    TWI0.MSTATUS = TWI_BUSSTATE_IDLE_gc; //tryb idle
}

void TWI_Start(uint8_t addressrw) //7 bitowy addr + 1 bit RW
{
    TWI0.MADDR = addressrw;
    while (!(TWI0.MSTATUS & (TWI_WIF_bm | TWI_RIF_bm))); //czekaj na zakonczenie transmisji
    
}

void TWI_Stop(void)
{
    TWI0.MCTRLB = TWI_MCMD_STOP_gc; //B, dlatego, zeby utrydnic microchip postanowil, ze dane inicjalizacyjne beda w A, natomiast sterujace w B
}

void TWI_Write(uint8_t data)
{
    TWI0.MDATA = data;
    while (!(TWI0.MSTATUS & TWI_WIF_bm)); //oczekiwanie na zbuforowanie danych
}

uint8_t TWI_Read_ACK()
{
    TWI0.MCTRLB = TWI_ACKACT_ACK_gc | TWI_MCMD_RECVTRANS_gc;
    while (!(TWI0.MSTATUS & TWI_RIF_bm));
    return TWI0.MDATA;
}

uint8_t TWI_Read_NACK()
{
    TWI0.MCTRLB = TWI_ACKACT_NACK_gc | TWI_MCMD_RECVTRANS_gc;
    while (!(TWI0.MSTATUS & TWI_RIF_bm));
    return TWI0.MDATA;
}