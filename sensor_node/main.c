#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// I2C Configuration
#define I2C_PORT i2c0
#define SDA_PIN 4
#define SCL_PIN 5
#define BME680_ADDR 0x77

// Register Map (Matching your Zephyr bme680_reg.h)
#define REG_ID 0xD0
#define REG_RESET 0xE0
#define REG_CTRL_HUM 0x72
#define REG_CTRL_MEAS 0x74
#define REG_CONFIG 0x75
#define REG_DATA_START 0x1D // Press MSB is 0x1F, Temp MSB is 0x22

#define SOFT_RESET_CMD 0xB6

// Calibration Struct (Extended for P and H)
typedef struct
{
    uint16_t par_t1;
    int16_t par_t2;
    int8_t par_t3;
    uint16_t par_p1;
    int16_t par_p2;
    int8_t par_p3;
    int16_t par_p4;
    int16_t par_p5;
    int8_t par_p6;
    int8_t par_p7;
    int16_t par_p8;
    int16_t par_p9;
    uint16_t par_p10;
    uint16_t par_h1;
    uint16_t par_h2;
    int8_t par_h3;
    int8_t par_h4;
    int8_t par_h5;
    int8_t par_h6;
    int8_t par_h7;
    int32_t t_fine;
} bme_calib;

// --- I2C Helpers ---
static void i2c_setup()
{
    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);
}

static int read_reg(uint8_t reg, uint8_t *buf, int len)
{
    if (i2c_write_blocking(I2C_PORT, BME680_ADDR, &reg, 1, true) < 0)
        return -1;
    return i2c_read_blocking(I2C_PORT, BME680_ADDR, buf, len, false);
}

static void write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    i2c_write_blocking(I2C_PORT, BME680_ADDR, buf, 2, false);
}

// --- Compensation Formulas (Bosch Fixed Point) ---

static int32_t calc_temp(int32_t adc_t, bme_calib *c)
{
    int32_t var1 = ((adc_t >> 3) - ((int32_t)c->par_t1 << 1));
    int32_t var2 = (var1 * (int32_t)c->par_t2) >> 11;
    int32_t var3 = ((((var1 >> 1) * (var1 >> 1)) >> 12) * ((int32_t)c->par_t3 << 4)) >> 14;
    c->t_fine = var2 + var3;
    return (c->t_fine * 5 + 128) >> 8;
}

static uint32_t calc_press(int32_t adc_p, bme_calib *c)
{
    int32_t var1 = (c->t_fine >> 1) - 64000;
    int32_t var2 = ((((var1 >> 2) * (var1 >> 2)) >> 11) * (int32_t)c->par_p6) >> 2;
    var2 = var2 + ((var1 * (int32_t)c->par_p5) << 1);
    var2 = (var2 >> 2) + ((int32_t)c->par_p4 << 16);
    var1 = (((((var1 >> 2) * (var1 >> 2)) >> 13) * ((int32_t)c->par_p3 << 5)) >> 3) + (((int32_t)c->par_p2 * var1) >> 1);
    var1 >>= 18;
    var1 = ((32768 + var1) * (int32_t)c->par_p1) >> 15;
    if (var1 == 0)
        return 0;
    uint32_t p = (uint32_t)(((int32_t)1048576 - adc_p) - (var2 >> 12)) * (3125);
    p = (p < 0x80000000) ? ((p << 1) / (uint32_t)var1) : ((p / (uint32_t)var1) * 2);
    var1 = ((int32_t)c->par_p9 * (int32_t)(((p >> 3) * (p >> 3)) >> 13)) >> 12;
    var2 = ((int32_t)(p >> 2) * (int32_t)c->par_p8) >> 13;
    int32_t var3 = ((int32_t)(p >> 8) * (int32_t)(p >> 8) * (int32_t)(p >> 8) * (int32_t)c->par_p10) >> 17;
    return (uint32_t)((int32_t)p + ((var1 + var2 + var3 + ((int32_t)c->par_p7 << 7)) >> 4));
}

