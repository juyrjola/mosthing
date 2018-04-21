#ifndef SENSORS_H
#define SENSORS_H

enum value_type {
    SENSOR_INT,
    SENSOR_FLOAT,
    SENSOR_STRING,
};

struct sensor_data {
    // Common config
    char type[16];
    char *mqtt_topic;
    int poll_delay;

    // Driver specific config
    int pin;

    // Internal state
    bool enabled;
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

extern void sensors_init(void);

extern int bme280_init(struct sensor_data *sensor);
extern int bme280_poll(struct sensor_data *sensor, struct sensor_measurement *out);
extern int dht_init(struct sensor_data *sensor);
extern int dht_poll(struct sensor_data *sensor, struct sensor_measurement *out);
extern int ds18b20_init(struct sensor_data *sensor);
extern int ds18b20_poll(struct sensor_data *sensor, struct sensor_measurement *out);

#endif
