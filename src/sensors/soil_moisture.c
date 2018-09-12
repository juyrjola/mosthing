#include <mgos_adc.h>
#include <mgos.h>
#include "sensors.h"


int soil_moisture_poll(struct sensor *sensor, struct sensor_measurement *out)
{
    int val;

    val = mgos_adc_read(sensor->gpio);
    printf("ADC %d\n", val);

    return 0;
}


int soil_moisture_init(struct sensor *sensor)
{
    LOG(LL_INFO, ("Initializing soil moisture sensor (GPIO %d)", sensor->gpio));
    if (sensor->gpio < 0) {
        LOG(LL_ERROR, ("Soil moisture GPIO not configured"));
        return -1;
    }

    if (!mgos_adc_enable(sensor->gpio)) {
        LOG(LL_ERROR, ("Unable to enable ADC on GPIO %d", sensor->gpio));
        return -1;
    }

    LOG(LL_INFO, ("Soil moisture sensor initialized"));

    return 0;
}
