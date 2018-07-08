#include "mgos.h"
#include "mgos_mqtt.h"
#include "actuators.h"

#define MAX_MAX_TIME (24 * 60 * 60 * 1000)

static struct actuator *actuators = NULL;

static struct actuator *get_actuator(int idx)
{
    static struct actuator **prev;
    int i = 0;

    prev = &actuators;
    while (*prev != NULL && i < idx) {
        prev = &(*prev)->next;
        i++;
    }
    if (*prev == NULL) {
        struct actuator *data;

        *prev = malloc(sizeof(struct actuator));
        data = *prev;
        memset(data, 0, sizeof(struct actuator));
        data->gpio = -1;
    }
    return *prev;
}

static const char *type_to_str(enum actuator_type type)
{
    switch (type) {
    case ACTUATOR_RELAY:
        return "relay";
    default:
        return "unknown";
    }
}

static void actuator_config_cb(void *arg, const char *name_in, size_t name_len,
                               const char *path, const struct json_token *token)
{
    struct actuator *actuator;
    int idx;
    char name[64], value[100];

    if (sscanf(path, "[%d]", &idx) != 1)
        return;

    actuator = get_actuator(idx);

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

    if (strcmp(name, "driver") == 0) {
        if (token->type == JSON_TYPE_STRING)
            strncpy(actuator->driver, value, sizeof(actuator->driver));
    } else if (strcmp(name, "gpio") == 0) {
        if (token->type == JSON_TYPE_NUMBER)
            sscanf(value, "%u", &actuator->gpio);
    } else if (strcmp(name, "max_time") == 0) {
        if (token->type == JSON_TYPE_NUMBER) {
            sscanf(value, "%u", &actuator->max_time);
            if (actuator->max_time > MAX_MAX_TIME) {
                actuator->max_time = MAX_MAX_TIME;
                LOG(LL_ERROR, ("Max. time too long, setting to %u s", actuator->max_time / 1000));
            }
        }
    } else if (strcmp(name, "active_low") == 0) {
        if (token->type == JSON_TYPE_TRUE)
            actuator->active_low = true;
        else
            actuator->active_low = false;
    } else if (strcmp(name, "mqtt_topic") == 0) {
        if (token->type == JSON_TYPE_STRING)
            actuator->mqtt_topic = strdup(value);
    }

    (void) arg;
}

static void init_actuator(struct actuator *actuator)
{
    char logbuf[120], *p = logbuf;

    if (strcmp(actuator->driver, "gpio_relay") == 0) {
        actuator->type = ACTUATOR_RELAY;
        if (gpio_relay_init(actuator) < 0)
            return;
        actuator->driver_set = gpio_relay_set;
    } else {
        LOG(LL_ERROR, ("Invalid actuator driver (%s)", actuator->driver));
        return;
    }

    p += sprintf(p, "actuator %s: %s\n", type_to_str(actuator->type), actuator->driver);
    if (actuator->mqtt_topic)
        p += sprintf(p, "\tMQTT topic: %s\n", actuator->mqtt_topic);
    if (actuator->gpio)
        p += sprintf(p, "\tGPIO: %d\n", actuator->gpio);
    p += sprintf(p, "\tActive low: %s\n", actuator->active_low ? "yes": "no");

    LOG(LL_INFO, ("%s", logbuf));

    actuator->enabled = 1;
}

static void subscribe_actuator(struct actuator *act, struct mg_connection *c)
{
    char buf[100];
    struct mg_mqtt_topic_expression te = {.topic = buf, .qos = 1};
    uint16_t sub_id = mgos_mqtt_get_packet_id();

    sprintf(buf, "%s", act->mqtt_topic);
    mg_mqtt_subscribe(c, &te, 1, sub_id);
    LOG(LL_INFO, ("Subscribing to %s (id %u)", buf, sub_id));
}

static void actuator_timer_cb(void *arg)
{
    struct actuator *actuator = (struct actuator *) arg;
    int new_state = 0;
    char buf[100];
    struct json_out jmo = JSON_OUT_BUF(buf, sizeof(buf));

    actuator->timer_id = (mgos_timer_id) 0;
    actuator->driver_set(actuator, new_state);
    LOG(LL_INFO, ("Setting actuator %s to %d (timer)", actuator->mqtt_topic, new_state));
    json_printf(&jmo, "{state: %d}", new_state);
    mgos_mqtt_pub(actuator->mqtt_topic, buf, strlen(buf), 1, false);
}

