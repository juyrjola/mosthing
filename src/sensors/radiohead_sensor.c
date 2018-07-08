#include <stdint.h>
#include "radiohead_sensor.h"
#include "radiohead.h"
#include "rfreport.h"
#include "sensors.h"
#include "mgos.h"

static bool rh_sensor_initialized = false;

int rh_sensor_init()
{
    if (!radiohead_is_initialized()) {
        LOG(LL_ERROR, ("Radiohead not initialized"));
        return -1;
    }
    rh_sensor_initialized = 1;
    return 0;
}

int rh_sensor_is_initialized()
{
    return rh_sensor_initialized;
}

int rh_sensor_handle_message(const uint8_t *buf, unsigned int buf_len)
{
    struct rf_sensor_report *report;

    if (rf_report_decode_msg(buf, buf_len, &report) < 0)
        return -1;
    sensors_handle_rf_report(report);
    free(report);

    return 0;
}

int rh_sensor_send_message(const uint8_t *buf, unsigned int buf_len)
{
    return radiohead_send_sensor_report(buf, buf_len);
}
