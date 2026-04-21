#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

#define SOUND_DO_PIN  16
#define SOUND_AO_GPIO 26   // ADC0
#define SOUND_ADC_CH  0

int main(void) {
    stdio_init_all();
    sleep_ms(2000);  // let USB CDC enumerate
    printf("LM386 sound sensor test\n");

    gpio_init(SOUND_DO_PIN);
    gpio_set_dir(SOUND_DO_PIN, GPIO_IN);
    // No internal pull — module drives DO actively

    adc_init();
    adc_gpio_init(SOUND_AO_GPIO);
    adc_select_input(SOUND_ADC_CH);

    bool last_do = gpio_get(SOUND_DO_PIN);
    uint32_t events = 0;

    // Print a rolling analog baseline every ~1s
    absolute_time_t next_baseline = make_timeout_time_ms(1000);

    while (true) {
        bool now_do = gpio_get(SOUND_DO_PIN);
        uint16_t ao = adc_read();  // 0..4095

        if (now_do != last_do) {
            events++;
            printf("DO edge: %d -> %d | AO=%u | events=%lu\n",
                   last_do, now_do, ao, events);
            last_do = now_do;
        }

        if (absolute_time_diff_us(get_absolute_time(), next_baseline) <= 0) {
            printf("baseline AO=%u (quiet reading)\n", ao);
            next_baseline = make_timeout_time_ms(1000);
        }

        sleep_ms(5);
    }
}