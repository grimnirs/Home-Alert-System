/*
 * Copyright (c) 2025 Group 8
 * SPDX-License-Identifier: Apache-2.0
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
 */

#define DT_DRV_COMPAT custom_sensor_node

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "sensor_node.h"

LOG_MODULE_REGISTER(sensor_node, CONFIG_SENSOR_LOG_LEVEL);

/* ------------------------------------------------------------------ */
/*  Per-instance run-time data                                        */
/* ------------------------------------------------------------------ */
struct sensor_node_data {
    /* UART device handle (resolved once during init) */
    const struct device *uart_dev;

    /* --- ISR ring state --- */
    uint8_t  rx_buf[SENSOR_NODE_FRAME_LEN];  /* assembling frame   */
    uint8_t  rx_idx;                          /* write position     */

    /* --- Double buffer: ISR writes, fetch reads --- */
    uint8_t  ready_buf[SENSOR_NODE_FRAME_LEN];
    struct k_sem frame_sem;                   /* given by ISR       */

    /* --- Parsed sensor values (filled by fetch) --- */
    int16_t  temperature;   /* scaled value from sensor node        */
    uint16_t gas;           /* raw / threshold index                */
    uint8_t  sound;         /* ADC 0-255                            */
    uint16_t distance;      /* cm                                   */
    uint8_t  alarm_status;  /* bit0=motion, bit1=gas, bit2=sound    */
};

/* ------------------------------------------------------------------ */
/*  Per-instance config (from devicetree, constant)                   */
/* ------------------------------------------------------------------ */
struct sensor_node_config {
    const struct device *uart_dev;
};

/* ================================================================== */
/*  UART interrupt callback                                           */
/* ================================================================== */
static void sensor_node_uart_isr(const struct device *uart_dev,
                                 void *user_data)
{
    const struct device *dev = user_data;
    struct sensor_node_data *data = dev->data;
    uint8_t byte;

    /* Drain the FIFO one byte at a time */
    while (uart_irq_update(uart_dev) &&
           uart_irq_rx_ready(uart_dev)) {

        if (uart_fifo_read(uart_dev, &byte, 1) != 1) {
            continue;
        }

        if (data->rx_idx == 0) {
            /* Waiting for start marker */
            if (byte == SENSOR_NODE_FRAME_START) {
                data->rx_buf[0] = byte;
                data->rx_idx = 1;
            }
            /* else: discard stray bytes */
        } else {
            data->rx_buf[data->rx_idx++] = byte;

            if (data->rx_idx == SENSOR_NODE_FRAME_LEN) {
                /* Got a full-length frame – validate end marker */
                if (byte == SENSOR_NODE_FRAME_END) {
                    /* Copy to ready buffer and signal */
                    memcpy(data->ready_buf, data->rx_buf,
                           SENSOR_NODE_FRAME_LEN);
                    k_sem_give(&data->frame_sem);
                    LOG_DBG("Valid frame received");
                } else {
                    LOG_WRN("Frame end marker mismatch: 0x%02x", byte);
                }
                /* Reset whether valid or not */
                data->rx_idx = 0;
            }
        }
    }
}

/* ================================================================== */
/*  Sensor API: sample_fetch                                          */
/* ================================================================== */
static int sensor_node_sample_fetch(const struct device *dev,
                                    enum sensor_channel chan)
{
    struct sensor_node_data *data = dev->data;
    int ret;

    ARG_UNUSED(chan);  /* we always fetch the whole frame */

    /*
     * Wait up to 2 seconds for a new frame.  If nothing arrives the
     * application gets -EAGAIN and can decide what to do.
     */
    ret = k_sem_take(&data->frame_sem, K_MSEC(2000));
    if (ret < 0) {
        LOG_WRN("No frame received within timeout");
        return -EAGAIN;
    }

    /* Parse the ready buffer (big-endian, matching sensor node packing) */
    const uint8_t *f = data->ready_buf;

    data->temperature  = (int16_t)((f[1] << 8) | f[2]);
    data->gas          = (uint16_t)((f[3] << 8) | f[4]);
    data->sound        = f[5];
    data->distance     = (uint16_t)((f[6] << 8) | f[7]);
    data->alarm_status = f[8];

    LOG_DBG("Parsed: temp=%d gas=%u sound=%u dist=%u alarm=0x%02x",
            data->temperature, data->gas, data->sound,
            data->distance, data->alarm_status);

    return 0;
}

/* ================================================================== */
/*  Sensor API: channel_get                                           */
/* ================================================================== */
static int sensor_node_channel_get(const struct device *dev,
                                   enum sensor_channel chan,
                                   struct sensor_value *val)
{
    struct sensor_node_data *data = dev->data;

    switch ((int)chan) {

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

    case SENSOR_CHAN_GAS_RAW:
        val->val1 = data->gas;
        val->val2 = 0;
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

/* ================================================================== */
/*  Driver init                                                       */
/* ================================================================== */
static int sensor_node_init(const struct device *dev)
{
    struct sensor_node_data *data = dev->data;
    const struct sensor_node_config *cfg = dev->config;

    data->uart_dev = cfg->uart_dev;
    data->rx_idx = 0;

    k_sem_init(&data->frame_sem, 0, 1);

    if (!device_is_ready(data->uart_dev)) {
        LOG_ERR("UART device not ready");
        return -ENODEV;
    }

    /* Configure UART interrupt-driven reception */
    uart_irq_callback_user_data_set(data->uart_dev,
                                    sensor_node_uart_isr,
                                    (void *)dev);
    uart_irq_rx_enable(data->uart_dev);

    LOG_INF("Sensor node driver initialised (UART: %s)",
            data->uart_dev->name);

    return 0;
}

/* ================================================================== */
/*  Driver API table                                                  */
/* ================================================================== */
static const struct sensor_driver_api sensor_node_api = {
    .sample_fetch = sensor_node_sample_fetch,
    .channel_get  = sensor_node_channel_get,
};

/* ================================================================== */
/*  Instantiation macro (one instance per devicetree node)            */
/* ================================================================== */
#define SENSOR_NODE_INST(inst)                                          \
                                                                        \
    static struct sensor_node_data sensor_node_data_##inst;             \
                                                                        \
    static const struct sensor_node_config sensor_node_cfg_##inst = {   \
        .uart_dev = DEVICE_DT_GET(DT_INST_PHANDLE(inst, uart_dev)),    \
    };                                                                  \
                                                                        \
    SENSOR_DEVICE_DT_INST_DEFINE(                                       \
        inst,                                                           \
        sensor_node_init,                                               \
        NULL,                           /* pm */                        \
        &sensor_node_data_##inst,                                       \
        &sensor_node_cfg_##inst,                                        \
        POST_KERNEL,                                                    \
        CONFIG_SENSOR_INIT_PRIORITY,                                    \
        &sensor_node_api);

DT_INST_FOREACH_STATUS_OKAY(SENSOR_NODE_INST)
