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
        *prev = malloc(sizeof(struct sensor_data));
        memset(*prev, 0, sizeof(struct sensor_data));
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
    } else if (strcmp(name, "mqtt_topic") == 0) {
        if (token->type == JSON_TYPE_STRING)
            sensor->mqtt_topic = strdup(value);
    }

    (void) arg;
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

        LOG(LL_INFO, ("%s: %s", topic, message));
        bool res = mgos_mqtt_pub(topic, message, strlen(message), 0, false);
        (void) res;

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

    LOG(LL_INFO, ("%s", logbuf));

    if (strcmp(sensor->type, "bme280") == 0) {
        if (bme280_init(sensor) < 0)
            return;
    } else if (strcmp(sensor->type, "dht") == 0) {
        if (dht_init(sensor) < 0)
            return;
    } else {
        LOG(LL_ERROR, ("Invalid sensor type (%s)", sensor->type));
        return;
    }
    sensor->enabled = 1;

    mgos_set_timer(sensor->poll_delay, MGOS_TIMER_REPEAT, poll_sensor, sensor);
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
    mgos_msleep(1000);
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
