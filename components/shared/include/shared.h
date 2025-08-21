#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define DEVICE_NAME "Vogon"
#define NVS_PARTITION "nvs_app"
#define NVS_NAMESPACE "vogon"

// NVS keys - max 15 characters

#define NVS_KEY_GENERAL_MEASUREMENT_INTERVAL "read_interval"

#define NVS_KEY_SENSORS_DHT22_MEASUREMENT_BULK_SIZE "dht_bulk_size"
#define NVS_KEY_SENSORS_DHT22_MEASUREMENT_BULK_SLEEP "dht_bulk_sleep"

#define NVS_KEY_SENSORS_SDS011_WARM_UP "sds_warm_up"
#define NVS_KEY_SENSORS_SDS011_MEASUREMENT_BULK_SIZE "sds_bulk_size"
#define NVS_KEY_SENSORS_SDS011_MEASUREMENT_BULK_SLEEP "sds_bulk_sleep"

#define NVS_KEY_SYNC_WIFI_SSID "wifi_ssid"
#define NVS_KEY_SYNC_WIFI_PASSWORD "wifi_password"
#define NVS_KEY_SYNC_MQTT_BROKER_URL "mqtt_broker_url"

// ===== ===== ===== =====

typedef struct
{
	float temperature;
	float humidity;
	uint16_t pm25;
	uint16_t pm10;
} shared_data_t;

typedef struct {
	int GENERAL_MEASUREMENT_INTERVAL;
	int SENSORS_DHT22_MEASUREMENT_BULK_SIZE;
	int SENSORS_DHT22_MEASUREMENT_BULK_SLEEP;
	int SENSORS_SDS011_WARM_UP;
	int SENSORS_SDS011_MEASUREMENT_BULK_SIZE;
	int SENSORS_SDS011_MEASUREMENT_BULK_SLEEP;
	char SYNC_WIFI_SSID[32];
	char SYNC_WIFI_PASSWORD[64];
	char SYNC_MQTT_BROKER_URL[256];
} shared_config_t;

extern SemaphoreHandle_t sync_mutex;
extern shared_data_t shared_data;
extern shared_config_t shared_config;

esp_err_t load_shared_config();
