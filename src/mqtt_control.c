#include <mgos.h>
#include <mgos_mqtt.h>

#define TOPIC_PREFIX            "thing/"
#define LOG_TOPIC_SUFFIX        "/log"
#define ERROR_LOG_TOPIC_SUFFIX  "/log/error"
#define STATS_TOPIC_SUFFIX      "/stats"
#define CONTROL_TOPIC_SUFFIX      "/control"
#define MAX_BUFFER_LINES        64
#define MQTT_LOG_SEND_DELAY     1000

#define UART_DEBUG


#ifdef ESP32
#include <rom/rtc.h>
static const char *get_reset_reason(void)
{
    const char *s;

    switch (rtc_get_reset_reason(0)) {
    case NO_MEAN:
        s = "no_mean";
        break;
    case POWERON_RESET:
        s = "poweron_reset";
        break;
    case SW_RESET:
        s = "sw_reset";
        break;
    case OWDT_RESET:
        s = "owdt_reset";
        break;
    case DEEPSLEEP_RESET:
        s = "deepsleep_reset";
        break;
    case SDIO_RESET:
        s = "sdio_reset";
        break;
    case TG0WDT_SYS_RESET:
        s = "tg0wdt_sys_reset";
        break;
    case TG1WDT_SYS_RESET:
        s = "tg1wdt_sys_reset";
        break;
    case RTCWDT_SYS_RESET:
        s = "rtcwdt_sys_reset";
        break;
    case INTRUSION_RESET:
        s = "intrusion_reset";
        break;
    case TGWDT_CPU_RESET:
        s = "tgwdt_cpu_reset";
        break;
    case SW_CPU_RESET:
        s = "sw_cpu_reset";
        break;
    case RTCWDT_CPU_RESET:
        s = "rtcwdt_cpu_reset";
        break;
    case EXT_CPU_RESET:
        s = "ext_cpu_reset";
        break;
    case RTCWDT_BROWN_OUT_RESET:
        s = "rtcwdt_brown_out_reset";
        break;
    case RTCWDT_RTC_RESET:
        s = "rtcwdt_rtc_reset";
        break;
    default:
        s = "unknown";
        break;
    }

    return s;
}

#else

static const char *get_reset_reason(void)
{
    return "unknown";
}

#endif

extern enum cs_log_level cs_log_cur_msg_level;

struct log_message {
    int8_t log_level;
    unsigned int seq_nr:24;
    double time;

    struct log_message *next;
    unsigned int msg_len;
    char msg[];
};

static bool mqtt_connected, mqtt_logging_active;
static struct log_message *logline_buffer, *logline_buffer_tail;
static int n_loglines;
static uint32_t log_sequence_nr;
static char *mqtt_topic;
static char *mqtt_log_topic;
static char *mqtt_error_log_topic;
static char *mqtt_stats_topic;
static char *mqtt_control_topic;

#ifdef UART_DEBUG
#define debug_printf(args...) mgos_uart_printf(0, args); mgos_uart_flush(0)
#else
#define debug_printf(args...)
#endif


static bool publish_log_message(int log_level, unsigned int seq_nr, double time, const char *msg_data, int msg_len)
{
    struct mbuf mbuf;
    struct json_out out = JSON_OUT_MBUF(&mbuf);
    const char *topic;
    bool sent;

    mbuf_init(&mbuf, 100);
    json_printf(&out, "{seq_nr: %u, log_level: %d, time: %.3lf, msg: %.*Q}", seq_nr, log_level, time, msg_len, msg_data);
    if (log_level == 0)
        topic = mqtt_error_log_topic;
    else
        topic = mqtt_log_topic;

    debug_printf("publishing %s: %.*s\n", topic, mbuf.len, mbuf.buf);
    sent = mgos_mqtt_pub(topic, mbuf.buf, mbuf.len, 0, false);

    mbuf_free(&mbuf);
    return sent;
}

static void append_log_message(int log_level, unsigned int seq_nr, double time, const char *msg_data, int msg_len)
{
    struct log_message *msg;

    if (n_loglines >= MAX_BUFFER_LINES)
        return;

    msg = malloc(sizeof(*msg) + msg_len);
    msg->log_level = log_level;
    msg->seq_nr = log_sequence_nr;
    msg->time = time;
    msg->next = NULL;

    memcpy(msg->msg, msg_data, msg_len);
    msg->msg_len = msg_len;

    if (!logline_buffer) {
        logline_buffer = msg;
        logline_buffer_tail = msg;
    } else {
        logline_buffer_tail->next = msg;
        logline_buffer_tail = msg;
    }
    n_loglines++;
}

static void send_log_buffer(void)
{
    struct log_message *msg;

    for (msg = logline_buffer; msg != NULL; msg = logline_buffer) {
        publish_log_message(msg->log_level, msg->seq_nr, msg->time, msg->msg, msg->msg_len);
        logline_buffer = msg->next;
        free(msg);
    }
    logline_buffer_tail = NULL;
    n_loglines = 0;
}

