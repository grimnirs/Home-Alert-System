/*
 * Home Security Base Node – main application.
 *
 * Periodically fetches sensor data from the custom sensor-node driver,
 * prints the values to the serial monitor, and drives four different alert LEDs
 * depending on which alarm flags are set.
 *
 * Interrupt demo:
 *   On startup we register a SENSOR_TRIG_THRESHOLD callback that fires
 *   immediately when the sensor node asserts its interrupt GPIO line.
 *   We also send a configuration command to tighten the distance threshold
 *   to 30 cm so the interrupt is easier to trigger during testing.
 *
 * Alarm LEDs:
 *   - motion_led:   GPIO 20
 *   - temp_led:     GPIO 18
 *   - sound_led:    GPIO 19
 *   - humidity_led: GPIO 21
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>  // 
#include <zephyr/drivers/uart.h>    // 

#include "../drivers/sensor_node/sensor_node.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// Devicetree handles

#define SENSOR_NODE DT_NODELABEL(sensor_node)

#define MOTION_LED_NODE   DT_ALIAS(motion_led)
#define TEMP_LED_NODE     DT_ALIAS(temp_led)
#define SOUND_LED_NODE    DT_ALIAS(sound_led)
#define HUMIDITY_LED_NODE DT_ALIAS(humidity_led)

static const struct gpio_dt_spec motion_led =
    GPIO_DT_SPEC_GET(MOTION_LED_NODE, gpios);

static const struct gpio_dt_spec temp_led =
    GPIO_DT_SPEC_GET(TEMP_LED_NODE, gpios);

static const struct gpio_dt_spec sound_led =
    GPIO_DT_SPEC_GET(SOUND_LED_NODE, gpios);

static const struct gpio_dt_spec humidity_led =
    GPIO_DT_SPEC_GET(HUMIDITY_LED_NODE, gpios);

/*
 * Interrupt callback - called from GPIO ISR context when the sensor node
 * pulls its INTR_PIN high (i.e. any alarm condition becomes active).
 * Keep this short; heavy work should be deferred to a thread if needed.
 */
static void alarm_trigger_cb(const struct device *dev,
                             const struct sensor_trigger *trig)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(trig);
    printk("[INTERRUPT] Sensor node alarm line asserted!\n");
}

// Main

int main(void)
{   
    
    const struct device *usb_dev = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
    if (!device_is_ready(usb_dev)) {
        return -ENODEV;
    }
    usb_enable(NULL);
    k_sleep(K_SECONDS(1)); 

    k_sleep(K_SECONDS(2));

    printk(" Home Security Base Node Started\n");

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

    /*
     * Register an interrupt trigger so we get an immediate callback the
     * moment the sensor node asserts its alarm line over GPIO, without
     * waiting for the next UART frame poll cycle.
     */
    struct sensor_trigger alarm_trig = {
        .type = SENSOR_TRIG_THRESHOLD,
        .chan = SENSOR_CHAN_ALARM_STATUS,
    };
    int ret = sensor_trigger_set(sensor_dev, &alarm_trig, alarm_trigger_cb);
    if (ret < 0) {
        printk("WARNING: Could not register alarm trigger (%d)\n", ret);
    } else {
        printk("[DEMO] Interrupt trigger registered on GPIO\n");
    }

    /*
     * Example of threshold configuration: lower the proximity alert distance
     * to 30 cm by sending a command frame to the sensor node over UART.
     * The sensor node applies the new value immediately on next loop iteration.
     */
    struct sensor_value dist_thresh = { .val1 = 30, .val2 = 0 };
    sensor_attr_set(sensor_dev,
                    SENSOR_CHAN_DISTANCE,
                    (enum sensor_attribute)SENSOR_ATTR_DIST_THRESH,
                    &dist_thresh);
    printk("[DEMO] Distance alert threshold set to 30 cm\n");

    while (1)
    {
        ret = sensor_sample_fetch(sensor_dev);

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

            // LED logic

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
