/*
 Protocol:
 
 - Protocol version: 4 bits
 - Number of observations: 4 bits
 - Sensor ID: 32 bits
 - Observations
   - Phenomenon type: 8 bits
   - Value type: 4 bits
   - Padding: 28 bits
   - Value: 32 bits
 - CRC32: 32 bits
 */

#ifndef __RFREPORT_H
#define __RFREPORT_H

#include <stdint.h>

#define RF_PHENOMENON_TEMPERATURE       0
#define RF_PHENOMENON_HUMIDITY          1
#define RF_PHENOMENON_LUMINOSITY        2

#define RF_PHENOMENON_MAX               2

#define RF_VALUE_FLOAT                  0

#define RF_PROTOCOL_V1                  1

struct rf_sensor_observation {
    uint8_t phenomenon;
    int value_type:4, padding:4;
    union {
        float float_val;
        int32_t int_val;
    } value;
} __attribute__((packed));

struct rf_sensor_report {
    int proto_ver:4;
    int n_observations:4;
    int padding:8;
    uint16_t sensor_id;
    uint32_t crc;
    struct rf_sensor_observation observations[];
} __attribute__((packed));


void rf_report_encode_msg(uint16_t sensor_id, unsigned int n_observations,
                          const struct rf_sensor_observation *observations,
                          uint8_t **buf_out, unsigned int *buf_len);

int rf_report_decode_msg(const uint8_t *buf, unsigned int buf_len,
                         struct rf_sensor_report **out);

#endif
