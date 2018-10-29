#include "mgos.h"
#include "mgos_mqtt.h"
#include "actuators.h"

#define MAX_MAX_TIME (24 * 60 * 60 * 1000)

static struct actuator *actuators = NULL;

static const struct actuator_driver *actuator_drivers[] = {
    &gpio_relay_driver, &gpio_select_driver
};

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
        data->gpio_inactive_state = ACTUATOR_GPIO_LOW;
        data->gpio_active_state = ACTUATOR_GPIO_HIGH;
    }
    return *prev;
}

static const char *type_to_str(enum actuator_type type)
{
    switch (type) {
    case ACTUATOR_RELAY:
        return "relay";
    case ACTUATOR_SELECT:
        return "select";
    default:
        return "unknown";
    }
}

static const char *gpio_state_to_str(enum actuator_gpio_state state)
{
    switch (state) {
    case ACTUATOR_GPIO_HIGH:
        return "high";
    case ACTUATOR_GPIO_LOW:
        return "low";
    case ACTUATOR_GPIO_FLOAT:
        return "float";
    default:
        return "unknown";
    }
}

struct config_parser_state {
    int depth;
};

static void actuator_config_cb(void *arg, const char *name_in, size_t name_len,
                               const char *path, const struct json_token *token)
{
    struct config_parser_state *state = (struct config_parser_state *) arg;
    struct actuator *actuator;
    int idx, skip;
    char name[64], value[100];

#if 0
    if (path != NULL)
        printf("%d type %d ptr %p len %3d path %s\n", state->depth, token->type, token->ptr, token->len, path);
#endif
    if (sscanf(path, "[%d]", &idx) != 1)
        return;

    actuator = get_actuator(idx);
    if (state->depth == 1 && token->type == JSON_TYPE_OBJECT_END) {
        actuator->config = *token;
    }

    // Only top-level keys are of interest to the general parser.
    if (state->depth != 1)
        skip = 1;
    else
        skip = 0;

    if (token->type == JSON_TYPE_OBJECT_START || token->type == JSON_TYPE_ARRAY_START)
        state->depth++;
    else if (token->type == JSON_TYPE_OBJECT_END || token->type == JSON_TYPE_ARRAY_END)
        state->depth--;

    if (skip || name_in == NULL)
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
        if (token->type != JSON_TYPE_STRING) {
            return;
        }
        for (unsigned int i = 0; i < sizeof(actuator_drivers) / sizeof(actuator_drivers[0]); i++) {
            const struct actuator_driver *driver = actuator_drivers[i];
            if (strcmp(driver->name, value) == 0) {
                actuator->driver = driver;
                break;
            }
        }
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
    } else if (strcmp(name, "gpio_active_state") == 0 || strcmp(name, "gpio_inactive_state") == 0) {
        enum actuator_gpio_state state;

        if (strcasecmp(value, "high") == 0)
            state = ACTUATOR_GPIO_HIGH;
        else if (strcasecmp(value, "low") == 0)
            state = ACTUATOR_GPIO_LOW;
        else if (strcasecmp(value, "float") == 0)
            state = ACTUATOR_GPIO_FLOAT;
        else {
            LOG(LL_ERROR, ("Invalid GPIO state: %s", value));
            return;
        }
        if (strcmp(name, "gpio_active_state") == 0)
            actuator->gpio_active_state = state;
        else
            actuator->gpio_inactive_state = state;
    } else if (strcmp(name, "mqtt_topic") == 0) {
        if (token->type == JSON_TYPE_STRING) {
            actuator->mqtt_control_topic = strdup(value);
            asprintf(&actuator->mqtt_state_topic, "%s/state", value);
        }
    }

    (void) arg;
}

extern int gpio_select_init(struct actuator *);

