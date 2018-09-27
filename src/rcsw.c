#include <mgos.h>
#include "bitset/bitset.h"

#define MAX_RX_CHANGES      180
#define TOLERANCE_US        200
#define TOLERANCE_PERCENT   20
#define MIN_PULSES          14

struct timing {
    uint16_t time_us:15;
    uint8_t state:1;
} __attribute__((packed));

struct rf433 {
    int input_gpio;

    mgos_timer_id rx_timeout_timer;

    double last_rx_time;
    uint16_t rx_id;
    uint16_t cur_rx, n_rx, sync_pos;
    struct timing rx_rb[MAX_RX_CHANGES]; // Ring buffer for RX
};

struct rf433_hi_lo {
    uint8_t high;
    uint8_t low;
};

struct rf433_protocol {
    const char *name;

    uint16_t pulse_length;
    struct rf433_hi_lo sync;
    struct rf433_hi_lo zero;
    struct rf433_hi_lo one;
    struct rf433_hi_lo pause;

    int (* decode)(const struct rf433_protocol *proto, struct bitset *);
    int (* find_sync)(const struct rf433_protocol *proto, int n_rx, const struct timing *rx);
};

static int calc_tolerance(int time)
{
    int tolerance;

    tolerance = time * TOLERANCE_PERCENT / 100;
    if (tolerance < TOLERANCE_US)
        tolerance = TOLERANCE_US;
    return tolerance;
}

static bool check_timing(const struct rf433_protocol *proto,
                         const struct timing *rx, uint16_t target_pulses,
                         int target_state)
{
    int tolerance;
    uint16_t target_time;

    target_time = proto->pulse_length * target_pulses;
    tolerance = calc_tolerance(target_time);

    if (rx->state != target_state)
        return false;
    if (rx->time_us + tolerance < target_time)
        return false;
    if (rx->time_us > target_time + tolerance)
        return false;
    return true;
}

static int nexa_decode(const struct rf433_protocol *proto, struct bitset *code)
{
    uint32_t transmitter_id;
    uint8_t code_buf[40], unit_id, channel;
    struct bitset *out;

    out = bitset_init(code_buf, sizeof(code_buf));
    if (code->n_bits != 64) {
        LOG(LL_ERROR, ("Odd number of Nexa bits: %d", code->n_bits));
        return -1;
    }
    for (int i = 0; i < code->n_bits / 2; i++) {
        bool bit = bitset_get(code, i * 2);

        if (bitset_get(code, i * 2 + 1) != !bit) {
            LOG(LL_ERROR, ("Nexa decode error (pos %d)", i));
            return -1;
        }
        bitset_append(out, bit);
    }
    bitset_copy(code, out);

    transmitter_id = 0;
    for (int i = 0; i < 26; i++)
        transmitter_id |= bitset_get(code, i) ? (1 << i) : 0;

    channel = (bitset_get(code, 28) << 1) | bitset_get(code, 29);
    unit_id = (1 << bitset_get(code, 30)) | bitset_get(code, 31);
    if (channel == 3)
        unit_id = ~unit_id & 0x03;
    unit_id = unit_id + 1;

    printf("transmitter 0x%08x %s unit %d: %s\n",
           transmitter_id, bitset_get(code, 26) ? "" : "group",
           unit_id, bitset_get(code, 27) ? "off" : "on");

    (void) proto;
    return 0;
}

static int everflourish_find_sync(const struct rf433_protocol *proto, int n_rx, const struct timing *rx)
{
    if (n_rx < 10 + 90)
        return -1;

    // Everflourish starts with three to five sync pulses.
    for (int i = 0; i <= n_rx - (10 + 90); i++, rx++) {
        int j, k;

        for (j = 0; j < 5; j++) {
            if (!check_timing(proto, rx + j * 2, proto->sync.high, 1))
                break;
            if (!check_timing(proto, rx + j * 2 + 1, proto->sync.low, 0))
                break;
        }
        //printf("check %d (%d)\n", i, j);
        if (j < 3)
            continue;

        //printf("possible sync at %d\n", i);
        // After finding sync, make sure the following bits are valid.
        for (k = 0; k < 10; k++) {
            const struct timing *t = rx + j * 2 + k * 2;

            //printf("checking %d %u   %d %u\n", t[0].state, t[0].time_us, t[1].state, t[1].time_us);
            if (check_timing(proto, t, proto->zero.high, 1) && check_timing(proto, t + 1, proto->zero.low, 0))
                continue;
            if (check_timing(proto, t, proto->one.high, 1) && check_timing(proto, t + 1, proto->one.low, 0))
                continue;
            break;
        }
        if (k != 10) {
            printf("pos %d invalid\n", i + j * 2 + k * 2);
            continue;
        }

        return i + j * 2;
    }
    return -1;
}

