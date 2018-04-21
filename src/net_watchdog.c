#include "mgos.h"

#define DEFAULT_WAIT    (30 * 1000)
#define AFTER_FIRE_WAIT DEFAULT_WAIT
#define MAX_ATTEMPTS    10


static const char *resolve_names[] = {
    "google.com", "microsoft.com", "hs.fi", "google.fi"
};

static struct net_watchdog_state {
    struct mg_mgr *mgr;
    int attempt_count;
    int relay_gpio;
} watchdog_state;

static void net_watchdog_resolve_cb(struct mg_dns_message *dns_msg, void *user_data, enum mg_resolve_err err)
{
    struct net_watchdog_state *state = (struct net_watchdog_state *) user_data;

    if (err == MG_RESOLVE_OK) {
        LOG(LL_INFO, ("resolve OK!"));
        state->attempt_count = 0;
    } else {
        LOG(LL_INFO, ("resolve unsuccessful"));
    }
    (void) dns_msg;
}

static void net_watchdog_prime(struct net_watchdog_state *, int);

static void net_watchdog_fire_cb(void *arg)
{
    struct net_watchdog_state *state = (struct net_watchdog_state *) arg;

    mgos_gpio_write(state->relay_gpio, 1);
    LOG(LL_INFO, ("internet AP powered up"));

    state->attempt_count = 0;
    net_watchdog_prime(state, 0);
}

static void net_watchdog_fire(struct net_watchdog_state *state)
{
    LOG(LL_INFO, ("watchdog fired, network connection down"));

    // Reset internet AP
    mgos_gpio_write(state->relay_gpio, 0);
    mgos_set_timer(5000, 0, net_watchdog_fire_cb, state);
}

static void net_watchdog_timer_cb(void *arg)
{
    struct net_watchdog_state *state = (struct net_watchdog_state *) arg;
    int name_idx;

    LOG(LL_INFO, ("net watchdog timer (%d attempts)", state->attempt_count));
    name_idx = state->attempt_count % (sizeof(resolve_names)/sizeof(resolve_names[0]));
    state->attempt_count++;
    if (state->attempt_count == MAX_ATTEMPTS) {
        net_watchdog_fire(state);
        return;
    }
    mg_resolve_async(state->mgr, resolve_names[name_idx], MG_DNS_A_RECORD,
                     net_watchdog_resolve_cb, state);
    net_watchdog_prime(state, 0);
}

static void net_watchdog_prime(struct net_watchdog_state *state, int time)
{    
    if (time <= 0)
        time = DEFAULT_WAIT;
    mgos_set_timer(time, 0, net_watchdog_timer_cb, state);
}

int net_watchdog_init(void)
{
    int gpio;

    if (!mgos_sys_config_get_net_watchdog_enable()) {
        LOG(LL_INFO, ("Net watchdog disabled in config"));
        return 0;
    }
    gpio = mgos_sys_config_get_net_watchdog_relay_gpio();
    if (gpio < 0) {
        LOG(LL_ERROR, ("Relay GPIO not set"));
        return -1;
    }
    mgos_gpio_write(gpio, 1);
    mgos_gpio_set_mode(gpio, MGOS_GPIO_MODE_OUTPUT);

    memset(&watchdog_state, 0, sizeof(watchdog_state));
    watchdog_state.relay_gpio = gpio;
    watchdog_state.mgr = mgos_get_mgr();

    net_watchdog_prime(&watchdog_state, 0);
    LOG(LL_INFO, ("Net watchdog initialized"));

    return 0;
}
