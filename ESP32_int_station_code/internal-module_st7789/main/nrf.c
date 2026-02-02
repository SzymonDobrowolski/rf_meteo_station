#include "spi_init.h"
#include "driver/spi_master.h"
#include "nrf.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"

//#include "Config.h"

static const char *TAG = "NRF";

void nrf_write_register(spi_device_handle_t spi, uint8_t reg, uint8_t data)
{
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans)); 
    
    // DMA wymaga pamięci w internal RAM
    uint8_t *tx = heap_caps_malloc(2, MALLOC_CAP_DMA);
    if (!tx) return; // Zabezpieczenie przed brakiem pamięci

    tx[0] = CMD_W_REGISTER | (reg & 0x1F); 
    tx[1] = data;

    trans.length = 16;   
    trans.tx_buffer = tx; 
    trans.rx_buffer = NULL;     

    spi_device_polling_transmit(spi, &trans);
    free(tx);
}

void nrf_write_buf(spi_device_handle_t spi, uint8_t reg, const uint8_t *data, uint8_t len) {
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    
    uint8_t *tx = heap_caps_malloc(len + 1, MALLOC_CAP_DMA);
    if (!tx) return;

    tx[0] = CMD_W_REGISTER | (reg & 0x1F);
    memcpy(tx + 1, data, len);

    trans.length = 8 * (len + 1);
    trans.tx_buffer = tx;
    trans.rx_buffer = NULL;

    spi_device_polling_transmit(spi, &trans);
    free(tx);
}

uint8_t nrf_read_register(spi_device_handle_t spi, uint8_t reg)
{
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));

    uint8_t *tx = heap_caps_malloc(2, MALLOC_CAP_DMA);
    uint8_t *rx = heap_caps_malloc(2, MALLOC_CAP_DMA);
    if (!tx || !rx) { // Proste zabezpieczenie
        if(tx) free(tx);
        if(rx) free(rx);
        return 0;
    }

    tx[0] = 0x00 | (reg & 0x1F); 
    tx[1] = 0xFF;                

    trans.length = 16;      
    trans.tx_buffer = tx;  
    trans.rx_buffer = rx;  

    spi_device_polling_transmit(spi, &trans);
    
    uint8_t result = rx[1];
    free(tx);
    free(rx);
    return result;
}

void nrf_read_buf(spi_device_handle_t spi, uint8_t cmd, uint8_t *buffer, uint8_t len) {
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    
    uint8_t *tx = heap_caps_malloc(len + 1, MALLOC_CAP_DMA);
    uint8_t *rx = heap_caps_malloc(len + 1, MALLOC_CAP_DMA);
    if (!tx || !rx) {
        if(tx) free(tx);
        if(rx) free(rx);
        return;
    }
    
    memset(tx, 0, len + 1);
    memset(rx, 0, len + 1);
    tx[0] = cmd; 

    trans.length = 8 * (1 + len);
    trans.tx_buffer = tx;
    trans.rx_buffer = rx;

    spi_device_polling_transmit(spi, &trans);

    memcpy(buffer, &rx[1], len);

    free(tx);
    free(rx);
}

void nrf_init(spi_device_handle_t *spi_handle_ptr)
{
    esp_err_t ret;
    spi_device_interface_config_t nrfcfg = {
        .clock_speed_hz = 4*1000*1000,   
        .mode = 0,                       
        .spics_io_num = NRF_CSN_PIN,     
        .queue_size = 7                  
    };

    // Teraz 'spi_handle_ptr' jest rozpoznawany poprawnie
    ret = spi_bus_add_device(VSPI_HOST, &nrfcfg, spi_handle_ptr);
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NRF Initialized");

    // Teraz możemy stworzyć lokalną zmienną 'spi', bo argument nazywa się inaczej
    spi_device_handle_t spi = *spi_handle_ptr;

    gpio_reset_pin(NRF_CE_PIN); 
    gpio_set_direction(NRF_CE_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(NRF_CE_PIN, 0);

    // Konfiguracja rejestrów (używamy lokalnego 'spi')
    nrf_write_register(spi, REG_RF_CH, Channel); 
    nrf_write_register(spi, REG_RX_PW_P0, sizeof(SensorData)); 
    nrf_write_register(spi, REG_RF_SETUP, NRF_SPEED); 
    nrf_write_register(spi, REG_EN_AA, 0x01);     
    nrf_write_register(spi, REG_EN_RXADDR, 0x01); 
    
    // Adres
    const uint8_t rx_addr[] = RX_ADDRESS_VAL; 
    nrf_write_buf(spi, REG_RX_ADDR_P0, rx_addr, 5); 

    // Power UP + RX Mode
    nrf_write_register(spi, REG_CONFIG, 0x0B); 
    
    vTaskDelay(pdMS_TO_TICKS(10));
    
    gpio_set_level(NRF_CE_PIN, 1);
}

bool nrf_receive_data(spi_device_handle_t spi, SensorData *data_out) {
    uint8_t status = nrf_read_register(spi, REG_STATUS);

    // Sprawdzenie bitu RX_DR (Data Ready) - bit 6
    if (status & (1 << 6)) {
        // Czytamy dokładnie tyle bajtów, ile ma struktura
        uint8_t raw_buffer[sizeof(SensorData)];
        
        nrf_read_buf(spi, CMD_R_RX_PAYLOAD, raw_buffer, sizeof(SensorData));
        
        // Reset flagi przerwania (zapis 1 czyści bit)
        nrf_write_register(spi, REG_STATUS, (1 << 6));

        memcpy(data_out, raw_buffer, sizeof(SensorData));
        return true;
    }
    return false;
}