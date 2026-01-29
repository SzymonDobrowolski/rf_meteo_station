#include <avr/io.h>
#include <util/delay.h>

uint8_t NRF_read_reg(uint8_t reg);
void NRF_write_reg(uint8_t reg, uint8_t value);
void NRF_set_tx_mode(void); //wysyłanie ze znaczikiem AT414 taki ot bajer fajny
void NRF_set_rx_mode(void);
void NRF_send_packet(sensor_packet_t *pkt);
void NRF_init(void);
