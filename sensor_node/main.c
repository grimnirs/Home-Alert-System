#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"

// UART
// #define UART_ID uart0
// #define BAUD_RATE 115200
// #define UART_TX_PIN 0
// #define UART_RX_PIN 1

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
#define TEMP_MAX 35.0f           // degrees Celsius
#define TEMP_MIN 5.0f            // degrees Celsius
#define HUMIDITY_MAX 80.0f       // %
#define HUMIDITY_MIN 20.0f       // %
// TODO: Add a lower limit to mimic wispering
#define SOUND_ALARM_THRESHOLD 45 // sound_i = raw ADC >> 4, range 0..63

// calib struct
typedef struct
{
    uint16_t par_t1;
    int16_t par_t2;
    int8_t par_t3;
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
static uint32_t calc_hum(int32_t adc_h, bme_calib *c, int32_t t_fine)
{
    int32_t temp_scaled, var1, var2, var3, var4, var5, var6, hum_comp;

    // 1. Calculate temperature in degC scaled by 100
    // This matches the datasheet's temp_scaled = (int32_t)temp_comp
    temp_scaled = (((int32_t)t_fine * 5) + 128) >> 8;

    // 2. var1 calculation
    var1 = (int32_t)adc_h -
           ((int32_t)((int32_t)c->par_h1 << 4)) -
           (((temp_scaled * (int32_t)c->par_h3) / ((int32_t)100)) >> 1);

    // 3. var2 calculation
    var2 = ((int32_t)c->par_h2 *
            (((temp_scaled * (int32_t)c->par_h4) / ((int32_t)100)) +
             (((temp_scaled * ((temp_scaled * (int32_t)c->par_h5) / ((int32_t)100))) >> 6) / ((int32_t)100)) +
             ((int32_t)(1 << 14)))) >>
           10;

    // 4. var3 calculation
    var3 = var1 * var2;

    // 5. var4 calculation
    var4 = (((int32_t)c->par_h6 << 7) +
            ((temp_scaled * (int32_t)c->par_h7) / ((int32_t)100))) >>
           4;

    // 6. var5 calculation (Notice the shift >> 10 is outside the square)
    var5 = ((var3 >> 14) * (var3 >> 14)) >> 10;

    // 7. var6 calculation
    var6 = (var4 * var5) >> 1;

    // 8. Final humidity calculation (hum_comp)
    // Matches: (((var3 + var6) >> 10) * 1000) >> 12
    hum_comp = (((var3 + var6) >> 10) * ((int32_t)1000)) >> 12;

    // 9. Clamping to [0% - 100%] in milli-percent
    if (hum_comp > 100000)
        hum_comp = 100000;
    else if (hum_comp < 0)
        hum_comp = 0;

    return (uint32_t)hum_comp;
}

// READ CALIB
void read_calib(bme_calib *c)
{
    uint8_t c1[25], c2[16];
    read_reg(0x89, c1, 25);
    read_reg(0xE1, c2, 16);

    // TEMPERATURE (From Table 1 in previous datasheet sections)
    c->par_t1 = (uint16_t)((c2[9] << 8) | c2[8]); // 0xE9 / 0xEA
    c->par_t2 = (int16_t)((c1[2] << 8) | c1[1]);  // 0x8A / 0x8B
    c->par_t3 = (int8_t)c1[3];                    // 0x8C

    // HUMIDITY (From Table 13)
    c->par_h1 = (uint16_t)(((uint16_t)c2[2] << 4) | (c2[1] & 0x0F)); // 0xE2 / 0xE3
    c->par_h2 = (uint16_t)(((uint16_t)c2[0] << 4) | (c2[1] >> 4));   // 0xE1 / 0xE2
    c->par_h3 = (int8_t)c2[3];                                       // 0xE4
    c->par_h4 = (int8_t)c2[4];                                       // 0xE5
    c->par_h5 = (int8_t)c2[5];                                       // 0xE6
    c->par_h6 = (int8_t)c2[6];                                       // 0xE7
    c->par_h7 = (int8_t)c2[7];                                       // 0xE8
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

    int32_t adc_t = (d[5] << 12) | (d[6] << 4) | (d[7] >> 4);
    uint16_t adc_h = (uint16_t)(((uint32_t)d[8] << 8) | (uint32_t)d[9]);

    float temp = calc_temp(adc_t, cal) / 100.0f;
    float hum = calc_hum(adc_h, cal, cal->t_fine) / 1000.0f;

    printf("T: %.2f C | H: %.2f %%\n", temp, hum);
    bme680_data_t data = {temp, hum};
    return data;
}
// UART
/*
void send_uart_frame(float temp, float humidity, uint16_t sound, uint16_t dist)
{

    int16_t temp_i = (int16_t)(temp * 100);
    int16_t hum_i = (int16_t)(humidity * 100);
    uint8_t sound_i = sound >> 4;

    uint8_t alarm = 0;
    if (dist < 50)
        alarm |= (1 << 0);
    if (temp > TEMP_MAX || temp < TEMP_MIN)
        alarm |= (1 << 1);
    if (sound_i > 200)
        alarm |= (1 << 2);
    if (humidity > HUMIDITY_MAX || humidity < HUMIDITY_MIN)
        alarm |= (1 << 4);

    uint8_t frame[10];

    frame[0] = 0xAA;
    frame[1] = temp_i >> 8;
    frame[2] = temp_i;
    frame[3] = hum_i >> 8;
    frame[4] = hum_i;
    frame[5] = sound_i;
    frame[6] = dist >> 8;
    frame[7] = dist;
    frame[8] = alarm;
    frame[9] = 0x55;

    uart_write_blocking(UART_ID, frame, 10);
}
*/

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

    float dist = 0;
    uint16_t sound = 0;
    float temp = 0;
    float hum = 0;

    while (true)
    {

        if (absolute_time_diff_us(get_absolute_time(), t_ultra) <= 0)
        {
            dist = read_distance();
            if (dist > 0)
                printf("Distance: %.2f cm\n", dist);
            else
                printf("Distance: ERROR (timeout)\n");
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
            hum = bme680.humidity;
            t_bme = make_timeout_time_ms(BME_INTERVAL);
        }

        // UART data that would be sent:
        int16_t temp_i = (int16_t)(temp * 100);
        int16_t hum_i = (int16_t)(hum * 100);
        uint8_t sound_i = sound >> 4;
        uint16_t dist_i = (uint16_t)dist;

        uint8_t alarm = 0;
        if (dist < 50)
            alarm |= (1 << 0);
        if (temp > TEMP_MAX || temp < TEMP_MIN)
            alarm |= (1 << 1);
        if (sound_i > SOUND_ALARM_THRESHOLD)
            alarm |= (1 << 2);
        if (hum > HUMIDITY_MAX || hum < HUMIDITY_MIN)
            alarm |= (1 << 4);

        // Display alarm status
        printf("\nALARM: %s\n", alarm ? "ON" : "OFF");
        if (alarm)
        {
            printf("  Triggered by:");
            if (alarm & (1 << 0))
                printf(" Distance<%dcm", 50);
            if (alarm & (1 << 1))
                printf(" Temp(%.1f°C)", temp);
            if (alarm & (1 << 2))
                printf(" Sound(%u)", sound_i);
            if (alarm & (1 << 4))
                printf(" Humidity(%.1f%%)", hum);
            printf("\n");
        }

        printf("\n=== UART Frame Data ===\n");
        printf("Raw Values - Temp: %d, Humidity: %d\n", temp_i, hum_i);
        printf("Sound: %u (raw %u), Distance: %u cm\n", sound_i, sound, dist_i);
        printf("Alarm Flags: 0x%02X\n", alarm);
        printf("Frame: 0xAA %02X %02X %02X %02X %02X %02X %02X 0x55\n",
               temp_i >> 8, temp_i, hum_i >> 8, hum_i,
               sound_i, dist_i >> 8, dist_i, alarm);
        printf("======================\n\n");

        sleep_ms(500);
    }
}