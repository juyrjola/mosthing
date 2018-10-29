#ifndef __ACTUATORS_H
#define __ACTUATORS_H

#include <stdint.h>
#include "frozen.h"

enum actuator_type {
    ACTUATOR_RELAY,
    ACTUATOR_SELECT,
};

enum actuator_gpio_state {
    ACTUATOR_GPIO_HIGH,
    ACTUATOR_GPIO_LOW,
    ACTUATOR_GPIO_FLOAT,
};

struct actuator {
    // Common config
    enum actuator_type type;
    char *mqtt_control_topic;   // Which MQTT topic to use to listen to
    char *mqtt_state_topic;     // Which MQTT topic to send state changes to
    unsigned int max_time;

    // Driver specific config
    int gpio;
    enum actuator_gpio_state gpio_active_state, gpio_inactive_state;
    struct json_token config;

    // Internal state
    int enabled:1;
    unsigned int timer_id;

    void *driver_data;
    const struct actuator_driver *driver;

    struct actuator *next;
};

struct actuator_driver {
    const char *name;
    enum actuator_type type;

    int (* init)(struct actuator *);
    int (* set)(struct actuator *, const char *);
};


void actuators_init(void);
void actuators_shutdown(void);

/* Helper functions */
int actuators_parse_onoff(const char *);
void actuators_set_gpio(struct actuator *act, int gpio, int new_state);

extern struct actuator_driver gpio_relay_driver;
extern struct actuator_driver gpio_select_driver;

#endif