static int generic_find_sync(const struct rf433_protocol *proto, int n_rx, const struct timing *rx)
{
    int i, min_high_pulse;

    min_high_pulse = proto->sync.high * proto->pulse_length;
    min_high_pulse -= calc_tolerance(min_high_pulse);

    for (i = 0; i < n_rx - MIN_PULSES; i++) {
        const struct timing *t = rx + i;

        if (!check_timing(proto, t, proto->sync.low, 0))
            continue;
        // The high pulse before the first sync might be longer than what we
        // expect, probably because we're still receiving noise.
        if (i > 1 && (t[-1].state == 0 || t[-1].time_us < min_high_pulse))
            continue;
        // It's a match!
        break;
    }
    if (i == n_rx - MIN_PULSES)
        return -1;

    return i + 1;
}

static int generic_check_pause(const struct rf433_protocol *proto, const struct timing *rx)
{
    uint16_t min_time;

    if (!proto->pause.high || !proto->pause.low)
        return -1;

    if (!check_timing(proto, rx, proto->pause.high, 1))
        return -1;

    // Low pulse must be at least this many pulses long
    min_time = (proto->pause.low * proto->pulse_length);
    min_time -= calc_tolerance(min_time);
    if (rx[1].state || rx[1].time_us < min_time)
        return -1;

    return 0;
}

static struct rf433_protocol protocols[] = {
    // 1180us     590us    pause 16ms
    {"everflourish",    590, { 1,  1}, {1,  2}, {1,  1}, {  1,  27}, .find_sync = everflourish_find_sync},    // everflourish
    {"nexa",            250, { 1, 10}, {1,  5}, {1,  1}, {  1,  40}, .decode = nexa_decode},
    {"1",               350, { 1, 31}, {1,  3}, {3,  1}, },    // protocol 1
    {"2",               650, { 1, 10}, {1,  2}, {2,  1}, },    // protocol 2
    {"3",               100, {30, 71}, {4, 11}, {9,  6}, },    // protocol 3
    {"4",               380, { 1,  6}, {1,  3}, {3,  1}, },    // protocol 4
    {"5",               500, { 6, 14}, {1,  2}, {2,  1}, },    // protocol 5
};

static const struct protocol *get_protocol()


static int parse_rx(uint16_t rx_id, const struct rf433_protocol *proto, int n_rx,
                    const struct timing *rx)
{
    const struct timing *rx_start = rx;
    int rx_left, sync_start, ret;
    uint8_t code_buf[32];
    struct bitset *code;

    rx_left = n_rx;
    if (proto->find_sync != NULL)
        ret = proto->find_sync(proto, n_rx, rx);
    else
        ret = generic_find_sync(proto, n_rx, rx);

    if (ret < 0) {
        //printf("[%04x] %s: Sync word not found (%d pulses RX)\n", rx_id, proto->name, n_rx);
        return -1;
    }
    rx_left -= ret;
    rx += ret;

    sync_start = ret;
    printf("[%04x] %s: Packet found at %d (pulses %d)\n", rx_id, proto->name, sync_start, rx_left);

    code = bitset_init(code_buf, sizeof(code_buf));
    while (rx_left > 1) {
        int bit;

        if (check_timing(proto, rx, proto->zero.high, 1) &&
            check_timing(proto, rx + 1, proto->zero.low, 0))
            bit = 0;
        else if (check_timing(proto, rx, proto->one.high, 1) && 
                 check_timing(proto, rx + 1, proto->one.low, 0))
            bit = 1;
        else if (generic_check_pause(proto, rx) == 0) {
                // It's the pause "bit" between transmissions
                break;
        } else {
            printf("No match at location %d (%d %u  %d %u)\n",
                   rx - rx_start, rx[0].state, rx[0].time_us, rx[1].state, rx[1].time_us);
            return -1;
        }
        //printf("%d %d : %d %d -> %d\n", rx->state, rx->time_us, rx[1].state, rx[1].time_us, bit);
        bitset_append(code, bit);
        if (code->n_bits == code->max_bits) {
            printf("Too many bits!!\n");
            break;
        }
        rx_left -= 2;
        rx += 2;
    }
    if (proto->decode != NULL) {
        int ret = proto->decode(proto, code);

        if (ret < 0)
            return -1;
    }
    int i, zeroes = 0;
    for (i = 0; i < code->n_bits; i++) {
        if (!bitset_get(code, i))
            zeroes++;
    }
    if (1) {
        char buf[128];

        bitset_print(buf, code);
        printf("code: %s\n", buf);
#if 0
        for (int i = 0; i < n_rx; i++) {
            printf("\t%u %u\n", rx_start[i].state, rx_start[i].time_us);
        }
#endif
    }
#if 0
    if (zeroes > 5*8) {
        printf("all zeroes\n");
        return -1;
    }
#endif
    return rx - rx_start;
}

