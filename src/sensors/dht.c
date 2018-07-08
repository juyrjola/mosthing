#include "mgos.h"
#include "mgos_dht.h"
#include "sensors.h"

int dht_poll(struct sensor *sensor, struct sensor_measurement *out)
{
    struct mgos_dht *dht = (struct mgos_dht *) sensor->driver_data;
    float temp, humidity;
    int n_values = 0;

    temp = mgos_dht_get_temp(dht);
    humidity = mgos_dht_get_humidity(dht);

    if (temp > -60 && temp < 160) {
        out->property_name = "temperature";
        out->unit = "C";
        out->type = SENSOR_FLOAT;
        out->float_val = temp;
        n_values++;
        out++;
    } else
        LOG(LL_ERROR, ("Temperature read failed (val %f)", temp));

    if (humidity > -20 && humidity < 120) {
        out->property_name = "humidity";
        out->unit = "%";
        out->type = SENSOR_FLOAT;
        out->float_val = humidity;
        n_values++;
        out++;
    } else
        LOG(LL_ERROR, ("Humidity read failed (val %f)", humidity));

    return n_values;
}

int dht_init(struct sensor *sensor)
{
    struct mgos_dht *dht;
    struct sensor_measurement data[2];
    int ret, i;

    LOG(LL_INFO, ("Initializing DHT sensor on GPIO %d", sensor->gpio));
    dht = mgos_dht_create(sensor->gpio, DHT22);
    if (dht == NULL) {
        LOG(LL_ERROR, ("DHT not detected"));
        return -1;
    }
    sensor->driver_data = (void *) dht;

    for (i = 0; i < 5; i++) {
        ret = dht_poll(sensor, data);
        if (ret > 0)
            break;
        mgos_msleep(500);
    }
    if (i == 5) {
        mgos_dht_close(dht);
        LOG(LL_INFO, ("DHT not detected"));
        return -1;
    }

    LOG(LL_INFO, ("DHT initialized"));

    return 0;
}
