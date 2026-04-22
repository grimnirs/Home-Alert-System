/*
 * Copyright (c) 2025 Group 8
 * SPDX-License-Identifier: Apache-2.0
 *
 * Home Security Base Node – main application.
 *
 * Periodically fetches sensor data from the custom sensor-node driver,
 * prints the values to the debug console, and drives an alert LED with
 * different blink patterns depending on which alarm flags are set:
 *
 *   1 blink  = motion / proximity  (HC-SR04)
 *   2 blinks = gas / environmental (BME680)
 *   3 blinks = sound anomaly       (LM386)
 *
 * If multiple alarms are active simultaneously, the highest-priority
 * pattern is shown (motion > gas > sound).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

/* Pull in custom channel definitions */
#include "../drivers/sensor_node/sensor_node.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  Devicetree handles                                                */
/* ------------------------------------------------------------------ */

/* The sensor-node device defined in app.overlay */
#define SENSOR_NODE DT_NODELABEL(sensor_node)

/* The alert LED */
#define ALERT_LED_NODE DT_ALIAS(alert_led)

static const struct gpio_dt_spec alert_led =
    GPIO_DT_SPEC_GET(ALERT_LED_NODE, gpios);

/* ------------------------------------------------------------------ */
/*  LED helpers                                                       */
/* ------------------------------------------------------------------ */

/** Blink the LED @p times (each blink = 150 ms on + 150 ms off). */
static void blink_led(const struct gpio_dt_spec *led, int times)
{
    for (int i = 0; i < times; i++) {
        gpio_pin_set_dt(led, 1);
        k_msleep(150);
        gpio_pin_set_dt(led, 0);
        k_msleep(150);
    }
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */
int main(void)
{
    /* ---- Get the sensor-node device ---- */
    const struct device *sensor_dev = DEVICE_DT_GET(SENSOR_NODE);

    if (!device_is_ready(sensor_dev)) {
        LOG_ERR("Sensor node device not ready");
        return -ENODEV;
    }

    /* ---- Set up the alert LED ---- */
    if (!gpio_is_ready_dt(&alert_led)) {
        LOG_ERR("Alert LED GPIO not ready");
        return -ENODEV;
    }
    gpio_pin_configure_dt(&alert_led, GPIO_OUTPUT_INACTIVE);

    LOG_INF("=== Home Security Base Node started ===");

    /* ---- Main loop ---- */
    while (1) {
        int ret = sensor_sample_fetch(sensor_dev);

        if (ret == 0) {
            /* ---- Read all channels ---- */
            struct sensor_value temp, gas, sound, dist, alarm;

            sensor_channel_get(sensor_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
            sensor_channel_get(sensor_dev,
                               (enum sensor_channel)SENSOR_CHAN_GAS_RAW, &gas);
            sensor_channel_get(sensor_dev,
                               (enum sensor_channel)SENSOR_CHAN_SOUND, &sound);
            sensor_channel_get(sensor_dev,
                               (enum sensor_channel)SENSOR_CHAN_DISTANCE, &dist);
            sensor_channel_get(sensor_dev,
                               (enum sensor_channel)SENSOR_CHAN_ALARM_STATUS,
                               &alarm);

            /* ---- Debug print ---- */
            printk("[BASE] Temp: %d.%02d C | Gas: %d | Sound: %d | "
                   "Dist: %d cm | Alarm: 0x%02x\n",
                   temp.val1, temp.val2 / 10000,
                   gas.val1,
                   sound.val1,
                   dist.val1,
                   alarm.val1);

            /* ---- LED alert handling ---- */
            uint8_t alarm_flags = (uint8_t)alarm.val1;

            if (alarm_flags & ALARM_MOTION_BIT) {
                /* Highest priority: motion/intrusion → 1 blink */
                LOG_WRN("ALERT: Motion detected!");
                blink_led(&alert_led, 1);
            } else if (alarm_flags & ALARM_GAS_BIT) {
                /* Gas / environmental anomaly → 2 blinks */
                LOG_WRN("ALERT: Gas / environmental anomaly!");
                blink_led(&alert_led, 2);
            } else if (alarm_flags & ALARM_SOUND_BIT) {
                /* Abnormal sound → 3 blinks */
                LOG_WRN("ALERT: Abnormal sound detected!");
                blink_led(&alert_led, 3);
            }
            /* No alarm → LED stays off */

        } else if (ret == -EAGAIN) {
            LOG_WRN("No data from sensor node (timeout)");
        } else {
            LOG_ERR("sensor_sample_fetch error: %d", ret);
        }

        /*
         * Small delay before next fetch.  The sensor node determines
         * the real data rate; this just prevents a tight spin if frames
         * arrive faster than we process them.
         */
        k_msleep(100);
    }

    return 0;
}
