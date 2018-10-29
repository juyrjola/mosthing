#include <mgos.h>
#include "actuators.h"

struct selection {
    char *name;
    int gpio;
    struct selection *next;
};


struct gpio_select_driver_state {
    struct selection *selections;
};

int gpio_select_set(struct actuator *act, const char *value)
{
    struct gpio_select_driver_state *state = (struct gpio_select_driver_state *) act->driver_data;
    struct selection *new_sel = NULL, *sel;

    LOG(LL_INFO, ("setting %s", value));
    if (strcmp(value, "off") != 0) {
        for (sel = state->selections; sel != NULL; sel = sel->next) {
            if (strcmp(sel->name, value) == 0) {
                new_sel = sel;
                break;
            }
        }
        if (new_sel == NULL) {
            LOG(LL_ERROR, ("Invalid state: %s", value));
            return -1;
        }
    }

    for (sel = state->selections; sel != NULL; sel = sel->next)
        if (sel != new_sel)
            actuators_set_gpio(act, sel->gpio, 0);

    if (new_sel != NULL)
        actuators_set_gpio(act, new_sel->gpio, 1);

    return 0;
}

int gpio_select_init(struct actuator *act)
{
    struct gpio_select_driver_state *state;
    struct selection **prev;
    struct json_token *config;
    int ret, i;

    state = malloc(sizeof(*state));
    memset(state, 0, sizeof(*state));

    config = &act->config;
    prev = &state->selections;
    for (i = 0; ; i++) {
        struct json_token token;
        struct selection *sel;

        ret = json_scanf_array_elem(config->ptr, config->len, ".states", i, &token);
        if (ret < 0)
            break;

        sel = malloc(sizeof(*sel));
        memset(sel, 0, sizeof(*sel));

        ret = json_scanf(token.ptr, token.len, "{gpio: %d, name: %Q}", &sel->gpio, &sel->name);
        if (ret < 0) {
            LOG(LL_ERROR, ("Parsing of gpio_select config failed"));
            goto fail;
        }
        *prev = sel;
        prev = &sel->next;
    }

    if (state->selections == NULL) {
        LOG(LL_ERROR, ("gpio_select requires at least one entry in \"states\""));
        goto fail;
    }

    for (struct selection *sel = state->selections; sel != NULL; sel = sel->next)
        LOG(LL_INFO, ("\tGPIO %2d: %s", sel->gpio, sel->name));

    act->driver_data = state;
    gpio_select_set(act, "off");

    return 0;
fail:
    free(state);
    return -1;
}

struct actuator_driver gpio_select_driver = {
    .name = "gpio_select",
    .type = ACTUATOR_SELECT,
    .init = gpio_select_init,
    .set = gpio_select_set,
};
