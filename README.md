# Mongoose OS framework for internet-enabled things

## Configuring

Use `generate_config.py` script to split a YAML config file into
JSON config files suitable for Mongoose OS. Here's an example config file:

```yaml
i2c:
    enable: true
    scl_gpio: 22
    sda_gpio: 21

wifi:
    sta:
        enable: true
        ssid: <ssid here>
        pass: <psk passphrase here>

mqtt:
    enable: true
    server: mqtt.example.com:1883
    user: <username>
    pass: <password>

sensors:
-   type: bme280
    poll_delay: 10000
    mqtt_topic: home/greenhouse
    power_gpio: 26
-   type: gpio_ultrasound
    poll_delay: 30000
    output_gpio: 16
    gpio: 17
    mqtt_topic: home/greenhouse/water_container

actuators:
-   driver: gpio_relay
    gpio: 27
    active_low: false
    max_time: 300000
    mqtt_topic: home/greenhouse/mist_pump
-   driver: gpio_relay
    gpio: 18
    active_low: false
    max_time: 60000
    mqtt_topic: home/greenhouse/microdrip_pump
```

## Building

First [download and install](https://mongoose-os.com/software.html) the `mos` tool. If you don't have Docker installed, remove `--local` from the commands below.

When building for a new platform for the first time, run `mos build` like this:

```
python generate_config.py <your-config.yaml> && mos build --verbose --local --platform esp8266
```

Repeat builds get faster by preventing lib update:

```
python generate_config.py <your-config.yaml> && mos build --verbose --no-libs-update --local --platform esp8266
```

## Flashing

```
# for esp8266
mos flash --verbose --esp-baud-rate 460800
# or for esp32:
mos flash --verbose --esp-baud-rate 921600
```

## Development

If you want to have Clang-based autocomplete in your code editor, you can
help it find all the header files by generating a configuration file:

```
for a in $(find -name include | cut -c3-) deps/mongoose-os/common deps/mongoose-os/frozen deps/mongoose-os build/gen ; do echo -I$a ; done > .clang_complete
```
