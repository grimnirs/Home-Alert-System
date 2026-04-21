#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

#define SOUND_AO_GPIO 26
#define SOUND_ADC_CH  0

int main(void) {
    stdio_init_all();
    sleep_ms(2000);
    printf("LM386 sound sensor test\n");

    adc_init();
    adc_gpio_init(SOUND_AO_GPIO);
    adc_select_input(SOUND_ADC_CH);

    absolute_time_t next_print = make_timeout_time_ms(1000);

    while (true) {
        if (absolute_time_diff_us(get_absolute_time(), next_print) <= 0) {
            printf("AO=%u\n", adc_read());
            next_print = make_timeout_time_ms(1000);
        }
        sleep_ms(5);
    }
}