static uint32_t calc_hum(int32_t adc_h, bme_calib *c)
{
    int32_t temp = (c->t_fine * 5 + 128) >> 8;
    int32_t v1 = adc_h - ((int32_t)c->par_h1 << 4) - (((temp * (int32_t)c->par_h3) / 100) >> 1);
    int32_t v2 = ((int32_t)c->par_h2 * (((temp * (int32_t)c->par_h4) / 100) + (((temp * ((temp * (int32_t)c->par_h5) / 100)) >> 6) / 100) + (1 << 14))) >> 10;
    int32_t v3 = v1 * v2;
    int32_t v4 = (((int32_t)c->par_h6 << 7) + ((temp * (int32_t)c->par_h7) / 100)) >> 4;
    int32_t h = (((v3 >> 14) * (v3 >> 14)) >> 10);
    h = ((v3 + ((h * v4) >> 1)) >> 10);
    h = (h * 1000) >> 12;
    if (h > 100000)
        h = 100000;
    else if (h < 0)
        h = 0;
    return (uint32_t)h;
}

// --- Calibration Reader ---
static void read_calib(bme_calib *c)
{
    uint8_t c1[25], c2[16];
    read_reg(0x89, c1, 25);
    read_reg(0xE1, c2, 16);

    c->par_t1 = (uint16_t)((c2[9] << 8) | c2[8]);
    c->par_t2 = (int16_t)((c1[2] << 8) | c1[1]);
    c->par_t3 = (int8_t)c1[3];
    c->par_p1 = (uint16_t)((c1[6] << 8) | c1[5]);
    c->par_p2 = (int16_t)((c1[8] << 8) | c1[7]);
    c->par_p3 = (int8_t)c1[9];
    c->par_p4 = (int16_t)((c1[12] << 8) | c1[11]);
    c->par_p5 = (int16_t)((c1[14] << 8) | c1[13]);
    c->par_p6 = (int8_t)c1[15];
    c->par_p7 = (int8_t)c1[16];
    c->par_p8 = (int16_t)((c1[19] << 8) | c1[18]);
    c->par_p9 = (int16_t)((c1[21] << 8) | c1[20]);
    c->par_p10 = (uint16_t)c1[22];
    c->par_h1 = (uint16_t)((c2[2] << 4) | (c2[1] & 0x0F));
    c->par_h2 = (uint16_t)((c2[0] << 4) | (c2[1] >> 4));
    c->par_h3 = (int8_t)c2[3];
    c->par_h4 = (int8_t)c2[4];
    c->par_h5 = (int8_t)c2[5];
    c->par_h6 = (uint8_t)c2[6];
    c->par_h7 = (int8_t)c2[7];
}

int main()
{
    stdio_init_all();
    sleep_ms(2000);
    i2c_setup();

    uint8_t id;
    read_reg(REG_ID, &id, 1);
    if (id != 0x61)
    {
        printf("BME680 not found! ID: 0x%02X\n", id);
        return -1;
    }

    write_reg(REG_RESET, SOFT_RESET_CMD);
    sleep_ms(100);

    bme_calib cal;
    read_calib(&cal);

    while (1)
    {
        // 1. Set Humidity oversampling (0x72) - MUST be done first
        write_reg(REG_CTRL_HUM, 0x01); // x1
        // 2. Set Temp/Press oversampling and trigger Forced Mode (0x74)
        // 0x25 = 001(T x1) 001(P x1) 01(Forced)
        write_reg(REG_CTRL_MEAS, 0x25);

        // Wait for measurement bit to clear in status register
        uint8_t status;
        do
        {
            read_reg(0x1D, &status, 1);
            sleep_ms(10);
        } while (status & 0x20); // Bit 5 is "measuring"

        // Read all data (Temp, Press, Hum) starting from 0x1D
        uint8_t d[10];
        read_reg(0x1D, d, 10);

        int32_t adc_p = (d[2] << 12) | (d[3] << 4) | (d[4] >> 4);
        int32_t adc_t = (d[5] << 12) | (d[6] << 4) | (d[7] >> 4);
        int32_t adc_h = (d[8] << 8) | d[9];

        // Compensate (Order matters: Temp MUST be first to set t_fine)
        float temp = calc_temp(adc_t, &cal) / 100.0f;
        float press = calc_press(adc_p, &cal) / 100.0f;
        float hum = calc_hum(adc_h, &cal) / 1000.0f;

        printf("T: %.2f C | P: %.2f hPa | H: %.2f %%\n", temp, press, hum);

        sleep_ms(3000);
    }
}