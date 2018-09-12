#include "sensors.h"
#include <math.h>
#include <mgos.h>

#define N_MEASUREMENTS 10
#define MEASUREMENT_TOLERANCE 5  /* in percents */

struct gpio_ultrasound_state {
    int echo_gpio;
    int trig_gpio;

    double m_per_us;
    double trig_time;

    float measurements[N_MEASUREMENTS];
    int n_measurements, n_timeouts;
    mgos_timer_id timeout_timer;
};

static double sound_m_per_us(float temp)
{
    double speed;

    speed = 20.05 * (sqrt(temp + 273.15));
    return speed / 1000000;
}

static int start_measurement(struct sensor *sensor);

static void timeout_timer_cb(void *arg)
{
    struct sensor *sensor = (struct sensor *) arg;
    struct gpio_ultrasound_state *state = (struct gpio_ultrasound_state *) sensor->driver_data;

    mgos_gpio_disable_int(state->echo_gpio);
    state->timeout_timer = 0;
    LOG(LL_ERROR, ("GPIO ultrasound timeout"));
    state->n_timeouts++;
    if (state->n_timeouts == 3) {
        LOG(LL_ERROR, ("GPIO ultrasound measurement failed, too many timeouts"));
        return;
    }

    // Try again
    start_measurement(sensor);
}

static int start_measurement(struct sensor *sensor)
{
    struct gpio_ultrasound_state *state = (struct gpio_ultrasound_state *) sensor->driver_data;
    int j;

    state->timeout_timer = mgos_set_timer(500, 0, timeout_timer_cb, sensor);
    mgos_gpio_enable_int(state->echo_gpio);
    mgos_gpio_write(state->trig_gpio, 1);
    mgos_usleep(10);
    state->trig_time = mgos_uptime();
    mgos_gpio_write(state->trig_gpio, 0);

    for (j = 0; j < 10000; j++) {
        if (mgos_gpio_read(state->echo_gpio))
            break;
        mgos_usleep(1);
    }
    if (j == 10000) {
        LOG(LL_ERROR, ("Ultrasound echo GPIO did not go high"));
        mgos_gpio_disable_int(state->echo_gpio);
        mgos_clear_timer(state->timeout_timer);
        state->timeout_timer = 0;
        return -1;
    }

    return 0;
}

static float calc_median(int n, float *x)
{
    float temp;
    int i, j;

    // the following two loops sort the array x in ascending order
    for (i = 0; i < n - 1; i++) {
        for (j = i + 1; j < n; j++) {
            if (x[j] < x[i]) {
                // swap elements
                temp = x[i];
                x[i] = x[j];
                x[j] = temp;
            }
        }
    }

    if (n % 2 == 0) {
        // if there is an even number of elements, return mean of the two elements in the middle
        return ((x[n / 2] + x[n / 2 - 1]) / 2.0);
    } else {
        // else return the element in the middle
        return x[n / 2];
    }
}


static void finish_measurement(void *arg)
{
    struct sensor *sensor = (struct sensor *) arg;
    struct gpio_ultrasound_state *state = (struct gpio_ultrasound_state *) sensor->driver_data;
    struct sensor_measurement out;
    double valid_measurements[N_MEASUREMENTS], median, mean;
    char buf[100], *s = buf;
    int i, n_invalid = 0, n_valid = 0;;

    if (state->n_measurements < N_MEASUREMENTS) {
        start_measurement(sensor);
        return;
    }
    for (i = 0; i < state->n_measurements; i++) {
        s += sprintf(s, "%3.3f m   ", state->measurements[i]);
    }
    LOG(LL_INFO, ("%s", buf));

    median = calc_median(state->n_measurements, state->measurements);
    for (i = 0; i < state->n_measurements; i++) {
        double m = state->measurements[i];
        if (m < (1 - MEASUREMENT_TOLERANCE / 100.0) * median ||
            m > (1 + MEASUREMENT_TOLERANCE / 100.0) * median) {
            n_invalid++;
            continue;
        }
        valid_measurements[n_valid++] = m;
    }

    if (n_invalid > 2) {
        LOG(LL_ERROR, ("Too many out-of-tolerance measurements"));
        return;
    }
    mean = 0;
    for (i = 0; i < n_valid; i++)
        mean += valid_measurements[i];
    mean /= n_valid;

    memset(&out, 0, sizeof(out));

    out.property_name = "distance";
    out.unit = "m";
    out.type = SENSOR_FLOAT;
    out.float_val = mean;
    out.precision = 3;

    sensors_report(sensor, &out, 1);
}

