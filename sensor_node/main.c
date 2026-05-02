#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"

// UART
#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1

// Distance sensor
#define TRIG_PIN 3
#define ECHO_PIN 2

// sound sensor
#define SOUND_ADC_CH 0
#define SOUND_GPIO 26

// BME680
// I2C Configuration
#define I2C_PORT i2c0
#define SDA_PIN 4
#define SCL_PIN 5

#define BME680_ADDR 0x77

#define REG_ID 0xD0
#define REG_RESET 0xE0
#define REG_CTRL_HUM 0x72
#define REG_CTRL_MEAS 0x74
#define REG_DATA_START 0x1D

#define SOFT_RESET_CMD 0xB6

// timing
#define DIST_INTERVAL 500
#define BME_INTERVAL 3000
#define SOUND_INTERVAL 200

// alarm thresholds
#define TEMP_MAX 35.0f       // degrees Celsius
#define TEMP_MIN 5.0f        // degrees Celsius
#define PRESSURE_MAX 1050.0f // hPa
#define PRESSURE_MIN 950.0f  // hPa
#define HUMIDITY_MAX 80.0f   // %
#define HUMIDITY_MIN 20.0f   // %

// calib struct
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

// bme680 data struct
typedef struct
{
    float temperature;
    float pressure;
    float humidity;
} bme680_data_t;

// I2C HELPERS
void write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    i2c_write_blocking(I2C_PORT, BME680_ADDR, buf, 2, false);
}

int read_reg(uint8_t reg, uint8_t *buf, int len)
{
    i2c_write_blocking(I2C_PORT, BME680_ADDR, &reg, 1, true);
    return i2c_read_blocking(I2C_PORT, BME680_ADDR, buf, len, false);
}

// CALC COMPENSATION
int32_t calc_temp(int32_t adc_t, bme_calib *c)
{
    int32_t var1 = ((adc_t >> 3) - ((int32_t)c->par_t1 << 1));
    int32_t var2 = (var1 * (int32_t)c->par_t2) >> 11;
    int32_t var3 = ((((var1 >> 1) * (var1 >> 1)) >> 12) * ((int32_t)c->par_t3 << 4)) >> 14;
    c->t_fine = var2 + var3;
    return (c->t_fine * 5 + 128) >> 8;
}

uint32_t calc_press(int32_t adc_p, bme_calib *c)
{
    int32_t var1 = (c->t_fine >> 1) - 64000;
    int32_t var2 = ((((var1 >> 2) * (var1 >> 2)) >> 11) * (int32_t)c->par_p6) >> 2;
    var2 += ((var1 * (int32_t)c->par_p5) << 1);
    var2 = (var2 >> 2) + ((int32_t)c->par_p4 << 16);
    var1 = (((((var1 >> 2) * (var1 >> 2)) >> 13) * ((int32_t)c->par_p3 << 5)) >> 3) + (((int32_t)c->par_p2 * var1) >> 1);
    var1 >>= 18;
    var1 = ((32768 + var1) * (int32_t)c->par_p1) >> 15;
    if (var1 == 0)
        return 0;

    uint32_t p = ((1048576 - adc_p) - (var2 >> 12)) * 3125;
    p = (p < 0x80000000) ? ((p << 1) / var1) : ((p / var1) * 2);

    var1 = ((int32_t)c->par_p9 * (int32_t)(((p >> 3) * (p >> 3)) >> 13)) >> 12;
    var2 = ((int32_t)(p >> 2) * (int32_t)c->par_p8) >> 13;
    int32_t var3 = ((int32_t)(p >> 8) * (p >> 8) * (p >> 8) * (int32_t)c->par_p10) >> 17;

    return p + ((var1 + var2 + var3 + ((int32_t)c->par_p7 << 7)) >> 4);
}

uint32_t calc_hum(int32_t adc_h, bme_calib *c)
{
    int32_t temp = (c->t_fine * 5 + 128) >> 8;
    int32_t v1 = adc_h - ((int32_t)c->par_h1 << 4) - (((temp * (int32_t)c->par_h3) / 100) >> 1);
    int32_t v2 = ((int32_t)c->par_h2 *
                  (((temp * (int32_t)c->par_h4) / 100) +
                   (((temp * ((temp * (int32_t)c->par_h5) / 100)) >> 6) / 100) +
                   (1 << 14))) >>
                 10;
    int32_t h = (v1 * v2) >> 10;
    if (h > 100000)
        h = 100000;
    if (h < 0)
        h = 0;
    return h;
}

// READ CALIB
void read_calib(bme_calib *c)
{
    uint8_t c1[25], c2[16];
    read_reg(0x89, c1, 25);
    read_reg(0xE1, c2, 16);

    c->par_t1 = (c2[9] << 8) | c2[8];
    c->par_t2 = (c1[2] << 8) | c1[1];
    c->par_t3 = c1[3];
    c->par_p1 = (c1[6] << 8) | c1[5];
    c->par_p2 = (c1[8] << 8) | c1[7];
    c->par_p3 = c1[9];
    c->par_p4 = (c1[12] << 8) | c1[11];
    c->par_p5 = (c1[14] << 8) | c1[13];
    c->par_p6 = c1[15];
    c->par_p7 = c1[16];
    c->par_p8 = (c1[19] << 8) | c1[18];
    c->par_p9 = (c1[21] << 8) | c1[20];
    c->par_p10 = c1[22];
    c->par_h1 = (c2[2] << 4) | (c2[1] & 0x0F);
    c->par_h2 = (c2[0] << 4) | (c2[1] >> 4);
    c->par_h3 = c2[3];
    c->par_h4 = (c2[4] << 4) | (c2[5] & 0x0F);
    c->par_h5 = (c2[5] >> 4) | (c2[6] << 4);
    c->par_h6 = c2[7];
    c->par_h7 = c2[8];
}

