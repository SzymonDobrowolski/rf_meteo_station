#include <avr/io.h>
#include <util/delay.h>

#define MAIN_CLK 20000000 //20MHz

#define TWI_CLK 100000 //100KHz

#define BME280_ADDR 0x76 //lub 0x77 zaleznie od plytki

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

void TWI_Init()
{
    PORTA.PIN2CTRL = PORT_PULLUPEN_bm;
    PORTA.PIN3CTRL = PORT_PULLUPEN_bm;

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

uint8_t TWI_Read_NACK(void)
{
    TWI0.MCTRLB = TWI_ACKACT_NACK_gc | TWI_MCMD_RECVTRANS_gc;
    while (!(TWI0.MSTATUS & TWI_RIF_bm));
    return TWI0.MDATA;
}

#define BME280_ADDR 0x76

void BME280_WriteReg(uint8_t reg, uint8_t value)
{
    TWI_Start((BME280_ADDR << 1) | 0);   // Write
    TWI_Write(reg);
    TWI_Write(value);
    TWI_Stop();
}

uint8_t BME280_ReadReg(uint8_t reg)
{
    uint8_t val;

    TWI_Start((BME280_ADDR << 1) | 0);
    TWI_Write(reg);
    TWI_Start((BME280_ADDR << 1) | 1);
    val = TWI_Read_NACK();
    TWI_Stop();

    return val;
}

void BME280_ReadBytes(uint8_t reg, uint8_t *buf, uint8_t len)
{
    TWI_Start((BME280_ADDR << 1) | 0);
    TWI_Write(reg);

    TWI_Start((BME280_ADDR << 1) | 1);

    for (uint8_t i = 0; i < len - 1; i++)
        buf[i] = TWI_Read_ACK();

    buf[len - 1] = TWI_Read_NACK();

    TWI_Stop();
}

void BME280_Init(void)
{
    BME280_WriteReg(0xE0, 0xB6); //reset
    _delay_ms(5);

    //ctrl_hum
    BME280_WriteReg(0xF2, 0x01);

    //ctrl_meas (temp + press)
    BME280_WriteReg(0xF4, 0x27);

    //config
    BME280_WriteReg(0xF5, 0xA0);
}

void BME280_ReadCalibration(void)
{
    uint8_t buf1[26];
    uint8_t buf2[7];

    //kalibracja 0x88..0xA1 (26 bajtów)
    BME280_ReadBytes(0x88, buf1, 26);

    dig_T1 = (uint16_t)((buf1[1] << 8) | buf1[0]);
    dig_T2 = (int16_t)((buf1[3] << 8) | buf1[2]);
    dig_T3 = (int16_t)((buf1[5] << 8) | buf1[4]);

    dig_P1 = (uint16_t)((buf1[7] << 8) | buf1[6]);
    dig_P2 = (int16_t)((buf1[9] << 8) | buf1[8]);
    dig_P3 = (int16_t)((buf1[11] << 8) | buf1[10]);
    dig_P4 = (int16_t)((buf1[13] << 8) | buf1[12]);
    dig_P5 = (int16_t)((buf1[15] << 8) | buf1[14]);
    dig_P6 = (int16_t)((buf1[17] << 8) | buf1[16]);
    dig_P7 = (int16_t)((buf1[19] << 8) | buf1[18]);
    dig_P8 = (int16_t)((buf1[21] << 8) | buf1[20]);
    dig_P9 = (int16_t)((buf1[23] << 8) | buf1[22]);

    dig_H1 = buf1[25]; //0xA1

    //kalibracja 0xE1..0xE7 (7 bajtów)
    BME280_ReadBytes(0xE1, buf2, 7);

    dig_H2 = (int16_t)((buf2[1] << 8) | buf2[0]);
    dig_H3 = buf2[2];

    //H4 i H5 są zapisane "przycinając" bajty:
    //H4 = buf2[3] << 4 | (buf2[4] & 0x0F)
    //H5 = buf2[5] << 4 | (buf2[4] >> 4)
    dig_H4 = (int16_t)((buf2[3] << 4) | (buf2[4] & 0x0F));
    dig_H5 = (int16_t)((buf2[5] << 4) | ((buf2[4] >> 4) & 0x0F));

    dig_H6 = (int8_t)buf2[6];
}

int32_t BME280_Compensate_T(int32_t adc_T)
{
    int32_t var1, var2, T;

    var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;

    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >> 8; //temperatura w 0.01°C

    return T;
}

uint32_t BME280_Compensate_P(int32_t adc_P)
{
    int64_t var1, var2, p;

    var1 = (int64_t)t_fine - 128000;
    var2 = var1 * var1 * (int64_t)dig_P6;
    var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
    var2 = var2 + (((int64_t)dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * ((int64_t)dig_P1)) >> 33;

    if (var1 == 0) {
        return 0; //unikamy dzielenia przez zero
    }

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dig_P8) * p) >> 19;

    p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);

    //wynik p jest w jednostce Pa * 256 (Q8), więc dzielimy przez 256 aby otrzymać Pa
    return (uint32_t)(p >> 8);
}

