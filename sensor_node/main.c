#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define TRIG_PIN 3
#define ECHO_PIN 2

int main()
{
    stdio_init_all();
    sleep_ms(2000);
    printf("HC-SR04 Distance Sensor Test\n");

    gpio_init(TRIG_PIN);
    gpio_set_dir(TRIG_PIN, GPIO_OUT);

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);

    while (true)
    {
        // Send 10us trigger pulse
        gpio_put(TRIG_PIN, 0);
        sleep_us(2);
        gpio_put(TRIG_PIN, 1);
        sleep_us(10);
        gpio_put(TRIG_PIN, 0);

        // Wait for echo to go HIGH
        while (gpio_get(ECHO_PIN) == 0)
            ;

        absolute_time_t start = get_absolute_time();

        // Wait for echo to go LOW
        while (gpio_get(ECHO_PIN) == 1);

        absolute_time_t end = get_absolute_time();

        // Calculate time difference in microseconds
        int64_t duration = absolute_time_diff_us(start, end);

        // Convert to distance (cm)
        float distance = duration / 58.0;

        printf("Distance: %.2f cm\n", distance);

        sleep_ms(500);
    }
}