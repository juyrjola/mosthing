#include <stdlib.h>
#include "mgos.h"
#include "sensors.h"

#define MEASUREMENT_TOLERANCE  0.05

struct mh_z19_state {
    bool all_good;
    uint8_t gpio;
    uint16_t co2_level;
    double rise_time, fall_time, sample_time;
    uint16_t co2_history[5];
    int history_idx;
};

static void report_co2_level(struct mh_z19_state *state, uint16_t ppm,
                             double now)
{
    uint32_t sum = 0;
    int n, i, c;
    bool good_measurement = true;

    n = sizeof(state->co2_history) / sizeof(state->co2_history[0]);
    if (ppm < 200 || ppm > 2000) {
        state->all_good = false;
        return;
    }
    for (i = state->history_idx, c = 0; c < n; i = (i + 1) % n, c++) {
        uint16_t hist_ppm = state->co2_history[i];

        if (ppm < hist_ppm * (1-MEASUREMENT_TOLERANCE) ||
            ppm > hist_ppm * (1+MEASUREMENT_TOLERANCE)) {
            good_measurement = false;
            break;
        }
        sum += hist_ppm;
    }
    state->co2_history[state->history_idx] = ppm;
    state->history_idx = (state->history_idx + 1) % n;
    if (!good_measurement) {
        state->all_good = false;
        return;
    }

    sum += ppm;
    state->co2_level = sum / (n + 1);
    state->all_good = true;
    state->sample_time = now;
    return;
}

static void mh_z19_int_handler(int pin, void *arg)
{
    struct mh_z19_state *state = (struct mh_z19_state *) arg;
    int is_high = mgos_gpio_read(state->gpio);
    double now;
    unsigned int diff_rise, diff_fall;

    now = mgos_uptime();
    if (is_high) {
        unsigned int high_time, low_time, cycle_time, ppm;

        diff_rise = (unsigned int) ((now - state->rise_time) * 1000000);
        diff_fall = (unsigned int) ((now - state->fall_time) * 1000000);
        high_time = diff_rise - diff_fall;
        low_time = diff_fall;
        cycle_time = diff_rise / 1000;

        // Make sure cycle time is 5% within 1004ms
        if (cycle_time >= 1004*.95 && cycle_time <= 1004*1.05) {
            ppm = (2000 * (high_time - 2000)) / (high_time + low_time - 4000);
            report_co2_level(state, ppm, now);
        } else
            state->all_good = false;

        state->rise_time = now;

    } else
        state->fall_time = now;

    (void) pin;
}

int mh_z19_poll(struct sensor *sensor, struct sensor_measurement *out)
{
    struct mh_z19_state *state = (struct mh_z19_state *) sensor->driver_data;
    double now;

    if (!state->all_good)
        return 0;

    now = mgos_uptime();
    if ((now - state->sample_time) > 2000)
        return 0;

    out->property_name = "co2_level";
    out->unit = "ppm";
    out->type = SENSOR_FLOAT;
    out->float_val = state->co2_level;
    return 1;
}


int mh_z19_init(struct sensor *sensor)
{
    struct mh_z19_state *state;
    int pin = sensor->gpio;

    LOG(LL_INFO, ("Initializing MH-Z19 sensor (PWM mode on pin %d)", pin));
    if (pin < 0) {
        LOG(LL_ERROR, ("MH-Z19 pin not configured"));
        return -1;
    }

    state = malloc(sizeof(*state));
    memset(state, 0, sizeof(*state));
    state->gpio = pin;
    sensor->driver_data = (void *) state;

    mgos_gpio_set_mode(pin, MGOS_GPIO_MODE_INPUT);
    mgos_gpio_set_int_handler_isr(pin, MGOS_GPIO_INT_EDGE_ANY, mh_z19_int_handler, state);
    mgos_gpio_enable_int(pin);
    LOG(LL_INFO, ("MH-Z19 initialized"));

    return 0;
}
