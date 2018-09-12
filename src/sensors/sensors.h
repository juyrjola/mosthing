#ifndef __SENSORS_H
#define __SENSORS_H

#include <stdint.h>
#include "rfreport.h"

enum value_type {
    SENSOR_INT,
    SENSOR_FLOAT,
    SENSOR_STRING,
};

struct sensor_measurement {
    const char *property_name;
    const char *unit;
    enum value_type type;
    uint8_t precision;   // How many decimals are valid

    int int_val;
    float float_val;
    char *char_val;
};

struct sensor {
    // Common config
    char type[16];
    char *mqtt_topic;       // Which MQTT topic to use to report this sensor
    uint16_t rh_sensor_id;  // Which RadioHead sensor ID corresponds to this sensor
    int poll_delay;

    // Driver specific config
    int gpio;
    int power_gpio;
    int output_gpio;

    // Internal state
    int enabled:1;
    unsigned int timer_id;
    void *driver_data;
    int (*poll)(struct sensor *, struct sensor_measurement *);
    int (*shutdown)(struct sensor *);

    struct sensor *next;
};

void sensors_init(void);
void sensors_report(struct sensor *sensor, const struct sensor_measurement *values, int n_values);
void sensors_handle_rf_report(const struct rf_sensor_report *report);
void sensors_shutdown(void);

int bme280_init(struct sensor *sensor);
int bme280_poll(struct sensor *sensor, struct sensor_measurement *out);
int dht_init(struct sensor *sensor);
int dht_poll(struct sensor *sensor, struct sensor_measurement *out);
int ds18b20_init(struct sensor *sensor);
int ds18b20_poll(struct sensor *sensor, struct sensor_measurement *out);
int mh_z19_init(struct sensor *sensor);
int mh_z19_poll(struct sensor *sensor, struct sensor_measurement *out);
int gpio_ultrasound_init(struct sensor *sensor);
int gpio_ultrasound_poll(struct sensor *sensor, struct sensor_measurement *out);
int soil_moisture_init(struct sensor *sensor);
int soil_moisture_poll(struct sensor *sensor, struct sensor_measurement *out);

#endif