static void rf433_rx_done(void *arg)
{
    struct rf433 *rf = (struct rf433 *) arg;
    struct timing timings[MAX_RX_CHANGES];
    uint32_t rx_id;
    uint16_t sync_pos, pos, n_rx;
    int i, n_protocols = sizeof(protocols) / sizeof(protocols[0]);

    mgos_gpio_disable_int(rf->input_gpio);
    rx_id = rf->rx_id;
    sync_pos = rf->sync_pos;
    n_rx = rf->n_rx;
    pos = (MAX_RX_CHANGES + sync_pos - n_rx) % MAX_RX_CHANGES;
    for (i = 0; i < n_rx; i++) {
        timings[i] = rf->rx_rb[pos];
        pos++;
        if (pos == MAX_RX_CHANGES)
            pos = 0;
    }
    mgos_gpio_enable_int(rf->input_gpio);

    if (n_rx < MIN_PULSES)
        return;

    for (i = 0; i < n_protocols; i++) {
        int ret = parse_rx(rx_id, protocols + i, n_rx, timings);

        if (ret >= 0) {
            // We found a match, remove the processed bytes from the
            // ring buffer.
            mgos_gpio_disable_int(rf->input_gpio);
            // Make sure we have not received another packet while
            // processing the current one.
            if (rf->rx_id == rx_id && rf->n_rx > ret)
                rf->n_rx -= ret;
            mgos_gpio_enable_int(rf->input_gpio);
            break;
        }
    }
    if (i == n_protocols) {
        for (i = 0; i < n_rx / 2; i++) {
            const struct timing *t = timings + 2*i;
            printf("%3d. %d %5u   %d %5u\n", i * 2, t[0].state, t[0].time_us, t[1].state, t[1].time_us);
        }
    }
}

static void rf433_int_handler(int pin, void *arg)
{
    struct rf433 *rf = (struct rf433 *) arg;
    struct timing *t;
    double now = mgos_uptime();
    int state;
    unsigned int diff_us;

    state = mgos_gpio_read(rf->input_gpio);
    diff_us = (int) ((now - rf->last_rx_time) * 1000000);
    rf->last_rx_time = now;

    // Store the time for the last received state change.
    t = rf->rx_rb + rf->cur_rx;
    t->time_us = diff_us;

    if (diff_us < 80) {
        // Too short pulse, reset RX
        rf->n_rx = 0;
        t->state = state;
        return;
    }

    // Advance to the next slot in the ring buffer.
    rf->cur_rx = (rf->cur_rx + 1);
    if (rf->cur_rx == MAX_RX_CHANGES)
        rf->cur_rx = 0;

    t = rf->rx_rb + rf->cur_rx;
    t->state = state;
    if (rf->n_rx < MAX_RX_CHANGES)
        rf->n_rx++;

    // If we have enough pulses in store and this pulse is long enough
    // to separate transmissions, attempt to decode.
    if (rf->n_rx > MIN_PULSES && diff_us > 4300) {
        rf->sync_pos = rf->cur_rx;
        rf->rx_id++;
        mgos_invoke_cb(rf433_rx_done, rf, true);
    }

    (void) pin;
}

void test_433(void)
{
    struct rf433 *rf = (struct rf433 *) malloc(sizeof(*rf));

    memset(rf, 0, sizeof(*rf));
    rf->input_gpio = 4;
    rf->last_rx_time = mgos_uptime();

    mgos_gpio_set_mode(rf->input_gpio, MGOS_GPIO_MODE_INPUT);
    mgos_gpio_set_int_handler_isr(rf->input_gpio, MGOS_GPIO_INT_EDGE_ANY, rf433_int_handler, rf);
    mgos_gpio_enable_int(rf->input_gpio);
}


#if 0
#include "RCSwitch.h"

RCSwitch rcswitch = RCSwitch();

static void check_timer(void *arg)
{
    if (!rcswitch.available())
        return;
    printf("val %08lx bl %u delay %u proto %u\n", rcswitch.getReceivedValue(),
        rcswitch.getReceivedBitlength(), rcswitch.getReceivedDelay(),
        rcswitch.getReceivedProtocol());
    rcswitch.resetAvailable();
}

static void init_rcswitch(void)
{
    printf("Initializing RCSwitch\n");
    mgos_gpio_set_mode(input_gpio, MGOS_GPIO_MODE_INPUT);
    rcswitch.enableReceive(input_gpio);
    mgos_set_timer(100, MGOS_TIMER_REPEAT, check_timer, NULL);
    printf("RCSwitch initialized\n");
}

extern "C" {
    void test_433(void) {
        init_rcswitch();
    }
}

#endif