static void gpio_ultrasound_int_handler(int gpio, void *arg)
{
    struct sensor *sensor = (struct sensor *) arg;
    struct gpio_ultrasound_state *state = (struct gpio_ultrasound_state *) sensor->driver_data;
    double echo_time, diff;

    mgos_gpio_disable_int(state->echo_gpio);

    if (!state->trig_time) // spurious interrupt?
        return;
    if (state->timeout_timer) {
        mgos_clear_timer(state->timeout_timer);
        state->timeout_timer = 0;
    }

    echo_time = mgos_uptime();
    diff = (echo_time - state->trig_time) * 1000000;

    state->measurements[state->n_measurements++] = state->m_per_us * diff / 2;
    mgos_set_timer(0, 0, finish_measurement, sensor);
}

int gpio_ultrasound_poll(struct sensor *sensor, struct sensor_measurement *out)
{
    struct gpio_ultrasound_state *state = (struct gpio_ultrasound_state *) sensor->driver_data;

    state->n_measurements = 0;
    state->n_timeouts = 0;
    state->m_per_us = sound_m_per_us(20);
    if (start_measurement(sensor) < 0)
        return -1;

    return 0;
}

static int test_connection(struct sensor *sensor)
{
    struct gpio_ultrasound_state *state = (struct gpio_ultrasound_state *) sensor->driver_data;
    int j;

    mgos_gpio_write(state->trig_gpio, 1);
    mgos_usleep(50);
    mgos_gpio_write(state->trig_gpio, 0);

    for (j = 0; j < 10000; j++) {
        if (mgos_gpio_read(state->echo_gpio))
            break;
        mgos_usleep(1);
    }
    if (j == 10000) {
        LOG(LL_ERROR, ("Ultrasound echo GPIO did not go high"));
        return -1;
    }
    for (j = 0; j < 500; j++) {
        if (!mgos_gpio_read(state->echo_gpio))
            break;
        mgos_msleep(1);
    }
    if (j == 500) {
        LOG(LL_ERROR, ("Ultrasound echo GPIO did not go low"));
        return -1;
    }

    return 0;
}

int gpio_ultrasound_init(struct sensor *sensor)
{
    struct gpio_ultrasound_state *state;
    int echo_gpio = sensor->gpio, trig_gpio = sensor->output_gpio;

    LOG(LL_INFO, ("Initializing GPIO ultrasound sensor (trig GPIO %d, echo GPIO %d)",
                  trig_gpio, echo_gpio));
    if (echo_gpio < 0 || trig_gpio < 0) {
        LOG(LL_ERROR, ("Ultrasound GPIOs not configured"));
        return -1;
    }

    state = malloc(sizeof(*state));
    memset(state, 0, sizeof(*state));
    state->echo_gpio = echo_gpio;
    state->trig_gpio = trig_gpio;
    sensor->driver_data = (void *) state;

    mgos_gpio_write(trig_gpio, 0);
    mgos_gpio_set_mode(trig_gpio, MGOS_GPIO_MODE_OUTPUT);
    mgos_usleep(100);
    mgos_gpio_set_mode(echo_gpio, MGOS_GPIO_MODE_INPUT);
    mgos_gpio_set_int_handler_isr(echo_gpio, MGOS_GPIO_INT_EDGE_NEG, gpio_ultrasound_int_handler, sensor);

    if (test_connection(sensor) < 0) {
        free(state);
        return -1;
    }

    LOG(LL_INFO, ("GPIO ultrasound initialized"));

    return 0;
}
