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
#include <zephyr/usb/usb_device.h>
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

/* Sensor node defined in app.overlay */
#define SENSOR_NODE DT_NODELABEL(sensor_node)

/* Alert LED alias from app.overlay */
#define ALERT_LED_NODE DT_ALIAS(alert_led)

static const struct gpio_dt_spec alert_led =
    GPIO_DT_SPEC_GET(ALERT_LED_NODE, gpios);

/* ------------------------------------------------------------------ */
/* LED helper                                                         */
/* ------------------------------------------------------------------ */

/*
 * Blink LED a given number of times.
 * Each blink = 150 ms ON + 150 ms OFF
 */
static void blink_led(const struct gpio_dt_spec *led, int times)
{
    for (int i = 0; i < times; i++)
    {
        gpio_pin_set_dt(led, 1);
        k_msleep(150);

        gpio_pin_set_dt(led, 0);
        k_msleep(150);
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    /* -------------------------------------------------------------- */
    /* Get sensor-node device                                         */
    /* -------------------------------------------------------------- */
    /* Enable USB serial */
    if (usb_enable(NULL) != 0)
    {
        return -EIO;
    }

    /* Wait for USB enumeration */
    k_sleep(K_SECONDS(2));
    
    const struct device *sensor_dev = DEVICE_DT_GET(SENSOR_NODE);

    if (!device_is_ready(sensor_dev))
    {
        printk("ERROR: Sensor node device not ready!\n");
        return -ENODEV;
    }

    /* -------------------------------------------------------------- */
    /* Configure alert LED                                            */
    /* -------------------------------------------------------------- */

    if (!gpio_is_ready_dt(&alert_led))
    {
        printk("ERROR: Alert LED GPIO not ready!\n");
        return -ENODEV;
    }

    gpio_pin_configure_dt(&alert_led, GPIO_OUTPUT_INACTIVE);

    printk("\n");
    printk("========================================\n");
    printk(" Home Security Base Node Started\n");
    printk("========================================\n\n");

    /* Prevent alarm spam */
    uint8_t previous_alarm_flags = 0;

    /* -------------------------------------------------------------- */
    /* Main loop                                                      */
    /* -------------------------------------------------------------- */

    while (1)
    {
        int ret = sensor_sample_fetch(sensor_dev);

        if (ret == 0)
        {
            /* ------------------------------------------------------ */
            /* Read sensor channels                                   */
            /* ------------------------------------------------------ */

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

            /* ------------------------------------------------------ */
            /* Print normal sensor readings                           */
            /* ------------------------------------------------------ */

            printk("[BASE] ");
            printk("Temp: %d.%02d C | ",
                   temp.val1,
                   temp.val2 / 10000);

            printk("Humidity: %d.%02d %% | ",
                   humidity.val1,
                   humidity.val2 / 10000);

            printk("Sound: %d | ",
                   sound.val1);

            printk("Distance: %d cm | ",
                   dist.val1);

            printk("Alarm: 0x%02x\n",
                   alarm_flags);

            /* ------------------------------------------------------ */
            /* Alarm handling                                         */
            /* ------------------------------------------------------ */

            if ((alarm_flags != 0) &&
                (alarm_flags != previous_alarm_flags))
            {
                printk("\n");
                printk("========================================\n");
                printk(" SECURITY ALARM TRIGGERED\n");
                printk("========================================\n");

                /* Motion alarm */
                if (alarm_flags & ALARM_MOTION_BIT)
                {
                    printk("-> Motion detected\n");
                }

                /* Temperature alarm */
                if (alarm_flags & ALARM_TEMP_BIT)
                {
                    printk("-> Temperature threshold exceeded\n");
                }

                /* Humidity alarm */
                if (alarm_flags & ALARM_HUMIDITY_BIT)
                {
                    printk("-> Humidity threshold exceeded\n");
                }

                /* Sound alarm */
                if (alarm_flags & ALARM_SOUND_BIT)
                {
                    printk("-> Abnormal sound detected\n");
                }

                printk("\n");
                printk("Current Sensor Values:\n");

                printk("Temperature : %d.%02d C\n",
                       temp.val1,
                       temp.val2 / 10000);

                printk("Humidity    : %d.%02d %%\n",
                       humidity.val1,
                       humidity.val2 / 10000);

                printk("Sound Level : %d\n",
                       sound.val1);

                printk("Distance    : %d cm\n",
                       dist.val1);

                printk("Alarm Flags : 0x%02x\n",
                       alarm_flags);

                printk("========================================\n\n");

                /* -------------------------------------------------- */
                /* LED blink patterns                                 */
                /* -------------------------------------------------- */

                if (alarm_flags & ALARM_MOTION_BIT)
                {
                    /* Highest priority */
                    blink_led(&alert_led, 1);
                }
                else if (alarm_flags &
                         (ALARM_TEMP_BIT |
                          ALARM_HUMIDITY_BIT))
                {
                    blink_led(&alert_led, 2);
                }
                else if (alarm_flags & ALARM_SOUND_BIT)
                {
                    blink_led(&alert_led, 3);
                }
            }

            /* Store previous alarm state */
            previous_alarm_flags = alarm_flags;
        }
        else if (ret == -EAGAIN)
        {
            printk("WARNING: No data from sensor node (timeout)\n");
        }
        else
        {
            printk("ERROR: sensor_sample_fetch failed (%d)\n", ret);
        }

        /*
         * Small delay to avoid tight polling loop.
         */
        k_msleep(100);
    }

    return 0;
}