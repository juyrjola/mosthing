#ifndef __RADIOHEAD_SENSOR_H
#define __RADIOHEAD_SENSOR_H

#include <stdint.h>
#include "rfreport.h"

int rh_sensor_init();
int rh_sensor_is_initialized();
int rh_sensor_handle_message(const uint8_t *buf, unsigned int buf_len);
int rh_sensor_send_message(const uint8_t *buf, unsigned int buf_len);

#endif
