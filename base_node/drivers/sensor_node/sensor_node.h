/*
 *
 * Custom Zephyr sensor driver for the Home Security Base Node.
 * Receives 10-byte UART frames from the Sensor Node and exposes
 * the data through the standard Zephyr Sensor API.
 */

#ifndef SENSOR_NODE_H
#define SENSOR_NODE_H

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>

/*
 * Custom sensor channels for data that has no standard Zephyr channel.
 * SENSOR_CHAN_PRIV_START is the first channel ID reserved for drivers.
 * Note: SENSOR_CHAN_HUMIDITY and SENSOR_CHAN_DISTANCE are standard Zephyr channels.
 */
enum sensor_node_channel {
    // Sound intensity level (0–255 ADC level)
    SENSOR_CHAN_SOUND = SENSOR_CHAN_PRIV_START,

    /**
     * Alarm status bitfield:
     *   bit 0 = motion detected    (HC-SR04 proximity)
     *   bit 1 = temperature alert  (BME680)
     *   bit 2 = sound alert        (LM386)
     *   bit 4 = humidity alert     (BME680)
     */
    SENSOR_CHAN_ALARM_STATUS,
};

/*
 * Custom attributes for configuring alarm thresholds on the sensor node.
 * Each sensor_attr_set() call sends a 5-byte command frame over UART to
 * the sensor node, which updates its runtime threshold.
 */
enum sensor_node_attr {
    SENSOR_ATTR_DIST_THRESH = SENSOR_ATTR_PRIV_START, /* distance threshold (cm, .val1 only) */
    SENSOR_ATTR_TEMP_MAX,       /* temperature upper limit (°C, .val1=int, .val2=frac×1e6) */
    SENSOR_ATTR_SOUND_THRESH,   /* sound level threshold (scaled 0..63, .val1 only) */
    SENSOR_ATTR_HUM_MAX,        /* humidity upper bound (%, standard sensor_value format) */
    SENSOR_ATTR_HUM_MIN,        /* humidity lower bound */
};

/*
 * UART frame format (10 bytes, big-endian multi-byte fields):
 *
 *  Byte  Field         Type       Description
 *  ----  -----------   --------   ---------------------------
 *   0    Start Byte    0xAA       Frame synchronisation
 *   1    Temp_H        int16 hi   Scaled temperature value
 *   2    Temp_L        int16 lo
 *   3    Hum_H         uint16 hi  Humidity × 100
 *   4    Hum_L         uint16 lo
 *   5    Sound         uint8      Sound intensity (ADC 0–255)
 *   6    Dist_H        uint16 hi  Distance in cm
 *   7    Dist_L        uint16 lo
 *   8    A_Status      bitfield   Alarm flags
 *   9    End Byte      0x55       Frame termination
 */

#define SENSOR_NODE_FRAME_START  0xAA
#define SENSOR_NODE_FRAME_END    0x55
#define SENSOR_NODE_FRAME_LEN    10

/* Alarm status bit masks */
#define ALARM_MOTION_BIT   BIT(0)
#define ALARM_TEMP_BIT     BIT(1)
#define ALARM_SOUND_BIT    BIT(2)
#define ALARM_HUMIDITY_BIT BIT(4)

/*
 * Config command frame (base node → sensor node), 5 bytes:
 *   [0xBB][CMD][VAL_H][VAL_L][0x66]
 *
 * VAL_H:VAL_L is big-endian uint16. For temperature it is reinterpreted
 * as int16 on the sensor node side (see sensor node firmware).
 */
#define SENSOR_NODE_CMD_START        0xBB
#define SENSOR_NODE_CMD_END          0x66
#define SENSOR_NODE_CMD_FRAME_LEN    5

#define SENSOR_NODE_CMD_DIST_THRESH  0x01   /* proximity threshold (cm) */
#define SENSOR_NODE_CMD_TEMP_MAX     0x02   /* temperature limit (°C × 100) */
#define SENSOR_NODE_CMD_SOUND_THRESH 0x03   /* sound threshold (scaled 0..63) */
#define SENSOR_NODE_CMD_HUM_MAX      0x04   /* humidity upper bound (% × 100) */
#define SENSOR_NODE_CMD_HUM_MIN      0x05   /* humidity lower bound (% × 100) */

#endif /* SENSOR_NODE_H */
