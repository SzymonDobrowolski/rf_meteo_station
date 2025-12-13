#include <avr/io.h>
#include <util/delay.h>

typedef struct __attribute__((packed)) {//to jest struktura 12 bajtowa, nrf może przyjąc 32, dzieki struktorze packet wyslemy cale 12 bajtow za jednym zamachem
    int32_t temp_hundredths;
    uint32_t pressure_pa;
    uint32_t hum_x1024;
} sensor_packet_t;

uint8_t NRF_read_reg(uint8_t reg);
void NRF_write_reg(uint8_t reg, uint8_t value);
void NRF_set_tx_mode(void); //wysyłanie ze znaczikiem AT414 taki ot bajer fajny
void NRF_set_rx_mode(void);
void NRF_send_packet(sensor_packet_t *pkt);
void NRF_init(void);