static void init_actuator(struct actuator *actuator)
{
    char *logbuf, *p;

    if (actuator->driver == NULL) {
        LOG(LL_ERROR, ("No driver for actuator"));
        return;
    }
    actuator->type = actuator->driver->type;

    if (actuator->driver->init(actuator) < 0)
        return;

    logbuf = malloc(1000);
    p = logbuf;
    p += sprintf(p, "actuator %s: %s\n", type_to_str(actuator->type), actuator->driver->name);
    if (actuator->mqtt_control_topic)
        p += sprintf(p, "\tMQTT control topic: %s\n", actuator->mqtt_control_topic);
    if (actuator->mqtt_state_topic)
        p += sprintf(p, "\tMQTT state topic: %s\n", actuator->mqtt_state_topic);
    if (actuator->gpio >= 0)
        p += sprintf(p, "\tGPIO: %d\n", actuator->gpio);
    p += sprintf(p, "\tActive: %s\n", gpio_state_to_str(actuator->gpio_active_state));
    p += sprintf(p, "\tInactive: %s\n", gpio_state_to_str(actuator->gpio_inactive_state));

    LOG(LL_INFO, ("%s", logbuf));
    free(logbuf);

    actuator->enabled = 1;
}

static void subscribe_actuator(struct actuator *act, struct mg_connection *c)
{
    char buf[100];
    struct mg_mqtt_topic_expression te = {.topic = buf, .qos = 1};
    uint16_t sub_id = mgos_mqtt_get_packet_id();

    sprintf(buf, "%s", act->mqtt_control_topic);
    LOG(LL_INFO, ("Subscribing to %s (id %u)", buf, sub_id));
    mg_mqtt_subscribe(c, &te, 1, sub_id);
}

static void actuator_timer_cb(void *arg)
{
    struct actuator *actuator = (struct actuator *) arg;
    const char *new_state;
    char buf[100];
    struct json_out jmo = JSON_OUT_BUF(buf, sizeof(buf));

    if (actuator->type == ACTUATOR_RELAY || actuator->type == ACTUATOR_SELECT)
        new_state = "off";
    else {
        LOG(LL_ERROR, ("Unknown actuator type for timer callback"));
        return;
    }

    actuator->timer_id = (mgos_timer_id) 0;
    actuator->driver->set(actuator, new_state);
    LOG(LL_INFO, ("Setting actuator %s to %s (timer)", actuator->mqtt_control_topic, new_state));
    json_printf(&jmo, "{state: %Q}", new_state);
    mgos_mqtt_pub(actuator->mqtt_state_topic, buf, strlen(buf), 1, false);
}

static void handle_mqtt_publish(struct mg_connection *conn, struct mg_mqtt_message *msg)
{
    struct mg_str *s = &msg->payload;
    struct mg_str *topic = &msg->topic;
    struct actuator *actuator;
    char buf[100];
    struct json_out jmo = JSON_OUT_BUF(buf, sizeof(buf));
    char *new_state = NULL;
    int time = -1;

    LOG(LL_INFO, ("%.*s: got command: [%.*s]", (int) topic->len, topic->p, (int) s->len, s->p));
    mg_mqtt_puback(conn, msg->message_id);

    if (json_scanf(s->p, s->len, "{state: %Q, time: %d}", &new_state, &time) <= 0 ||
        new_state == NULL || time == 0) {
        LOG(LL_ERROR, ("JSON parameters state or time invalid"));
        if (new_state != NULL)
            free(new_state);
        return;
    }
    for (actuator = actuators; actuator != NULL; actuator = actuator->next) {
        if (actuator->mqtt_control_topic == NULL)
            continue;
        if (topic->len != strlen(actuator->mqtt_control_topic))
            continue;
        if (strncmp(topic->p, actuator->mqtt_control_topic, topic->len) != 0)
            continue;
        break;
    }
    if (actuator == NULL) {
        LOG(LL_ERROR, ("Actuator not found"));
        free(new_state);
        return;
    }
    if (actuator->max_time) {
        if (time < 0 || time > (int) actuator->max_time)
            time = actuator->max_time;
    }
    if (time > 0)
        sprintf(buf, " (time %d ms)", time);
    else
        buf[0] = '\0';
    LOG(LL_INFO, ("Setting actuator %s to %s%s", actuator->mqtt_control_topic, new_state, buf));
    if (actuator->driver->set(actuator, new_state) < 0) {
        LOG(LL_INFO, ("Error setting actuator value"));
        return;
    } else {
        json_printf(&jmo, "{state: %Q}", new_state);
    }
    if (actuator->mqtt_state_topic != NULL) {
        // Publish the new state (or error)
        mg_mqtt_publish(conn, actuator->mqtt_state_topic, mgos_mqtt_get_packet_id(),
                        MG_MQTT_QOS(1), buf, strlen(buf));
    }
    if (time > 0) {
        if (actuator->timer_id)
            mgos_clear_timer(actuator->timer_id);
        actuator->timer_id = mgos_set_timer(time, 0, actuator_timer_cb, actuator);
    }
}

