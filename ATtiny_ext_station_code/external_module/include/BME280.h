#include <avr/io.h>
#include <util/delay.h>

//globalne zmienne do kalibracji pozniej sie zmieni na lokalne jak bedzie dzialac
static uint16_t dig_T1;
static int16_t  dig_T2;
static int16_t  dig_T3;

static uint16_t dig_P1;
static int16_t  dig_P2;
static int16_t  dig_P3;
static int16_t  dig_P4;
static int16_t  dig_P5;
static int16_t  dig_P6;
static int16_t  dig_P7;
static int16_t  dig_P8;
static int16_t  dig_P9;

static uint8_t  dig_H1;
static int16_t  dig_H2;
static uint8_t  dig_H3;
static int16_t  dig_H4;
static int16_t  dig_H5;
static int8_t   dig_H6;

// t_fine (potrzebne do kompensacji P i H)
static int32_t t_fine;

void BME280_WriteReg(uint8_t reg, uint8_t value);
uint8_t BME280_ReadReg(uint8_t reg);
void BME280_ReadBytes(uint8_t reg, uint8_t *buf, uint8_t len);
void BME280_Init(void);
void BME280_ReadCalibration(void);
int32_t BME280_Compensate_T(int32_t adc_T);
uint32_t BME280_Compensate_P(int32_t adc_P);
uint32_t BME280_Compensate_H(int32_t adc_H);

