/*
 *
 * Zephyr sensor driver for Home Security Base Node.
 *
 * Workflow:
 *   1. UART ISR receives bytes one at a time.
 *   2. A state machine synchronises on 0xAA, fills a 10-byte buffer,
 *      and validates the 0x55 end marker.
 *   3. On a valid frame the ISR copies the payload into a "ready" buffer
 *      and gives a semaphore.
 *   4. sensor_sample_fetch() takes the semaphore (with timeout) and
 *      parses the ready buffer into typed fields.
 *   5. sensor_channel_get() returns whichever channel the application asks for.
 *   6. sensor_attr_set() serialises a threshold change into a 5-byte command
 *      frame and sends it back to the sensor node over UART TX.
 *   7. sensor_trigger_set() arms a GPIO interrupt on the intr-gpios pin so the
 *      application is notified the moment the sensor node asserts its alarm line.
 */

#define DT_DRV_COMPAT zephyr_sensor_node

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "sensor_node.h"

LOG_MODULE_REGISTER(sensor_node, CONFIG_SENSOR_LOG_LEVEL);


// Per-instance run-time data
struct sensor_node_data
{
    /* UART device handle (resolved once during init) */
    const struct device *uart_dev;

    // ISR ring state
    uint8_t rx_buf[SENSOR_NODE_FRAME_LEN]; /* assembling frame   */
    uint8_t rx_idx;                        /* write position     */

    //Double buffer: ISR writes, fetch reads
    uint8_t ready_buf[SENSOR_NODE_FRAME_LEN];
    struct k_sem frame_sem; /* given by ISR       */

    // Parsed sensor values (filled by fetch)
    int16_t temperature;  /* scaled value from sensor node        */
    uint16_t humidity;    /* scaled humidity ×100                  */
    uint8_t sound;        /* ADC 0-255                            */
    uint16_t distance;    /* cm                                   */
    uint8_t alarm_status; /* bit0=motion, bit1=temp, bit2=sound, bit4=humidity */

    // Interrupt / trigger support
    struct gpio_callback intr_gpio_cb;
    sensor_trigger_handler_t trigger_handler;
    const struct device *trigger_dev;   /* pointer back to our own device */
    struct sensor_trigger trig;
};

// Per-instance config (from devicetree, constant)
struct sensor_node_config
{
    const struct device *uart_dev;
    struct gpio_dt_spec intr_gpio;      /* interrupt line from sensor node */
};


// GPIO interrupt callback - called from ISR when sensor node asserts INTR_PIN
static void sensor_node_intr_gpio_cb(const struct device *port,
                                     struct gpio_callback *cb,
                                     gpio_port_pins_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(pins);

    struct sensor_node_data *data =
        CONTAINER_OF(cb, struct sensor_node_data, intr_gpio_cb);

    if (data->trigger_handler) {
        data->trigger_handler(data->trigger_dev, &data->trig);
    }
}

// UART interrupt callback
static void sensor_node_uart_isr(const struct device *uart_dev,
                                 void *user_data)
{
    const struct device *dev = user_data;
    struct sensor_node_data *data = dev->data;
    uint8_t byte;

    /* Drain the FIFO one byte at a time */
    uart_irq_update(uart_dev);

    while (uart_irq_rx_ready(uart_dev))
    {

        if (uart_fifo_read(uart_dev, &byte, 1) != 1)
        {
            continue;
        }

        if (data->rx_idx == 0)
        {
            /* Waiting for start marker */
            if (byte == SENSOR_NODE_FRAME_START)
            {
                data->rx_buf[0] = byte;
                data->rx_idx = 1;
            }
            /* else: discard stray bytes */
        }
        else
        {
            data->rx_buf[data->rx_idx++] = byte;

            if (data->rx_idx == SENSOR_NODE_FRAME_LEN)
            {
                /* Got a full-length frame – validate end marker */
                if (byte == SENSOR_NODE_FRAME_END)
                {
                    /* Copy to ready buffer and signal */
                    memcpy(data->ready_buf, data->rx_buf,
                           SENSOR_NODE_FRAME_LEN);
                    k_sem_give(&data->frame_sem);
                    LOG_DBG("Valid frame received");
                }
                else
                {
                    LOG_WRN("Frame end marker mismatch: 0x%02x", byte);
                }
                /* Reset whether valid or not */
                data->rx_idx = 0;
            }
        }
    }
}

// Sensor API: sample_fetch
static int sensor_node_sample_fetch(const struct device *dev,
                                    enum sensor_channel chan)
{
    struct sensor_node_data *data = dev->data;
    int ret;

    ARG_UNUSED(chan); /* we always fetch the whole frame */

