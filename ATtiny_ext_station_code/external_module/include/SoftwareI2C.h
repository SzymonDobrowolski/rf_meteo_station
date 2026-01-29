#include <avr/io.h>
#include <stdint.h>

/* * Inicjalizacja magistrali (Software I2C).
 * Ustawia piny SDA i SCL w stan wysoki (Input + Pull-up).
 */
void TWI_Init(void);

/* * Generuje sygnał START i wysyła adres urządzenia.
 * addressrw: 7-bitowy adres przesunięty w lewo + bit R/W (0=Write, 1=Read)
 */
void TWI_Start(uint8_t addressrw);

/* * Generuje sygnał STOP (zwalnia magistralę).
 */
void TWI_Stop(void);

/* * Wysyła jeden bajt danych do urządzenia Slave.
 */
void TWI_Write(uint8_t data);

/* * Odczytuje jeden bajt danych od Slave i wysyła ACK.
 * Używane, gdy chcemy czytać kolejne bajty.
 */
uint8_t TWI_Read_ACK(void);

/* * Odczytuje jeden bajt danych od Slave i wysyła NACK.
 * Używane przy ostatnim bajcie, aby zakończyć transmisję.
 */
uint8_t TWI_Read_NACK(void);