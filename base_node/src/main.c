/*
 * Copyright (c) 2025 Group 8
 * SPDX-License-Identifier: Apache-2.0
 *
 * Home Security Base Node – main application.
 *
 * Periodically fetches sensor data from the custom sensor-node driver,
 * prints the values to the serial monitor, and drives an alert LED with
 * different blink patterns depending on which alarm flags are set.
 *
 * Alarm priority:
 *   1 blink  = motion / intrusion
 *   2 blinks = environmental anomaly
 *   3 blinks = abnormal sound
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include "../drivers/sensor_node/sensor_node.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/* Devicetree handles                                                 */
/* ------------------------------------------------------------------ */

#define SENSOR_NODE DT_NODELABEL(sensor_node)

#define MOTION_LED_NODE DT_ALIAS(motion_led)
#define TEMP_LED_NODE DT_ALIAS(temp_led)
#define SOUND_LED_NODE DT_ALIAS(sound_led)
#define HUMIDITY_LED_NODE DT_ALIAS(humidity_led)

static const struct gpio_dt_spec motion_led =
    GPIO_DT_SPEC_GET(MOTION_LED_NODE, gpios);

static const struct gpio_dt_spec temp_led =
    GPIO_DT_SPEC_GET(TEMP_LED_NODE, gpios);

static const struct gpio_dt_spec sound_led =
    GPIO_DT_SPEC_GET(SOUND_LED_NODE, gpios);

static const struct gpio_dt_spec humidity_led =
    GPIO_DT_SPEC_GET(HUMIDITY_LED_NODE, gpios);

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    k_sleep(K_SECONDS(2));

    printk("\n========================================\n");
    printk(" Home Security Base Node Started\n");
    printk("========================================\n\n");

    const struct device *sensor_dev = DEVICE_DT_GET(SENSOR_NODE);

    if (!device_is_ready(sensor_dev))
    {
        printk("Sensor node not ready\n");
        return -ENODEV;
    }

    gpio_pin_configure_dt(&motion_led, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&temp_led, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&sound_led, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&humidity_led, GPIO_OUTPUT_INACTIVE);

    bool blink = false;

    while (1)
    {
        blink = !blink;

        int ret = sensor_sample_fetch(sensor_dev);

        if (ret == 0)
        {
            struct sensor_value temp;
            struct sensor_value humidity;
            struct sensor_value sound;
            struct sensor_value dist;
            struct sensor_value alarm;

            sensor_channel_get(sensor_dev,
                               SENSOR_CHAN_AMBIENT_TEMP,
                               &temp);

            sensor_channel_get(sensor_dev,
                               (enum sensor_channel)SENSOR_CHAN_HUMIDITY,
                               &humidity);

            sensor_channel_get(sensor_dev,
                               (enum sensor_channel)SENSOR_CHAN_SOUND,
                               &sound);

            sensor_channel_get(sensor_dev,
                               (enum sensor_channel)SENSOR_CHAN_DISTANCE,
                               &dist);

            sensor_channel_get(sensor_dev,
                               (enum sensor_channel)SENSOR_CHAN_ALARM_STATUS,
                               &alarm);

            uint8_t alarm_flags = (uint8_t)alarm.val1;

            printk("[BASE] Temp: %d.%02d C | ",
                   temp.val1, temp.val2 / 10000);

            printk("Humidity: %d.%02d %% | ",
                   humidity.val1, humidity.val2 / 10000);

            printk("Sound: %d | ", sound.val1);

            printk("Distance: %d cm | ", dist.val1);

            printk("Alarm: 0x%02x\n", alarm_flags);

            /* ------------------------------------------------------ */
            /* LED LOGIC (FIXED)                                      */
            /* ------------------------------------------------------ */

            gpio_pin_set_dt(&motion_led,
                            (alarm_flags & ALARM_MOTION_BIT) ? 1 : 0);

            gpio_pin_set_dt(&temp_led,
                            (alarm_flags & ALARM_TEMP_BIT) ? 1 : 0);

            gpio_pin_set_dt(&sound_led,
                            (alarm_flags & ALARM_SOUND_BIT) ? 1 : 0);

            gpio_pin_set_dt(&humidity_led,
                            (alarm_flags & ALARM_HUMIDITY_BIT) ? 1 : 0);

            if (alarm_flags)
            {
                printk("ALARM ACTIVE\n");
            }
        }
        else if (ret == -EAGAIN)
        {
            printk("WARNING: No data from sensor node (timeout)\n");
        }
        else
        {
            printk("ERROR: sensor_sample_fetch failed (%d)\n", ret);
        }

        k_msleep(300);
    }
}