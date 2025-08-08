# Vogon Sensor

An ESP-IDF (v6) project for an ESP32-based environmental / air quality sensor node. It gathers readings from attached sensors (DHT22 temperature & humidity, SDS011 particulate matter) and syncs data via MQTT. Designed to be lightweight and power‑aware.

## Features

-   Reads temperature and humidity data from DHT22 sensor
-   Reads particulate matter (PM2.5 and PM10) data from SDS011 sensor
-   Sends sensor data to a server via MQTT
-   Connects to WiFi for internet connectivity
-   Easy configuration and setup via BLE configuration interface

## Hardware Requirements

-   ESP32 development board
-   DHT22 temperature and humidity sensor
-   SDS011 particulate matter sensor
-   Breadboard and jumper wires for connections
-   Power supply (5V USB or battery)

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

Steps:

```bash
idf.py set-target esp32      # (only once if not cached)
idf.py menuconfig            # Adjust options (optional)
idf.py build                 # Compile
idf.py flash monitor         # Flash and open serial monitor
```

(Use `Ctrl + ]` to exit monitor; pass `-p <port>` if auto-detect fails.)

## Configuration

Project / feature toggles live in Kconfig menus (run `idf.py menuconfig`). Defaults are captured in `sdkconfig.defaults`; a generated working config is `sdkconfig` (ignored in VCS).

## MQTT Topics and messages

By default, the firmware publishes sensor data to the following MQTT topic: `vogonair/:mac_address/raw`.

## Acknowledgment

Source code heavily inspired by [github.com/Sibyx/vogon-air-sensor](https://github.com/Sibyx/vogon-air-sensor).
