#include "mgos.h"
#include "mgos_mqtt.h"
#include "sensors.h"

static struct sensor_data *sensors = NULL;
static bool time_is_set = false;

static struct sensor_data *get_sensor(int idx)
{
    static struct sensor_data **prev;
    int i = 0;

    prev = &sensors;
    while (*prev != NULL && i < idx) {
        prev = &(*prev)->next;
        i++;
    }
    if (*prev == NULL) {
        struct sensor_data *data;

        *prev = malloc(sizeof(struct sensor_data));
        data = *prev;
        memset(data, 0, sizeof(struct sensor_data));
        data->pin = -1;
        data->power_gpio = -1;
    }
    return *prev;
}

static void sensor_config_cb(void *arg, const char *name_in, size_t name_len,
                             const char *path, const struct json_token *token)
{
    struct sensor_data *sensor;
    int idx;
    char name[64], value[100];

    if (sscanf(path, "[%d]", &idx) != 1)
        return;

    sensor = get_sensor(idx);

    if (name_in == NULL)
        return;

    if (name_len > sizeof(name))
        name_len = sizeof(name);
    strncpy(name, name_in, name_len);
    name[name_len] = '\0';

    value[0] = '\0';
    if (token) {
        if (token->ptr) {
            int len = token->len;
            if (len > (int) sizeof(value))
                len = sizeof(value);
            strncpy(value, token->ptr, len);
            value[len] = '\0';
        }
    }

    if (strcmp(name, "type") == 0) {
        if (token->type == JSON_TYPE_STRING)
            strncpy(sensor->type, value, sizeof(sensor->type));
    } else if (strcmp(name, "poll_delay") == 0) {
        if (token->type == JSON_TYPE_NUMBER)
            sscanf(value, "%u", &sensor->poll_delay);
    } else if (strcmp(name, "pin") == 0) {
        if (token->type == JSON_TYPE_NUMBER)
            sscanf(value, "%u", &sensor->pin);
    } else if (strcmp(name, "power_gpio") == 0) {
        if (token->type == JSON_TYPE_NUMBER)
            sscanf(value, "%u", &sensor->power_gpio);
    } else if (strcmp(name, "mqtt_topic") == 0) {
        if (token->type == JSON_TYPE_STRING)
            sensor->mqtt_topic = strdup(value);
    } else if (strcmp(name, "rh_sensor_id") == 0) {
        if (token->type == JSON_TYPE_NUMBER)
            sscanf(value, "%hi", &sensor->rh_sensor_id);
    }

    (void) arg;
}

static void report_mqtt(struct sensor_data *sensor,
                        struct sensor_measurement *val, int n_val,
                        double measurement_time)
{

}

static void report_rh(struct sensor_data *sensor,
                      struct sensor_measurement *val, int n_val,
                      double measurement_time)
{

}

static void report_measurements(struct sensor_data *sensor,
                                struct sensor_measurement *val, int n_val,
                                double measurement_time)
{
    while (n_val--) {
        char message[100], topic[100];
        struct json_out out = JSON_OUT_BUF(message, sizeof(message));

        sprintf(topic, "%s/%s", sensor->mqtt_topic, val->property_name);
        if (measurement_time)
            json_printf(&out, "{value: %.2f, time: %lf}", val->float_val, measurement_time);
        else
            json_printf(&out, "{value: %.2f}", val->float_val);

        bool res = mgos_mqtt_pub(topic, message, strlen(message), 0, false);
        LOG(LL_INFO, ("%s: %s (%sreported)", topic, message, res ? "" : "not "));

        val++;
    }
}

static void poll_sensor(void *arg)
{
    struct sensor_data *sensor = (struct sensor_data *) arg;
    struct sensor_measurement values[10], *val;
    double measurement_time;

    int n_values;

    LOG(LL_INFO, ("callback for %s", sensor->type));
    if (strcmp(sensor->type, "bme280") == 0) {
        n_values = bme280_poll(sensor, values);
    } else if (strcmp(sensor->type, "dht") == 0) {
        n_values = dht_poll(sensor, values);
    } else if (strcmp(sensor->type, "mh-z19") == 0) {
        n_values = mh_z19_poll(sensor, values);
    } else if (strcmp(sensor->type, "ds18b20") == 0) {
        n_values = ds18b20_poll(sensor, values);
    } else
        n_values = 0;

    if (time_is_set && n_values)
        measurement_time = cs_time();
    else
        measurement_time = 0;

    for (val = values; val - values < n_values; val++) {
        LOG(LL_INFO, ("%s: %.2f %s", val->property_name, val->float_val, val->unit));
    }

    if (n_values)
        report_measurements(sensor, values, n_values, measurement_time);
}

