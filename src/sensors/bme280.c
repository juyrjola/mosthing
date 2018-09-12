#include <mgos.h>
#include <mgos_arduino_bme280.h>
#include <mgos_i2c.h>
#include "sensors.h"

#define BME280_I2C_ADDRESS   0x76

static void bme280_reset(struct sensor *sensor)
{
    Adafruit_BME280 *bme = sensor->driver_data;

    LOG(LL_INFO, ("Resetting BME280"));
    if (sensor->power_gpio >= 0) {
        mgos_gpio_write(sensor->power_gpio, 0);
        mgos_msleep(10);
        mgos_gpio_write(sensor->power_gpio, 1);
        mgos_msleep(10);
    }
    if (!mgos_bme280_begin(bme, BME280_I2C_ADDRESS))
        LOG(LL_ERROR, ("BME280 reset failed"));
}

int bme280_poll(struct sensor *sensor, struct sensor_measurement *out)
{
    Adafruit_BME280 *bme = sensor->driver_data;
    int temp = mgos_bme280_read_temperature(bme);
    int humidity = mgos_bme280_read_humidity(bme);
    int pres = mgos_bme280_read_pressure(bme);
    int n_values = 0;
    float val;

    val = temp / 100.0;
    if (temp != MGOS_BME280_RES_FAIL && val >= -100 && val <= 120) {
        out->property_name = "temperature";
        out->unit = "C";
        out->type = SENSOR_FLOAT;
        out->float_val = val;
        n_values++;
        out++;
    } else {
        LOG(LL_ERROR, ("Temperature read failed (val %d)", temp));
        goto reset;
    }

    val = humidity / 100.0;
    // Check that humidity values are sane
    if (humidity != MGOS_BME280_RES_FAIL && val < 100 && val > 0) {
        out->property_name = "humidity";
        out->unit = "%";
        out->type = SENSOR_FLOAT;
        out->float_val = val;
        n_values++;
        out++;
    } else {
        LOG(LL_ERROR, ("Humidity read failed (val %d)", humidity));
        goto reset;
    }

    val = pres / 10000.0;
    if (pres != MGOS_BME280_RES_FAIL && val > 700 && val < 1200) {
        out->property_name = "pressure";
        out->unit = "hPa";
        out->type = SENSOR_FLOAT;
        out->float_val = val;
        n_values++;
        out++;
    } else {
        LOG(LL_ERROR, ("Pressure read failed (val %d)", pres));
        goto reset;
    }

    return n_values;

reset:
    bme280_reset(sensor);
    return 0;
}

int bme280_init(struct sensor *sensor)
{
    Adafruit_BME280 *bme;
    int ok;

    LOG(LL_INFO, ("Initializing BME280"));
    if (!mgos_sys_config_get_i2c_enable()) {
        LOG(LL_ERROR, ("I2C not enabled"));
        return -1;
    }
    if (sensor->power_gpio >= 0) {
        mgos_gpio_write(sensor->power_gpio, 0);
        mgos_gpio_set_mode(sensor->power_gpio, MGOS_GPIO_MODE_OUTPUT);
        mgos_msleep(10);
        mgos_gpio_write(sensor->power_gpio, 1);
        mgos_msleep(10);
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
