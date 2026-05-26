// Home Security Sensor Node

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

// Interrupt output - pulled high when any alarm condition is active
// Wire this pin to GPIO 22 on the base node (Pico 2)
#define INTR_PIN 6

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

/*
 * Config command protocol (base node -> sensor node over UART RX)
 * Frame format: [0xBB][CMD][VAL_H][VAL_L][0x66]  (5 bytes, big-endian value)
 */
#define CMD_START        0xBB
#define CMD_END          0x66
#define CMD_FRAME_LEN    5

#define CMD_DIST_THRESH  0x01   // proximity alert threshold (cm, uint16)
#define CMD_TEMP_MAX     0x02   // temperature upper limit (°C × 100, int16)
#define CMD_SOUND_THRESH 0x03   // sound threshold (scaled 0..63, VAL_L only)
#define CMD_HUM_MAX      0x04   // humidity upper bound (% × 100, uint16)
#define CMD_HUM_MIN      0x05   // humidity lower bound (% × 100, uint16)

// alarm thresholds - runtime-configurable via CMD frames from the base node
static bool     irq_enabled  = true;
static uint8_t  irq_mask     = 0x17;  // bit0+1+2+4 = all alarms
static float    threshold_dist      = 50.0f;  // cm
static int16_t  threshold_temp      = 4000;   // 40.00 °C (stored ×100)
static uint8_t  threshold_sound     = 30;     // scaled ADC range 0..63
static uint16_t threshold_hum_max   = 8000;   // 80.00%
static uint16_t threshold_hum_min   = 2000;   // 20.00%

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

// command RX state machine
typedef struct {
    uint8_t buf[CMD_FRAME_LEN];
    uint8_t idx;
} cmd_state_t;

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

    temp_scaled = (((int32_t)t_fine * 5) + 128) >> 8;

    var1 = (int32_t)adc_h -
           ((int32_t)((int32_t)c->par_h1 << 4)) -
           (((temp_scaled * (int32_t)c->par_h3) / ((int32_t)100)) >> 1);

    var2 = ((int32_t)c->par_h2 *
            (((temp_scaled * (int32_t)c->par_h4) / ((int32_t)100)) +
             (((temp_scaled * ((temp_scaled * (int32_t)c->par_h5) / ((int32_t)100))) >> 6) / ((int32_t)100)) +
             ((int32_t)(1 << 14)))) >>
           10;

    var3 = var1 * var2;

    var4 = (((int32_t)c->par_h6 << 7) +
            ((temp_scaled * (int32_t)c->par_h7) / ((int32_t)100))) >>
           4;

    var5 = ((var3 >> 14) * (var3 >> 14)) >> 10;

    var6 = (var4 * var5) >> 1;

    hum_comp = (((var3 + var6) >> 10) * ((int32_t)1000)) >> 12;

    if (hum_comp > 100000)
        hum_comp = 100000;
    else if (hum_comp < 0)
        hum_comp = 0;

    return (uint32_t)hum_comp;
}

// READ CALIBRATION
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

// UART data frame (sensor node -> base node)
void send_uart_frame(float temp, float humidity, uint16_t sound, uint16_t dist, uint8_t alarm)
{

    int16_t temp_i = (int16_t)(temp * 100);
    int16_t hum_i = (int16_t)(humidity * 100);
    uint8_t sound_i = sound >> 6;
    uint16_t dist_i = dist;

    uint8_t frame[10];

    frame[0] = 0xAA;
    frame[1] = temp_i >> 8;
    frame[2] = temp_i;
    frame[3] = hum_i >> 8;
    frame[4] = hum_i;
    frame[5] = sound_i;
    frame[6] = dist_i >> 8;
    frame[7] = dist_i;
    frame[8] = alarm;
    frame[9] = 0x55;

    uart_write_blocking(UART_ID, frame, 10);
}

