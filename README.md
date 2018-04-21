# Mongoose OS framework for internet-enabled things

## Building

First [download and install](https://mongoose-os.com/software.html) the `mos` tool. If you don't have Docker installed, remove `--local` from the commands below.

When building for a new platform for the first time, run `mos build` like this:

```
mos build --verbose --local --platform esp8266
```

Repeat builds get faster by preventing lib update:

```
mos build --verbose --no-libs-update --local --platform esp8266
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
