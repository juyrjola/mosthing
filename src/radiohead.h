#ifndef __MOSTHING_RADIOHEAD_H
#define __MOSTHING_RADIOHEAD_H

#ifdef __cplusplus
extern "C" {
#endif

int radiohead_init(void);
int radiohead_is_configured(void);
int radiohead_is_initialized(void);
int radiohead_send_sensor_report(const void *msg, unsigned int msg_len);

#ifdef __cplusplus
}
#endif

#endif
