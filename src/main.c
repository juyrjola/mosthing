#include <mgos.h>
#include "sensors.h"
#include "actuators.h"

static int status_led = -1;

static void timer_cb(void *arg)
{
    LOG(LL_INFO, ("%s", "timer_cb"));
    if (status_led >= 0) {
        mgos_gpio_toggle(status_led);
    }
    (void) arg;

#if 0
    wifi_ap_record_t wifidata;
    if (esp_wifi_sta_get_ap_info(&wifidata) == 0) {
        uint8_t *bid = wifidata.bssid;
        printf("%02x:%02x:%02x:%02x:%02x:%02x ch %d sec_ch %d rssi:%d\r\n",
            bid[0], bid[1], bid[2], bid[3], bid[4], bid[5],
            wifidata.primary, wifidata.second, wifidata.rssi);
    }
#endif
}

extern void net_watchdog_init(void);
extern void radiohead_init(void);
extern void display_init(void);

static void test_deep_sleep(void *args)
{
    printf("LED down\n");
    mgos_gpio_write(status_led, 1);
    mgos_msleep(1000);

    printf("Going to sleep\n");
    mgos_msleep(100);

    sensors_shutdown();
    mgos_gpio_set_mode(status_led, MGOS_GPIO_MODE_INPUT);
    //wifi_station_disconnect();
    //wifi_set_opmode_current(NULL_MODE);
    system_deep_sleep(10*60*1000*1000);
}

enum mgos_app_init_result mgos_app_init(void)
{
    if (status_led >= 0)
        mgos_gpio_set_mode(status_led, MGOS_GPIO_MODE_OUTPUT);
    //mgos_set_timer(1000, MGOS_TIMER_REPEAT, timer_cb, NULL);

    net_watchdog_init();
    sensors_init();
    actuators_init();
    //radiohead_init();
    //display_init();

    //mgos_gpio_write(status_led, 0);
    //mgos_msleep(1000);
    //mgos_set_timer(15000, 0, test_deep_sleep, NULL);

    return MGOS_APP_INIT_SUCCESS;
}