static void mqtt_log_handler(int ev, void *ev_data, void *userdata)
{
    struct mgos_debug_hook_arg *arg = (struct mgos_debug_hook_arg *) ev_data;
    double time;
    unsigned int seq_nr;
    int log_level = cs_log_cur_msg_level;
    bool sent = false;

    if (log_level == LL_NONE)
        return;

    seq_nr = log_sequence_nr++;
    time = mg_time();
    if (mqtt_logging_active) {
        if (n_loglines)
            send_log_buffer();
        sent = publish_log_message(log_level, seq_nr, time, arg->data, arg->len);
    }
    if (!sent)
        append_log_message(log_level, seq_nr, time, arg->data, arg->len);
}

static void send_loglines(void *arg)
{
    if (!n_loglines || !mqtt_connected)
        return;

    debug_printf("Sending buffered loglines\n");
    send_log_buffer();
    mqtt_logging_active = true;
}

static void handle_mqtt_connection(void)
{
    char msg[100];
    struct json_out out = JSON_OUT_BUF(msg, sizeof(msg));

    json_printf(&out, "{online:%B,uptime:%lf,reset_reason:%Q}",
                1, mgos_uptime(), get_reset_reason());

    debug_printf("MQTT connected\n");
    mqtt_connected = true;
    mgos_mqtt_pub(mqtt_topic, msg, strlen(msg), 0, false);
    mgos_set_timer(1000, 0, send_loglines, NULL);    
}

static void mqtt_ev_handler(struct mg_connection *c, int ev, void *p, void *user_data)
{
    if (ev == MG_EV_MQTT_CONNACK) {
        handle_mqtt_connection();
    } else if (ev == MG_EV_MQTT_DISCONNECT) {
        mqtt_connected = false;
        mqtt_logging_active = false;
    }
}

static void send_stats(void *arg)
{
    int rssi;
    unsigned int free_heap_size;
    char buf[100];
    struct json_out out = JSON_OUT_BUF(buf, sizeof(buf));

    if (!mqtt_connected)
        return;

    rssi = mgos_wifi_sta_get_rssi();
    free_heap_size = mgos_get_free_heap_size();
    json_printf(&out, "{uptime:%.3lf,wifi_rssi:%d,free_heap_size:%u}",
                mgos_uptime(), rssi, free_heap_size);
    debug_printf("%s: %s", mqtt_stats_topic, buf);

    mgos_mqtt_pub(mqtt_stats_topic, buf, strlen(buf), 0, true);
}

static void mqtt_control_handler(struct mg_connection *nc, int ev,
                                 void *ev_data, void *user_data)
{
    struct mg_mqtt_message *msg = (struct mg_mqtt_message *) ev_data;
    struct mg_str *payload = &msg->payload;
    char *command;

    if (ev != MG_EV_MQTT_PUBLISH)
        return;

    if (json_scanf(payload->p, payload->len, "{command: %Q}", &command) != 1) {
        LOG(LL_ERROR, ("Malformed control input"));
        return;
    }
    if (strcmp(command, "reboot") == 0)
        mgos_system_restart_after(100);

    free(command);
}


void mqtt_control_init(void)
{
    const char *device_id;

    device_id = mgos_sys_config_get_device_id();
    if (!device_id) {
        LOG(LL_ERROR, ("Device ID not set, unable to initialize MQTT control"));
        return;
    }

    n_loglines = 0;
    logline_buffer = NULL;

    if (mgos_mqtt_get_global_conn() != NULL) {
        handle_mqtt_connection();
    } else
        mqtt_connected = false;

    mg_asprintf(&mqtt_topic, 0, "%s%s", TOPIC_PREFIX, device_id);
    mg_asprintf(&mqtt_log_topic, 0, "%s%s", mqtt_topic, LOG_TOPIC_SUFFIX);
    mg_asprintf(&mqtt_error_log_topic, 0, "%s%s", mqtt_topic, ERROR_LOG_TOPIC_SUFFIX);
    mg_asprintf(&mqtt_stats_topic, 0, "%s%s", mqtt_topic, STATS_TOPIC_SUFFIX);
    mg_asprintf(&mqtt_control_topic, 0, "%s%s", mqtt_topic, CONTROL_TOPIC_SUFFIX);

    if (!mgos_sys_config_get_mqtt_will_topic())
        mgos_sys_config_set_mqtt_will_topic(mqtt_topic);
    if (!mgos_sys_config_get_mqtt_will_message())
        mgos_sys_config_set_mqtt_will_message("{\"online\":false}");

    mgos_event_add_handler(MGOS_EVENT_LOG, mqtt_log_handler, NULL);
    mgos_mqtt_add_global_handler(mqtt_ev_handler, NULL);

    mgos_mqtt_global_subscribe(mg_mk_str(mqtt_control_topic), mqtt_control_handler, NULL);

    mgos_set_timer(60000, MGOS_TIMER_REPEAT, send_stats, NULL);
}