static void init_sensor(struct sensor_data *sensor)
{
    char logbuf[120], *p = logbuf;

    if (!sensor->poll_delay)
        sensor->poll_delay = 60000;
    p += sprintf(p, "Sensor %s\n", sensor->type);
    if (sensor->mqtt_topic)
        p += sprintf(p, "\tMQTT topic: %s\n", sensor->mqtt_topic);
    if (sensor->poll_delay)
        p += sprintf(p, "\tPoll delay: %u ms\n", sensor->poll_delay);
    if (sensor->pin)
        p += sprintf(p, "\tPin: %d\n", sensor->pin);
    if (sensor->power_gpio)
        p += sprintf(p, "\tPower GPIO: %d\n", sensor->power_gpio);

    LOG(LL_INFO, ("%s", logbuf));

    if (strcmp(sensor->type, "bme280") == 0) {
        if (bme280_init(sensor) < 0)
            return;
    } else if (strcmp(sensor->type, "dht") == 0) {
        if (dht_init(sensor) < 0)
            return;
    } else if (strcmp(sensor->type, "mh-z19") == 0) {
        if (mh_z19_init(sensor) < 0)
            return;
    } else if (strcmp(sensor->type, "ds18b20") == 0) {
        if (ds18b20_init(sensor) < 0)
            return;
    } else {
        LOG(LL_ERROR, ("Invalid sensor type (%s)", sensor->type));
        return;
    }
    sensor->enabled = 1;
    sensor->timer_id = mgos_set_timer(sensor->poll_delay, MGOS_TIMER_REPEAT, poll_sensor, sensor);
}

static void time_change_cb(int ev, void *evd, void *arg)
{
    time_is_set = true;
    (void) ev;
    (void) evd;
    (void) arg;
}

void sensors_init(void)
{
    size_t size;
    char *buf;
    int ret;

    LOG(LL_INFO, ("Sensors initializing"));

    //rfreport_test();

    buf = cs_read_file("sensors.json", &size);
    if (buf == NULL) {
        LOG(LL_ERROR, ("No sensor configuration file found"));
        return;
    }
    printf("%s", buf);
    ret = json_walk(buf, size, sensor_config_cb, NULL);
    free(buf);
    if (ret <= 0) {
        LOG(LL_ERROR, ("Sensor config parsing failed"));
        return;
    }

    for (struct sensor_data *sensor = sensors; sensor != NULL; sensor = sensor->next)
        init_sensor(sensor);

    mgos_event_add_handler(MGOS_EVENT_TIME_CHANGED, time_change_cb, NULL);
}

void sensors_handle_rf_report(const struct rf_sensor_report *report)
{
    struct sensor_data *sensor;
    struct sensor_measurement vals[5];
    double measurement_time;
    int i, c;

    for (sensor = sensors; sensor != NULL; sensor = sensor->next) {
        if (sensor->rh_sensor_id == report->sensor_id)
            break;
    }
    if (sensor != NULL) {
        LOG(LL_WARN, ("RH sensor id 0x%04x not registered", report->sensor_id));
        return;
    }
    c = report->n_observations;
    if (c > sizeof(vals) / sizeof(vals[0])) {
        LOG(LL_ERROR, ("Report from sensor 0x%04x has too many observations (%d)",
                       report->sensor_id, c));
        c = sizeof(vals) / sizeof(vals[0]);
    }
    for (i = 0; i < c; i++) {
        const struct rf_sensor_observation *obs = report->observations + i;
        const char *property_name = NULL;
        const char *unit = NULL;

        switch (obs->phenomenon) {
        case RF_PHENOMENON_TEMPERATURE:
            property_name = "temperature";
            unit = "C";
            break;
        case RF_PHENOMENON_HUMIDITY:
            property_name = "humidity";
            unit = "%";
            break;
        default:
            LOG(LL_ERROR, ("Unable to handle phenomenon %d from sensor 0x%04x",
                           obs->phenomenon, report->sensor_id));
            break;
        }
        if (property_name == NULL)
            continue;
        vals[i].property_name = property_name;
        vals[i].unit = unit;
        vals[i].type = SENSOR_FLOAT;
        vals[i].float_val = obs->value.float_val;
    }

    if (time_is_set && c)
        measurement_time = cs_time();
    else
        measurement_time = 0;

    report_measurements(sensor, vals, c, measurement_time);    
}


void sensors_shutdown(void)
{
    struct sensor_data *sensor;

    for (sensor = sensors; sensor != NULL; sensor = sensor->next) {
        if (sensor->enabled)
            mgos_clear_timer(sensor->timer_id);
    }
}