static void mqtt_ev_handler(struct mg_connection *c, int ev, void *p,
                            void *user_data)
{
    struct mg_mqtt_message *msg = (struct mg_mqtt_message *) p;
    struct actuator *actuator;

    if (ev == MG_EV_MQTT_CONNACK) {
        LOG(LL_INFO, ("CONNACK: %d", msg->connack_ret_code));
        for (actuator = actuators; actuator != NULL; actuator = actuator->next) {
            if (!actuator->mqtt_topic)
                continue;
            subscribe_actuator(actuator, c);            
        }
    } else if (ev == MG_EV_MQTT_SUBACK) {
        LOG(LL_INFO, ("Subscription %u acknowledged", msg->message_id));
    } else if (ev == MG_EV_MQTT_PUBLISH) {
        struct mg_str *s = &msg->payload;
        struct mg_str *topic = &msg->topic;
        char buf[100];
        struct json_out jmo = JSON_OUT_BUF(buf, sizeof(buf));
        int new_state = -1, time = -1;

        LOG(LL_INFO, ("%.*s: got command: [%.*s]", (int) topic->len, topic->p, (int) s->len, s->p));
        mg_mqtt_puback(c, msg->message_id);
        if (json_scanf(s->p, s->len, "{state: %d}", &new_state) == 1) {
            // It is our message echoed back to us
            return; 
        }

        if (json_scanf(s->p, s->len, "{new_state: %d, time: %d}", &new_state, &time) <= 0 ||
            (new_state != 0 && new_state != 1) ||
            (time == 0)) {
            LOG(LL_ERROR, ("JSON parameters new_state or time invalid"));
            return;
        }
        for (actuator = actuators; actuator != NULL; actuator = actuator->next) {
            if (!actuator->mqtt_topic)
                continue;
            if (topic->len != strlen(actuator->mqtt_topic))
                continue;
            if (strncmp(topic->p, actuator->mqtt_topic, topic->len) != 0)
                continue;
            break;
        }
        if (actuator == NULL) {
            LOG(LL_ERROR, ("Actuator not found"));
            return;
        }
        if (actuator->max_time) {
            if (time < 0 || time > actuator->max_time)
                time = actuator->max_time;
        }
        LOG(LL_INFO, ("Setting actuator %s to %d (time %d)", actuator->mqtt_topic, new_state, time));
        if (actuator->driver_set(actuator, new_state) < 0) {
            LOG(LL_INFO, ("Error setting actuator value"));
            return;
        } else {
            json_printf(&jmo, "{state: %d}", new_state);
        }
        // Publish the new state (or error)
        mg_mqtt_publish(c, actuator->mqtt_topic, mgos_mqtt_get_packet_id(),
                        MG_MQTT_QOS(1), buf, strlen(buf));

        if (time > 0) {
            if (actuator->timer_id)
                mgos_clear_timer(actuator->timer_id);
            actuator->timer_id = mgos_set_timer(time, 0, actuator_timer_cb, actuator);
        }
    }
}

void actuators_init(void)
{
    size_t size;
    char *buf;
    int ret;

    LOG(LL_INFO, ("actuators initializing"));

    buf = cs_read_file("actuators.json", &size);
    if (buf == NULL) {
        LOG(LL_ERROR, ("No actuator configuration file found"));
        return;
    }
    printf("%s", buf);
    ret = json_walk(buf, size, actuator_config_cb, NULL);
    free(buf);
    if (ret <= 0) {
        LOG(LL_ERROR, ("actuator config parsing failed"));
        return;
    }

    for (struct actuator *actuator = actuators; actuator != NULL; actuator = actuator->next)
        init_actuator(actuator);

    mgos_mqtt_add_global_handler(mqtt_ev_handler, NULL);
}

void actuators_shutdown(void)
{
    struct actuator *actuator;

    for (actuator = actuators; actuator != NULL; actuator = actuator->next) {
        if (actuator->enabled && actuator->timer_id)
            mgos_clear_timer(actuator->timer_id);
    }
}

int gpio_relay_init(struct actuator *act)
{
    int val = 0;

    if (act->active_low)
        val = 1;
    mgos_gpio_write(act->gpio, val);
    mgos_gpio_set_mode(act->gpio, MGOS_GPIO_MODE_OUTPUT);
    return 0;
}

int gpio_relay_set(struct actuator *act, int value)
{
    if (act->active_low)
        value = !value;
    mgos_gpio_write(act->gpio, value);
    return 0;
}
