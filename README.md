# One-headed-dog

A Cerberus 2100 emulator firmware for the Olimex ESP32-SBC-FabGL.

## How to do stuff

Build:
```
pio run
```

Flash
```
pio run -t upload
```

Generate clangd config for code autocomplete:
```
pio run --target compiledb
```
