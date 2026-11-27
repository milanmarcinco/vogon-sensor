# Vogon Sensor

An ESP-IDF (v6) project for an ESP32-based environmental / air quality sensor node. It gathers readings from attached sensors (DHT22 temperature & humidity, SDS011 particulate matter, SCD41 CO2) and syncs data via MQTT. Designed to be lightweight and power‑aware.

## Features

-   Reads temperature and humidity data from DHT22 sensor
-   Reads particulate matter (PM2.5 and PM10) data from SDS011 sensor
-   Reads CO2, temperature and humidity data from SCD41 sensor
-   Sends sensor data to a server via MQTT
-   Connects to WiFi for internet connectivity
-   Easy configuration and setup via BLE configuration interface

## Hardware Requirements

-   ESP32 development board
-   DHT22 temperature and humidity sensor
-   SDS011 particulate matter sensor
-   SCD41 CO2 sensor
-   Breadboard and jumper wires for connections
-   Power supply (5V USB or battery)

## Wiring

| Sensor | Sensor pin | ESP32 pin |
| ------ | ---------- | --------- |
| DHT22  | DATA       | GPIO23    |
| SDS011 | TXD        | GPIO16    |
| SDS011 | RXD        | GPIO17    |
| SCD41  | SDA        | GPIO21    |
| SCD41  | SCL        | GPIO22    |
| SCD41  | VDD        | 3V3       |

Power the SCD41 from 3V3 so its I2C pull-ups stay at 3.3 V. The SCD41 measures continuously, including while the ESP32 deep sleeps; the firmware reads it on each wake-up.

## Partition Table Summary

| Name     | Type | SubType | Offset  | Size  | Purpose                |
| -------- | ---- | ------- | ------- | ----- | ---------------------- |
| phy_init | data | phy     | 0x9000  | 4K    | System data            |
| nvs      | data | nvs     | 0xA000  | 12K   | System NVS             |
| nvs_app  | data | nvs     | 0xD000  | 12K   | Persistent app storage |
| factory  | app  | factory | 0x10000 | 1500K | Main firmware image    |

## Build & Flash

Prerequisites:

-   ESP-IDF exported environment (IDF_PATH set).
-   MQTT TLS files in `certs/`: `ca.pem`, `client.pem`, and `client.key` (not committed).

Steps:

```bash
idf.py set-target esp32      # (only once if not cached)
idf.py menuconfig            # Adjust options (optional)
idf.py build                 # Compile
idf.py flash monitor         # Flash and open serial monitor
```

(Use `Ctrl + ]` to exit monitor; pass `-p <port>` if auto-detect fails.)

## Configuration

Project / feature toggles live in Kconfig menus (run `idf.py menuconfig`). Shared defaults live in `sdkconfig.defaults`; optional `sdkconfig.local` overrides them for a local build and is ignored in VCS. The generated config lives in `build/sdkconfig` and is regenerated when either defaults file changes. Persist configuration changes in the defaults files; menuconfig changes are lost on regeneration. Do not use `idf.py save-defconfig` with local secrets loaded.

Project options are defined in `main/Kconfig.projbuild`; for example, `config BLE_IDLE_TIMEOUT` becomes `CONFIG_BLE_IDLE_TIMEOUT`. Keep local values in `sdkconfig.local`, including disabled booleans as `# CONFIG_DEBUG_SKIP_MEASUREMENTS is not set`. After changing an option in menuconfig, copy its setting from `build/sdkconfig` to `sdkconfig.local` to persist it. The build generates `build/config/sdkconfig.h` for C code; do not edit that header.

## MQTT Topics and messages

By default, the firmware publishes sensor data to the following MQTT topic: `vogonair/:mac_address/raw`.

Each message is a JSON object `{"sensor": <id>, "parameter": <id>, "value": <number>}`:

| Sensor        | Parameter                | Unit  |
| ------------- | ------------------------ | ----- |
| `0x01` DHT22  | `0x01` temperature       | °C    |
| `0x01` DHT22  | `0x02` humidity          | %     |
| `0x02` SDS011 | `0x03` PM2.5             | µg/m³ |
| `0x02` SDS011 | `0x04` PM10              | µg/m³ |
| `0x03` SCD41  | `0x05` CO2               | ppm   |
| `0x03` SCD41  | `0x06` temperature       | °C    |
| `0x03` SCD41  | `0x07` humidity          | %     |

The SCD41 options can also be set over BLE in the config JSON: `co2_bulk_size`, `co2_bulk_sleep` (seconds) and `co2_altitude` (meters above sea level, 0–3000, used for CO2 compensation).

## Acknowledgment

Source code heavily inspired by [github.com/Sibyx/vogon-air-sensor](https://github.com/Sibyx/vogon-air-sensor).