    /*
     * Wait up to 2 seconds for a new frame.  If nothing arrives the
     * application gets -EAGAIN and can decide what to do.
     */
    ret = k_sem_take(&data->frame_sem, K_SECONDS(2));
    if (ret < 0)
    {
        LOG_DBG("[sensor_node] No frame received within timeout");
        return -EAGAIN;
    }

    /* Parse the ready buffer (big-endian, matching sensor node packing) */
    const uint8_t *f = data->ready_buf;

    data->temperature = (int16_t)((f[1] << 8) | f[2]);
    data->humidity = (uint16_t)((f[3] << 8) | f[4]);
    data->sound = f[5];
    data->distance = (uint16_t)((f[6] << 8) | f[7]);
    data->alarm_status = f[8];

    LOG_DBG("[sensor_node] Parsed: temp=%d humidity=%u sound=%u dist=%u alarm=0x%02x",
            data->temperature, data->humidity, data->sound,
            data->distance, data->alarm_status);

    return 0;
}

// Sensor API: channel_get
static int sensor_node_channel_get(const struct device *dev,
                                   enum sensor_channel chan,
                                   struct sensor_value *val)
{
    struct sensor_node_data *data = dev->data;

    switch ((int)chan)
    {

    case SENSOR_CHAN_AMBIENT_TEMP:
        /*
         * Convention with the sensor node:
         *   int16 value = temperature × 100
         *   e.g. 2350 → 23.50 °C
         *
         * Zephyr sensor_value:
         *   val1 = integer part
         *   val2 = fractional part in millionths
         */
        val->val1 = data->temperature / 100;
        val->val2 = (data->temperature % 100) * 10000;
        break;

    case SENSOR_CHAN_HUMIDITY:
        val->val1 = data->humidity / 100;
        val->val2 = (data->humidity % 100) * 10000;
        break;

    case SENSOR_CHAN_SOUND:
        val->val1 = data->sound;
        val->val2 = 0;
        break;

    case SENSOR_CHAN_DISTANCE:
        val->val1 = data->distance;
        val->val2 = 0;
        break;

    case SENSOR_CHAN_ALARM_STATUS:
        val->val1 = data->alarm_status;
        val->val2 = 0;
        break;

    default:
        LOG_ERR("Unsupported channel: %d", chan);
        return -ENOTSUP;
    }

    return 0;
}

// Sensor API: attr_set - send a configuration command to the sensor node
static int sensor_node_attr_set(const struct device *dev,
                                enum sensor_channel chan,
                                enum sensor_attribute attr,
                                const struct sensor_value *val)
{
    struct sensor_node_data *data = dev->data;
    uint8_t cmd;
    uint16_t raw;

    ARG_UNUSED(chan);

    switch ((int)attr)
    {
    case SENSOR_ATTR_DIST_THRESH:
        cmd = SENSOR_NODE_CMD_DIST_THRESH;
        raw = (uint16_t)val->val1;
        break;

    case SENSOR_ATTR_TEMP_MAX:
        cmd = SENSOR_NODE_CMD_TEMP_MAX;
        /* encode as int16 ×100 to match sensor node representation */
        raw = (uint16_t)(int16_t)(val->val1 * 100 + val->val2 / 10000);
        break;

    case SENSOR_ATTR_SOUND_THRESH:
        cmd = SENSOR_NODE_CMD_SOUND_THRESH;
        raw = (uint16_t)(val->val1 & 0x3F);
        break;

    case SENSOR_ATTR_HUM_MAX:
        cmd = SENSOR_NODE_CMD_HUM_MAX;
        raw = (uint16_t)(val->val1 * 100 + val->val2 / 10000);
        break;

    case SENSOR_ATTR_HUM_MIN:
        cmd = SENSOR_NODE_CMD_HUM_MIN;
        raw = (uint16_t)(val->val1 * 100 + val->val2 / 10000);
        break;

    default:
        LOG_ERR("[sensor_node] Unknown attribute: %d", attr);
        return -ENOTSUP;
    }

    uint8_t frame[SENSOR_NODE_CMD_FRAME_LEN] = {
        SENSOR_NODE_CMD_START,
        cmd,
        (uint8_t)(raw >> 8),
        (uint8_t)(raw & 0xFF),
        SENSOR_NODE_CMD_END,
    };

    /* Polling TX - safe to call from thread context alongside interrupt-driven RX */
    for (int i = 0; i < SENSOR_NODE_CMD_FRAME_LEN; i++) {
        uart_poll_out(data->uart_dev, frame[i]);
    }

    LOG_INF("[sensor_node] Config sent: cmd=0x%02x val=%u", cmd, raw);
    return 0;
}