uint32_t BME280_Compensate_H(int32_t adc_H)
{
    int32_t v_x1_u32r;

    v_x1_u32r = t_fine - 76800;
    v_x1_u32r = (((((adc_H << 14) - ((int32_t)dig_H4 << 20) - ((int32_t)dig_H5 * v_x1_u32r)) + 16384) >> 15) *
                 (((((v_x1_u32r * (int32_t)dig_H6) >> 10) * (((v_x1_u32r * (int32_t)dig_H3) >> 11) + 32768)) >> 10) + 2097152) *
                  (int32_t)dig_H2 + 8192) >> 14;

    v_x1_u32r = v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * (int32_t)dig_H1) >> 4);
    if (v_x1_u32r < 0) v_x1_u32r = 0;
    if (v_x1_u32r > 419430400) v_x1_u32r = 419430400;

    return (uint32_t)(v_x1_u32r >> 12); //wynik w jednostce 1/1024 %RH
}



//na odpowiednie pliki pozniej podziele, po debugowaniu xD

int main(void) {
    // 1. KONFIGURACJA ZEGARA
    // ATtiny414 ma zabezpieczone rejestry kluczowe (CCP - Configuration Change Protection).
    // Aby zmienić ustawienia zegara, musimy użyć makra _PROTECTED_WRITE.

    // Wyłączamy preskaler zegara (MCLKCTRLB = 0), 
    // dzięki temu procesor działa na pełnym 20 MHz.
    _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 0);

    // 2. KONFIGURACJA PINÓW
    // W nowej serii AVR (0/1/2-series) używamy struktur PORTx.
    // DIRSET to rejestr, który ustawia bity na 1 (wyjście) bez ruszania innych.
    // PIN1_bm to "Bit Mask" dla pinu 1 (czyli wartość 0b00000010).

    PORTA.DIRSET = PIN7_bm; // Ustaw PA1 jako WYJŚCIE
    PORTA.OUTSET = PIN7_bm; //Ustaw 1 na PA1

    //BME280_ReadCalibration(); pamietac o tym przy inicjacji

    // 3. GŁÓWNA PĘTLA
    while (1) {
        // OUTTGL (Output Toggle) to sprzętowa funkcja zmiany stanu na przeciwny.
        // Nie musisz sprawdzać czy jest 1 czy 0, procesor sam to odwróci.
        //PORTA.OUTTGL = PIN7_bm;
        
        /*odczytywac dane, ale trzeba je potem skompensowac
        
        uint8_t data[8];
        BME280_ReadBytes(0xF7, data, 8);

        // ciśnienie (20 bit)
        uint32_t raw_press = (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);

        // temperatura (20 bit)
        uint32_t raw_temp  = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);

        // wilgotność (16 bit)
        uint32_t raw_hum   = (data[6] << 8)  | (data[7]);
        
        */

        /*Tutaj jest przyklzd kompensacji
        
        uint8_t data[8];
        BME280_ReadBytes(0xF7, data, 8);

        int32_t raw_pres = ( (int32_t)data[0] << 12 ) | ( (int32_t)data[1] << 4 ) | ( data[2] >> 4 );
        int32_t raw_temp = ( (int32_t)data[3] << 12 ) | ( (int32_t)data[4] << 4 ) | ( data[5] >> 4 );
        int32_t raw_hum  = ( (int32_t)data[6] << 8 )  | ( (int32_t)data[7] );

        //kompensacja
        int32_t temp_hundredths = BME280_Compensate_T(raw_temp); //0.01°C
        uint32_t pressure_pa    = BME280_Compensate_P(raw_pres); //Pa
        uint32_t hum_x1024      = BME280_Compensate_H(raw_hum);  //humidity * 1024

        //konwersje użytkowe
        float temperature_C = temp_hundredths / 100.0f;
        float humidity_pct  = hum_x1024 / 1024.0f;
        float pressure_hPa  = pressure_pa / 100.0f;
        
        */
        _delay_ms(500); // Czekaj 500ms
    }

    return 0;
}
