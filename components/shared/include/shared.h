#pragma once

#include <stdint.h>

#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "wifi.h"

#define DEVICE_NAME "Vogon"
#define NVS_PARTITION "nvs_app"
#define NVS_NAMESPACE "vogon"

// NVS keys - max 15 characters

#define NVS_KEY_CONFIG "config"

#define CFG_KEY_SYNC_WIFI_SSID "wifi_ssid"
#define CFG_KEY_SYNC_WIFI_USERNAME "wifi_username"
#define CFG_KEY_SYNC_WIFI_PASSWORD "wifi_password"
#define CFG_KEY_SYNC_WIFI_PROTOCOL "wifi_protocol"
#define CFG_KEY_SYNC_MQTT_BROKER_URL "mqtt_broker_url"
#define CFG_KEY_SENSORS_GENERAL_MEASUREMENT_INTERVAL "measurement_interval"
#define CFG_KEY_SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SIZE "environmental_bulk_size"
#define CFG_KEY_SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SLEEP "environmental_bulk_sleep"
#define CFG_KEY_SENSORS_PARTICULATE_WARM_UP "particulate_warm_up"
#define CFG_KEY_SENSORS_PARTICULATE_MEASUREMENT_BULK_SIZE "particulate_bulk_size"
#define CFG_KEY_SENSORS_PARTICULATE_MEASUREMENT_BULK_SLEEP "particulate_bulk_sleep"

// ===== ===== ===== =====

typedef struct {
	float temperature;
	float humidity;
	uint16_t pm25;
	uint16_t pm10;
} shared_data_t;

typedef struct {
	int GENERAL_MEASUREMENT_INTERVAL;

	int SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SIZE;
	int SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SLEEP;

	int SENSORS_PARTICULATE_WARM_UP;
	int SENSORS_PARTICULATE_MEASUREMENT_BULK_SIZE;
	int SENSORS_PARTICULATE_MEASUREMENT_BULK_SLEEP;

	char SYNC_WIFI_SSID[32];
	char SYNC_WIFI_USERNAME[64];
	char SYNC_WIFI_PASSWORD[64];
	wifi_auth_mode_t SYNC_WIFI_PROTOCOL;

	char SYNC_MQTT_BROKER_URL[256];
} shared_config_t;

extern SemaphoreHandle_t sync_mutex;
extern shared_data_t shared_data;
extern shared_config_t shared_config;

esp_err_t load_shared_config();
esp_err_t nvs_read_str(const char *key, char **value, size_t *len, const char *default_value);
