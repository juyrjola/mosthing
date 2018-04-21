#include "mgos.h"

static int status_led;

static void timer_cb(void *arg)
{
    LOG(LL_INFO, ("%s", "timer_cb"));
    if (status_led) {
        mgos_gpio_toggle(status_led);
    }
    (void) arg;
}

extern void net_watchdog_init(void);
extern void sensors_init(void);

enum mgos_app_init_result mgos_app_init(void)
{
    if (status_led)
        mgos_gpio_set_mode(status_led, MGOS_GPIO_MODE_OUTPUT);
    mgos_set_timer(1000, MGOS_TIMER_REPEAT, timer_cb, NULL);

    net_watchdog_init();
    sensors_init();

    return MGOS_APP_INIT_SUCCESS;
}
