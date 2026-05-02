/*
 * Copyright (c) 2025 Group 8
 * SPDX-License-Identifier: Apache-2.0
 *
 * Custom Zephyr sensor driver for the Home Security Base Node.
 * Receives 10-byte UART frames from the Sensor Node and exposes
 * the data through the standard Zephyr Sensor API.
 */

#ifndef SENSOR_NODE_H
#define SENSOR_NODE_H

#include <zephyr/drivers/sensor.h>

/*
 * Custom sensor channels for data that has no standard Zephyr channel.
 * SENSOR_CHAN_PRIV_START is the first channel ID reserved for drivers.
 */
enum sensor_node_channel {
    /** Gas / air quality raw value (arbitrary / ppm equivalent) */
    SENSOR_CHAN_GAS_RAW = SENSOR_CHAN_PRIV_START,

    /** Sound intensity level (0–255 ADC level) */
    SENSOR_CHAN_SOUND,

    /** Distance in cm from HC-SR04 */
    SENSOR_CHAN_DISTANCE,

    /**
     * Alarm status bitfield:
     *   bit 0 = motion detected  (HC-SR04 proximity)
     *   bit 1 = gas alert        (BME680)
     *   bit 2 = sound alert      (LM386)
     */
    SENSOR_CHAN_ALARM_STATUS,
};

/*
 * UART frame format (10 bytes, big-endian multi-byte fields):
 *
 *  Byte  Field         Type       Description
 *  ----  -----------   --------   ---------------------------
 *   0    Start Byte    0xAA       Frame synchronisation
 *   1    Temp_H        int16 hi   Scaled temperature value
 *   2    Temp_L        int16 lo
 *   3    Gas_H         uint16 hi  Gas / air quality index
 *   4    Gas_L         uint16 lo
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
#define ALARM_MOTION_BIT  BIT(0)
#define ALARM_GAS_BIT     BIT(1)
#define ALARM_SOUND_BIT   BIT(2)

#endif /* SENSOR_NODE_H */
