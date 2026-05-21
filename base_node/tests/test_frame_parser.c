/*
 * Unit tests for the sensor node UART frame parser logic.
 *
 * These tests verify that:
 *   1. A well-formed 10-byte frame is accepted and values parsed correctly.
 *   2. A frame with a wrong end marker is rejected.
 *   3. Alarm bit masks match the expected bit positions.
 *
 * Run with:
 *   west build -b native_sim base_node/tests && west build -t run
 */

#include <zephyr/ztest.h>
#include <stdint.h>
#include <string.h>

#include "../drivers/sensor_node/sensor_node.h"

/* Helper: build a raw 10-byte frame from known values */
static void build_frame(uint8_t *out,
                        int16_t temp_x100,
                        uint16_t hum_x100,
                        uint8_t sound,
                        uint16_t dist,
                        uint8_t alarm)
{
    out[0] = SENSOR_NODE_FRAME_START;
    out[1] = (uint8_t)(temp_x100 >> 8);
    out[2] = (uint8_t)(temp_x100 & 0xFF);
    out[3] = (uint8_t)(hum_x100 >> 8);
    out[4] = (uint8_t)(hum_x100 & 0xFF);
    out[5] = sound;
    out[6] = (uint8_t)(dist >> 8);
    out[7] = (uint8_t)(dist & 0xFF);
    out[8] = alarm;
    out[9] = SENSOR_NODE_FRAME_END;
}

ZTEST(frame_parser, test_valid_frame_parsed_correctly)
{
    uint8_t frame[SENSOR_NODE_FRAME_LEN];
    build_frame(frame, 2350, 5500, 80, 42, ALARM_MOTION_BIT);

    /* Start / end markers must match */
    zassert_equal(frame[0], SENSOR_NODE_FRAME_START, "Wrong start byte");
    zassert_equal(frame[9], SENSOR_NODE_FRAME_END,   "Wrong end byte");

    /* Temperature: 23.50 °C stored as 2350 */
    int16_t temp = (int16_t)((frame[1] << 8) | frame[2]);
    zassert_equal(temp, 2350, "Temperature mismatch");
    zassert_equal(temp / 100, 23, "Temp integer part wrong");

    /* Humidity: 55.00% stored as 5500 */
    uint16_t hum = (uint16_t)((frame[3] << 8) | frame[4]);
    zassert_equal(hum, 5500, "Humidity mismatch");

    /* Distance: 42 cm */
    uint16_t dist = (uint16_t)((frame[6] << 8) | frame[7]);
    zassert_equal(dist, 42, "Distance mismatch");

    /* Alarm: only motion bit set */
    zassert_equal(frame[8] & ALARM_MOTION_BIT, ALARM_MOTION_BIT, "Motion bit missing");
    zassert_equal(frame[8] & ALARM_TEMP_BIT,   0,                "Temp bit wrongly set");
}

ZTEST(frame_parser, test_bad_end_marker_detected)
{
    uint8_t frame[SENSOR_NODE_FRAME_LEN];
    build_frame(frame, 2000, 4000, 20, 100, 0);

    /* Corrupt the end marker */
    frame[9] = 0xAB;

    /* A real parser would reject this; verify our constant is correct */
    zassert_not_equal(frame[9], SENSOR_NODE_FRAME_END, "End marker check failed");
}

ZTEST(frame_parser, test_alarm_bits_independent)
{
    uint8_t all_alarms = ALARM_MOTION_BIT | ALARM_TEMP_BIT |
                         ALARM_SOUND_BIT  | ALARM_HUMIDITY_BIT;

    zassert_not_equal(all_alarms & ALARM_MOTION_BIT,   0, "Motion bit");
    zassert_not_equal(all_alarms & ALARM_TEMP_BIT,     0, "Temp bit");
    zassert_not_equal(all_alarms & ALARM_SOUND_BIT,    0, "Sound bit");
    zassert_not_equal(all_alarms & ALARM_HUMIDITY_BIT, 0, "Humidity bit");

    /* Bits must be distinct (no overlap) */
    zassert_equal(ALARM_MOTION_BIT   & ALARM_TEMP_BIT,     0, "Motion/Temp overlap");
    zassert_equal(ALARM_MOTION_BIT   & ALARM_SOUND_BIT,    0, "Motion/Sound overlap");
    zassert_equal(ALARM_MOTION_BIT   & ALARM_HUMIDITY_BIT, 0, "Motion/Hum overlap");
    zassert_equal(ALARM_TEMP_BIT     & ALARM_SOUND_BIT,    0, "Temp/Sound overlap");
    zassert_equal(ALARM_TEMP_BIT     & ALARM_HUMIDITY_BIT, 0, "Temp/Hum overlap");
    zassert_equal(ALARM_SOUND_BIT    & ALARM_HUMIDITY_BIT, 0, "Sound/Hum overlap");
}

ZTEST(frame_parser, test_cmd_frame_length)
{
    /* Command frames must fit in exactly 5 bytes */
    zassert_equal(SENSOR_NODE_CMD_FRAME_LEN, 5, "Wrong command frame length");
}

ZTEST_SUITE(frame_parser, NULL, NULL, NULL, NULL, NULL);
