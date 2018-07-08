#ifndef __ACTUATORS_H
#define __ACTUATORS_H

#include <stdint.h>

enum actuator_type {
    ACTUATOR_RELAY,
};

struct actuator {
    // Common config
    enum actuator_type type;
    char driver[16];
    char *mqtt_topic;       // Which MQTT topic to use to listen to
    unsigned int max_time;

    // Driver specific config
    int gpio;
    bool active_low;

    // Internal state
    int enabled:1;
    unsigned int timer_id;

    void *driver_data;
    int (*driver_set)(struct actuator *, int);

    struct actuator *next;
};

void actuators_init(void);
void actuators_shutdown(void);

int gpio_relay_init(struct actuator *actuator);
int gpio_relay_set(struct actuator *actuator, int value);

#endif
