#include "mgos.h"
#include "mgos_arduino_bme280.h"
#include "sensors.h"


int bme280_poll(struct sensor_data *sensor, struct sensor_measurement *out)
{
    Adafruit_BME280 *bme = sensor->driver_data;
    int temp = mgos_bme280_read_temperature(bme);
    int humidity = mgos_bme280_read_humidity(bme);
    int pres = mgos_bme280_read_pressure(bme);
    int n_values = 0;

    if (temp != MGOS_BME280_RES_FAIL && temp >= -100 && temp < 200) {
        out->property_name = "temperature";
        out->unit = "C";
        out->type = SENSOR_FLOAT;
        out->float_val = temp / 100.0;
        n_values++;
        out++;
    } else
        LOG(LL_ERROR, ("Temperature read failed"));

    // Check that humidity values are sane
    if (humidity != MGOS_BME280_RES_FAIL && humidity < 200 && humidity >= 0) {
        out->property_name = "humidity";
        out->unit = "%";
        out->type = SENSOR_FLOAT;
        out->float_val = humidity / 100.0;
        n_values++;
        out++;
    } else
        LOG(LL_ERROR, ("Humidity read failed"));

    if (pres != MGOS_BME280_RES_FAIL && pres > 200 && pres < 2000) {
        out->property_name = "pressure";
        out->unit = "hPa";
        out->type = SENSOR_FLOAT;
        out->float_val = pres / 10000.0;
        n_values++;
        out++;
    } else
        LOG(LL_ERROR, ("Pressure read failed"));

    if (!n_values)
        return -1;

    return n_values;
}

int bme280_init(struct sensor_data *sensor)
{
    Adafruit_BME280 *bme;
    int ok;

    LOG(LL_INFO, ("Initializing BME280"));
    if (!mgos_sys_config_get_i2c_enable()) {
        LOG(LL_ERROR, ("I2C not enabled"));
        return -1;
    }
    bme = mgos_bme280_create_i2c();
    ok = mgos_bme280_begin(bme, 0x76);
    if (!ok) {
        mgos_bme280_close(bme);
        LOG(LL_ERROR, ("BME280 not detected on I2C bus"));
        return -1;
    }
    sensor->driver_data = (void *) bme;
    LOG(LL_INFO, ("BME280 initialized"));

    return 0;
}
