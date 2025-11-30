#include <avr/io.h>
#include <util/delay.h>

void TWI_Init();

void TWI_Start(uint8_t addressrw); //7 bitowy addr + 1 bit RW

void TWI_Stop(void);

void TWI_Write(uint8_t data);

uint8_t TWI_Read_ACK();

uint8_t TWI_Read_NACK();