// sensor functions (one for each)
// distance
float read_distance()
{

    gpio_put(TRIG_PIN, 0);
    sleep_us(2);
    gpio_put(TRIG_PIN, 1);
    sleep_us(10);
    gpio_put(TRIG_PIN, 0);

    // Timeout protection
    absolute_time_t timeout = make_timeout_time_ms(30);
    while (gpio_get(ECHO_PIN) == 0)
    {
        if (absolute_time_diff_us(get_absolute_time(), timeout) < 0)
            return -1;
    }

    absolute_time_t start = get_absolute_time();

    while (gpio_get(ECHO_PIN) == 1)
    {
        if (absolute_time_diff_us(get_absolute_time(), timeout) < 0)
            return -1;
    }

    absolute_time_t end = get_absolute_time();

    int64_t duration = absolute_time_diff_us(start, end);
    return duration / 58.0;
}

// sound
uint16_t read_sound()
{
    return adc_read();
}

bme680_data_t read_bme680(bme_calib *cal)
{
    write_reg(REG_CTRL_HUM, 0x01);
    write_reg(REG_CTRL_MEAS, 0x25);

    uint8_t status;
    do
    {
        read_reg(REG_DATA_START, &status, 1);
        sleep_ms(10);
    } while (status & 0x20);

    uint8_t d[10];
    read_reg(REG_DATA_START, d, 10);

    int32_t adc_p = (d[2] << 12) | (d[3] << 4) | (d[4] >> 4);
    int32_t adc_t = (d[5] << 12) | (d[6] << 4) | (d[7] >> 4);
    int32_t adc_h = (d[8] << 8) | d[9];

    float temp = calc_temp(adc_t, cal) / 100.0f;
    float press = calc_press(adc_p, cal) / 100.0f;
    float hum = calc_hum(adc_h, cal) / 1000.0f;

    printf("T: %.2f C | P: %.2f hPa | H: %.2f %%\n", temp, press, hum);
    bme680_data_t data = {temp, press, hum};
    return data;
}
// UART

void send_uart_frame(float temp, float pressure, float humidity, uint16_t sound, uint16_t dist)
{

    int16_t temp_i = (int16_t)(temp * 100);
    int16_t press_i = (int16_t)(pressure * 100);
    int16_t hum_i = (int16_t)(humidity * 100);
    uint8_t sound_i = sound >> 4;

    uint8_t alarm = 0;
    if (dist < 50)
        alarm |= (1 << 0);
    if (temp > TEMP_MAX || temp < TEMP_MIN)
        alarm |= (1 << 1);
    if (sound_i > 200)
        alarm |= (1 << 2);
    if (pressure > PRESSURE_MAX || pressure < PRESSURE_MIN)
        alarm |= (1 << 3);
    if (humidity > HUMIDITY_MAX || humidity < HUMIDITY_MIN)
        alarm |= (1 << 4);

    uint8_t frame[12];

    frame[0] = 0xAA;
    frame[1] = temp_i >> 8;
    frame[2] = temp_i;
    frame[3] = press_i >> 8;
    frame[4] = press_i;
    frame[5] = hum_i >> 8;
    frame[6] = hum_i;
    frame[7] = sound_i;
    frame[8] = dist >> 8;
    frame[9] = dist;
    frame[10] = alarm;
    frame[11] = 0x55;

    uart_write_blocking(UART_ID, frame, 12);
}

int main()
{
    stdio_init_all();
    sleep_ms(2000);

    // Ultrasonic
    gpio_init(TRIG_PIN);
    gpio_set_dir(TRIG_PIN, GPIO_OUT);
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);

    // ADC
    adc_init();
    adc_gpio_init(SOUND_GPIO);
    adc_select_input(SOUND_ADC_CH);

    // I2C
    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    // BME init
    uint8_t id;
    read_reg(REG_ID, &id, 1);
    if (id != 0x61)
    {
        printf("BME680 not found!\n");
        return -1;
    }

    write_reg(REG_RESET, SOFT_RESET_CMD);
    sleep_ms(100);

    bme_calib cal;
    read_calib(&cal);

    // Timers
    absolute_time_t t_ultra = make_timeout_time_ms(DIST_INTERVAL);
    absolute_time_t t_sound = make_timeout_time_ms(SOUND_INTERVAL);
    absolute_time_t t_bme = make_timeout_time_ms(BME_INTERVAL);

    uint16_t dist = 0;
    uint16_t sound = 0;
    float temp = 0;
    float pressure = 0;
    float hum = 0;

    while (true)
    {

        if (absolute_time_diff_us(get_absolute_time(), t_ultra) <= 0)
        {
            dist = read_distance();
            printf("Distance: %.2f cm\n", dist);
            t_ultra = make_timeout_time_ms(DIST_INTERVAL);
        }

        if (absolute_time_diff_us(get_absolute_time(), t_sound) <= 0)
        {
            sound = read_sound();
            printf("Sound: %u\n", sound);
            t_sound = make_timeout_time_ms(SOUND_INTERVAL);
        }

        if (absolute_time_diff_us(get_absolute_time(), t_bme) <= 0)
        {
            bme680_data_t bme680 = read_bme680(&cal);
            temp = bme680.temperature;
            pressure = bme680.pressure;
            hum = bme680.humidity;
            t_bme = make_timeout_time_ms(BME_INTERVAL);
        }

        send_uart_frame(temp, pressure, hum, sound, dist);

        sleep_ms(500);
    }
}