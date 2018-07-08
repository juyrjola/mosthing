#include <stdlib.h>
#include "mgos.h"
#include "mgos_onewire.h"
#include "sensors.h"


static int ds18b20_read_temperature(int pin, int res, float *temperature)
{
    bool found = false;
    uint8_t rom[8], data[9];
    int16_t raw;
    int us, cfg;
    struct mgos_onewire *ow;

    // Step 1: Determine config
    if (res == 9) { // 9-bit resolution (93.75ms delay)
        cfg = 0x1f;
        us = 93750;
    } else if (res == 10) { // 10-bit resolution (187.5ms delay)
        cfg = 0x3f;
        us = 187500;
    } else if (res == 11) { // 11-bit resolution (375ms delay)
        cfg = 0x5f;
        us = 375000;
    } else {  // 12-bit resolution (750ms delay)
        cfg = 0x7f;
        us = 750000;
    }

    // Step 2: Find all the sensors
    ow = mgos_onewire_create(pin);
    mgos_onewire_search_clean(ow);
    while (mgos_onewire_next(ow, rom, 1)) {
        if (rom[0] != 0x28)
            continue; // Skip devices that are not DS18B20's
        if (found) {
            LOG(LL_ERROR, ("OneWire bus has more than one DS18B20 sensor. This is unsupported."));
            break;
        }
        // Only use the first found DS18B20 for now.
        found = true;
    }
    if (!found) {
        LOG(LL_ERROR, ("DS18B20 not found on OneWire bus"));
        return -1;
    }

    // Step 3: Write the configuration
    mgos_onewire_reset(ow);                         // Reset
    mgos_onewire_write(ow, 0xcc);                   // Skip Rom
    mgos_onewire_write(ow, 0x4e);                   // Write to scratchpad
    mgos_onewire_write(ow, 0x00);                   // Th or User Byte 1
    mgos_onewire_write(ow, 0x00);                   // Tl or User Byte 2
    mgos_onewire_write(ow, cfg);                    // Configuration register
    mgos_onewire_write(ow, 0x48);                   // Copy scratchpad
    
    // Step 4: Start temperature conversion
    mgos_onewire_reset(ow);                         // Reset
    mgos_onewire_write(ow, 0xcc);                   // Skip Rom
    mgos_onewire_write(ow, 0x44);                   // Start conversion
    mgos_usleep(us);                                // Wait for conversion

    // Step 5: Read the temperatures
    mgos_onewire_reset(ow);                     // Reset
    mgos_onewire_select(ow, rom);               // Select the device
    mgos_onewire_write(ow, 0xbe);               // Issue read command

    mgos_onewire_read_bytes(ow, data, 9);       // Read the 9 data bytes
    raw = (data[1] << 8) | data[0];             // Get the raw temperature
    cfg = (data[4] & 0x60);                     // Read the config (just in case)
    if (cfg == 0x00)
        raw = raw & ~7;       // 9-bit raw adjustment
    else if (cfg == 0x20)
        raw = raw & ~3;       // 10-bit raw adjustment
    else if (cfg == 0x40)
        raw = raw & ~1;       // 11-bit raw adjustment
    *temperature = (float) raw / 16.0;

    mgos_onewire_close(ow);                     // Close one wire

    return 0;
}

int ds18b20_poll(struct sensor_data *sensor, struct sensor_measurement *out)
{
    float temp;
    int ret;

    ret = ds18b20_read_temperature(sensor->pin, 12, &temp);
    if (ret < 0)
        return -1;

    if (temp > -60 && temp < 160) {
        out->property_name = "temperature";
        out->unit = "C";
        out->type = SENSOR_FLOAT;
        out->float_val = temp;
    } else {
        LOG(LL_ERROR, ("Invalid temperature value: %f", temp));
        return -1;
    }
    return 1;
}

int ds18b20_init(struct sensor_data *sensor)
{
    struct sensor_measurement out;

    LOG(LL_INFO, ("Initializing DS18B20 sensor (GPIO %d, power GPIO %d)", sensor->pin,
                  sensor->power_gpio));
    if (sensor->power_gpio > 0) {
        mgos_gpio_set_mode(sensor->power_gpio, MGOS_GPIO_MODE_OUTPUT);
        mgos_gpio_write(sensor->power_gpio, 1);
    }
    if (ds18b20_poll(sensor, &out) < 0)
        return -1;
    LOG(LL_INFO, ("DS18B20 initialized"));

    return 0;
}