// Update a threshold from a received config command
static void apply_command(uint8_t cmd, uint16_t val)
{
    switch (cmd)
    {
    case CMD_DIST_THRESH:
        threshold_dist = (float)val;
        printf("[CFG] dist threshold -> %.0f cm\n", threshold_dist);
        break;
    case CMD_TEMP_MAX:
        threshold_temp = (int16_t)val;
        printf("[CFG] temp max -> %.2f C\n", threshold_temp / 100.0f);
        break;
    case CMD_SOUND_THRESH:
        threshold_sound = (uint8_t)(val & 0x3F);
        printf("[CFG] sound threshold -> %u\n", threshold_sound);
        break;
    case CMD_HUM_MAX:
        threshold_hum_max = val;
        printf("[CFG] hum max -> %.2f%%\n", threshold_hum_max / 100.0f);
        break;
    case CMD_HUM_MIN:
        threshold_hum_min = val;
        printf("[CFG] hum min -> %.2f%%\n", threshold_hum_min / 100.0f);
        break;
    case 0x06:
    irq_enabled = (val != 0);
    printf("[CFG] IRQ_ENABLE -> %u\n", irq_enabled);
    break;
case 0x07:
    irq_mask = (uint8_t)(val & 0xFF);
    printf("[CFG] IRQ_MASK -> 0x%02x\n", irq_mask);
    break;
    default:
        printf("[CFG] unknown cmd 0x%02x, ignoring\n", cmd);
        break;
    }
}

// Non-blocking poll for incoming config frames on UART RX
static void poll_commands(cmd_state_t *cs)
{
    while (uart_is_readable(UART_ID))
    {
        uint8_t b = uart_getc(UART_ID);

        if (cs->idx == 0)
        {
            if (b == CMD_START)
            {
                cs->buf[0] = b;
                cs->idx = 1;
            }
            // discard anything that isn't a start byte
        }
        else
        {
            cs->buf[cs->idx++] = b;

            if (cs->idx == CMD_FRAME_LEN)
            {
                if (cs->buf[CMD_FRAME_LEN - 1] == CMD_END)
                {
                    uint16_t val = (uint16_t)((cs->buf[2] << 8) | cs->buf[3]);
                    apply_command(cs->buf[1], val);
                }
                // reset regardless of validity
                cs->idx = 0;
            }
        }
    }
}

int main()
{
    stdio_init_all();
    sleep_ms(2000);

    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    // Interrupt output - driven high when any alarm is active
    gpio_init(INTR_PIN);
    gpio_set_dir(INTR_PIN, GPIO_OUT);
    gpio_put(INTR_PIN, 0);

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
    absolute_time_t t_bme   = make_timeout_time_ms(BME_INTERVAL);

    float dist    = 0;
    uint16_t sound = 0;
    float temp    = 0;
    float hum     = 0;
    uint8_t sound_i = 0;

    bool dist_ready  = false;
    bool sound_ready = false;
    bool bme_ready   = false;

    cmd_state_t cs = {0};

    while (true)
    {
        // Check for configuration commands from the base node before reading sensors
        poll_commands(&cs);

        if (absolute_time_diff_us(get_absolute_time(), t_ultra) <= 0)
        {
            dist = read_distance();
            dist_ready = (dist > 0);

            printf("Distance: %.2f cm\n", dist);

            t_ultra = make_timeout_time_ms(DIST_INTERVAL);
        }

        if (absolute_time_diff_us(get_absolute_time(), t_sound) <= 0)
        {
            sound = read_sound();
            sound_i = sound >> 6;
            sound_ready = true;

            printf("Sound: %u (scaled %u)\n", sound, sound_i);

            t_sound = make_timeout_time_ms(SOUND_INTERVAL);
        }

        if (absolute_time_diff_us(get_absolute_time(), t_bme) <= 0)
        {
            bme680_data_t bme680 = read_bme680(&cal);
            temp = bme680.temperature;
            hum  = bme680.humidity;
            bme_ready = true;
            t_bme = make_timeout_time_ms(BME_INTERVAL);
        }

        // alarm logic - thresholds are now configurable at runtime
        uint8_t alarm = 0;

        if (dist_ready && dist < threshold_dist)
            alarm |= (1 << 0);

        if (bme_ready && (temp > threshold_temp / 100.0f))
            alarm |= (1 << 1);

        if (sound_ready && sound_i > threshold_sound)
            alarm |= (1 << 2);

        if (bme_ready && (hum * 100.0f > threshold_hum_max || hum * 100.0f < threshold_hum_min))
            alarm |= (1 << 4);

        // Drive interrupt line high when any alarm is active so the base node
        // can react immediately via GPIO interrupt rather than polling UART frames
        gpio_put(INTR_PIN, (irq_enabled && (alarm & irq_mask)) ? 1 : 0);

        // Send UART frame
        send_uart_frame(temp, hum, sound, (uint16_t)dist, alarm);

        // Display alarm status

        printf("\nALARM: 0x%02X\n", alarm);
        printf("Temp=%.2f Hum=%.2f Sound=%u Dist=%.2f\n",
               temp, hum, sound_i, dist);

        sleep_ms(500);
    }
}
