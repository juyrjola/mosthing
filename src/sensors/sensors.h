#ifndef __SENSORS_H
#define __SENSORS_H

#include <stdint.h>
#include "rfreport.h"

enum value_type {
    SENSOR_INT,
    SENSOR_FLOAT,
    SENSOR_STRING,
};

struct sensor_data {
    // Common config
    char type[16];
    char *mqtt_topic;       // Which MQTT topic to use to report this sensor
    uint16_t rh_sensor_id;  // Which RadioHead sensor ID corresponds to this sensor
    int poll_delay;

    // Driver specific config
    int pin;
    int power_gpio;
    int (*poll)(struct sensor *, int);

    // Internal state
    int enabled:1;
    unsigned int *timer_id;
    void *driver_data;
    struct sensor_data *next;
};

struct sensor_measurement {
    const char *property_name;
    const char *unit;
    enum value_type type;

    int int_val;
    float float_val;
    char *char_val;
};

void sensors_init(void);
void sensors_handle_rf_report(const struct rf_sensor_report *report);
void sensors_shutdown(void);

int bme280_init(struct sensor_data *sensor);
int bme280_poll(struct sensor_data *sensor, struct sensor_measurement *out);
int dht_init(struct sensor_data *sensor);
int dht_poll(struct sensor_data *sensor, struct sensor_measurement *out);
int ds18b20_init(struct sensor_data *sensor);
int ds18b20_poll(struct sensor_data *sensor, struct sensor_measurement *out);
int mh_z19_init(struct sensor_data *sensor);
int mh_z19_poll(struct sensor_data *sensor, struct sensor_measurement *out);

#endif