// Sensor API: trigger_set - arm/disarm the GPIO interrupt line
static int sensor_node_trigger_set(const struct device *dev,
                                   const struct sensor_trigger *trig,
                                   sensor_trigger_handler_t handler)
{
    struct sensor_node_data *data = dev->data;
    const struct sensor_node_config *cfg = dev->config;

    if (trig->type != SENSOR_TRIG_THRESHOLD) {
        LOG_ERR("[sensor_node] Only SENSOR_TRIG_THRESHOLD is supported");
        return -ENOTSUP;
    }

    if (!cfg->intr_gpio.port) {
        LOG_ERR("[sensor_node] No interrupt GPIO configured in devicetree");
        return -ENODEV;
    }

    data->trig = *trig;
    data->trigger_dev = dev;
    data->trigger_handler = handler;

    if (handler) {
        gpio_pin_interrupt_configure_dt(&cfg->intr_gpio, GPIO_INT_EDGE_RISING);
        LOG_INF("[sensor_node] Interrupt trigger armed (GPIO pin %d)", cfg->intr_gpio.pin);
    } else {
        gpio_pin_interrupt_configure_dt(&cfg->intr_gpio, GPIO_INT_DISABLE);
        LOG_INF("[sensor_node] Interrupt trigger disarmed");
    }

    return 0;
}


// Driver init
static int sensor_node_init(const struct device *dev)
{
    struct sensor_node_data *data = dev->data;
    const struct sensor_node_config *cfg = dev->config;

    LOG_INF("[sensor_node] Initializing...");

    data->uart_dev = cfg->uart_dev;
    data->rx_idx = 0;

    if (!data->uart_dev)
    {
        LOG_ERR("[sensor_node] UART device pointer is NULL!");
        return -ENODEV;
    }

    LOG_INF("[sensor_node] UART device obtained: %s", data->uart_dev->name);

    k_sem_init(&data->frame_sem, 0, 1);

    if (!device_is_ready(data->uart_dev))
    {
        LOG_ERR("[sensor_node] UART device not ready: %s", data->uart_dev->name);
        return -ENODEV;
    }

    LOG_INF("[sensor_node] UART device ready");

    /* Configure UART interrupt-driven reception */
    uart_irq_callback_user_data_set(data->uart_dev,
                                    sensor_node_uart_isr,
                                    (void *)dev);
    uart_irq_rx_enable(data->uart_dev);

    LOG_INF("[sensor_node] UART ISR configured and RX enabled");

    /* Set up the interrupt GPIO input if wired in the devicetree */
    if (cfg->intr_gpio.port) {
        if (!device_is_ready(cfg->intr_gpio.port)) {
            LOG_ERR("[sensor_node] Interrupt GPIO port not ready");
            return -ENODEV;
        }

        gpio_pin_configure_dt(&cfg->intr_gpio, GPIO_INPUT);
        gpio_init_callback(&data->intr_gpio_cb,
                           sensor_node_intr_gpio_cb,
                           BIT(cfg->intr_gpio.pin));
        gpio_add_callback(cfg->intr_gpio.port, &data->intr_gpio_cb);

        /* Leave interrupt disabled until trigger_set is called */
        gpio_pin_interrupt_configure_dt(&cfg->intr_gpio, GPIO_INT_DISABLE);

        LOG_INF("[sensor_node] Interrupt GPIO configured (pin %d)", cfg->intr_gpio.pin);
    }

    LOG_INF("[sensor_node] Driver initialised (UART: %s)",
            data->uart_dev->name);

    return 0;
}


// Driver API table
static const struct sensor_driver_api sensor_node_api = {
    .sample_fetch = sensor_node_sample_fetch,
    .channel_get  = sensor_node_channel_get,
    .attr_set     = sensor_node_attr_set,
    .trigger_set  = sensor_node_trigger_set,
};

// Instantiation macro (one instance per devicetree node)
#define SENSOR_NODE_INST(inst)                                               \
                                                                             \
    static struct sensor_node_data sensor_node_data_##inst;                  \
                                                                             \
    static const struct sensor_node_config sensor_node_cfg_##inst = {        \
        .uart_dev  = DEVICE_DT_GET(DT_INST_PHANDLE(inst, uart_dev)),         \
        .intr_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, intr_gpios, {0}),        \
    };                                                                       \
                                                                             \
    SENSOR_DEVICE_DT_INST_DEFINE(                                            \
        inst,                                                                \
        sensor_node_init,                                                    \
        NULL, /* pm */                                                       \
        &sensor_node_data_##inst,                                            \
        &sensor_node_cfg_##inst,                                             \
        POST_KERNEL,                                                         \
        CONFIG_SENSOR_INIT_PRIORITY,                                         \
        &sensor_node_api);

DT_INST_FOREACH_STATUS_OKAY(SENSOR_NODE_INST)
