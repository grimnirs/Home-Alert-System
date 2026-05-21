/* shared protocol constants - included by both sensor node and base node */
#pragma once

/* Data frame: sensor node -> base node, 10 bytes */
#define PROTO_DATA_START    0xAA
#define PROTO_DATA_END      0x55
#define PROTO_DATA_LEN      10

/* Config command frame: base node -> sensor node, 5 bytes */
#define PROTO_CMD_START     0xBB
#define PROTO_CMD_END       0x66
#define PROTO_CMD_LEN       5

/* Command IDs */
#define PROTO_CMD_DIST_THRESH   0x01
#define PROTO_CMD_TEMP_MAX      0x02
#define PROTO_CMD_SOUND_THRESH  0x03
#define PROTO_CMD_HUM_MAX       0x04
#define PROTO_CMD_HUM_MIN       0x05

/* Alarm bit positions in A_Status byte */
#define PROTO_ALARM_MOTION   (1u << 0)
#define PROTO_ALARM_TEMP     (1u << 1)
#define PROTO_ALARM_SOUND    (1u << 2)
#define PROTO_ALARM_HUMIDITY (1u << 4)