static void mqtt_ev_handler(struct mg_connection *c, int ev, void *p,
                            void *user_data)
{
    struct mg_mqtt_message *msg = (struct mg_mqtt_message *) p;
    struct actuator *actuator;

    if (ev == MG_EV_MQTT_CONNACK) {
        LOG(LL_INFO, ("CONNACK: %d", msg->connack_ret_code));
        for (actuator = actuators; actuator != NULL; actuator = actuator->next) {
            if (!actuator->mqtt_control_topic)
                continue;
            subscribe_actuator(actuator, c);
        }
    } else if (ev == MG_EV_MQTT_SUBACK) {
        LOG(LL_INFO, ("Subscription %u acknowledged", msg->message_id));
    } else if (ev == MG_EV_MQTT_PUBLISH) {
        handle_mqtt_publish(c, msg);
    }

    (void) user_data;
}

static char *config_buffer;

void actuators_init(void)
{
    struct config_parser_state parser_state;
    size_t size;
    int ret;

    LOG(LL_INFO, ("actuators initializing"));

    config_buffer = cs_read_file("actuators.json", &size);
    if (config_buffer == NULL) {
        LOG(LL_ERROR, ("No actuator configuration file found"));
        return;
    }
    printf("%s", config_buffer);
    memset(&parser_state, 0, sizeof(parser_state));
    ret = json_walk(config_buffer, size, actuator_config_cb, &parser_state);
    if (ret <= 0) {
        LOG(LL_ERROR, ("actuator config parsing failed"));
        free(config_buffer);
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
    free(config_buffer);
    config_buffer = NULL;
}

int actuators_parse_onoff(const char *value)
{
    if (strcasecmp(value, "on") == 0)
        return 1;
    if (strcasecmp(value, "off") == 0)
        return 0;
    return -1;
}

void actuators_set_gpio(struct actuator *act, int gpio, int new_state)
{
    enum actuator_gpio_state state;

    if (new_state)
        state = act->gpio_active_state;
    else
        state = act->gpio_inactive_state;

    LOG(LL_INFO, ("Setting GPIO %d to %s", gpio, gpio_state_to_str(state)));

    switch (state) {
    case ACTUATOR_GPIO_HIGH:
        mgos_gpio_write(gpio, 1);
        mgos_gpio_set_mode(gpio, MGOS_GPIO_MODE_OUTPUT);
        break;
    case ACTUATOR_GPIO_LOW:
        mgos_gpio_write(gpio, 0);
        mgos_gpio_set_mode(gpio, MGOS_GPIO_MODE_OUTPUT);
        break;
    case ACTUATOR_GPIO_FLOAT:
        mgos_gpio_set_mode(gpio, MGOS_GPIO_MODE_INPUT);
        break;
    }
}

int gpio_relay_init(struct actuator *act)
{
    actuators_set_gpio(act, act->gpio, 0);
    return 0;
}

int gpio_relay_set(struct actuator *act, const char *str_value)
{
    int value = actuators_parse_onoff(str_value);

    if (value < 0)
        return -1;

    actuators_set_gpio(act, act->gpio, value);
    return 0;
}

struct actuator_driver gpio_relay_driver = {
    .name = "gpio_relay",
    .type = ACTUATOR_RELAY,
    .init = gpio_relay_init,
    .set = gpio_relay_set,
};
