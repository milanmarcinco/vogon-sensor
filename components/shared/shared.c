#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "shared.h"

static const char *TAG = "MODULE[shared]";

SemaphoreHandle_t sync_mutex;
shared_data_t shared_data;
shared_config_t shared_config;

typedef enum {
	TYPE_U16,
	TYPE_STR
} handle_type_t;

typedef struct {
	void *destination;
	char *nvs_key;
	handle_type_t type;
} config_mapping_t;

config_mapping_t mappings[] = {
	{&shared_config.GENERAL_MEASUREMENT_INTERVAL, NVS_KEY_GENERAL_MEASUREMENT_INTERVAL, TYPE_U16},

	{&shared_config.SENSORS_DHT22_MEASUREMENT_BULK_SIZE, NVS_KEY_SENSORS_DHT22_MEASUREMENT_BULK_SIZE, TYPE_U16},
	{&shared_config.SENSORS_DHT22_MEASUREMENT_BULK_SLEEP, NVS_KEY_SENSORS_DHT22_MEASUREMENT_BULK_SLEEP, TYPE_U16},

	{&shared_config.SENSORS_SDS011_WARM_UP, NVS_KEY_SENSORS_SDS011_WARM_UP, TYPE_U16},
	{&shared_config.SENSORS_SDS011_MEASUREMENT_BULK_SIZE, NVS_KEY_SENSORS_SDS011_MEASUREMENT_BULK_SIZE, TYPE_U16},
	{&shared_config.SENSORS_SDS011_MEASUREMENT_BULK_SLEEP, NVS_KEY_SENSORS_SDS011_MEASUREMENT_BULK_SLEEP, TYPE_U16},

	{shared_config.SYNC_WIFI_SSID, NVS_KEY_SYNC_WIFI_SSID, TYPE_STR},
	{&shared_config.SYNC_WIFI_PROTOCOL, NVS_KEY_SYNC_WIFI_PROTOCOL, TYPE_U16},
	{shared_config.SYNC_WIFI_USERNAME, NVS_KEY_SYNC_WIFI_USERNAME, TYPE_STR},
	{shared_config.SYNC_WIFI_PASSWORD, NVS_KEY_SYNC_WIFI_PASSWORD, TYPE_STR},

	{shared_config.SYNC_MQTT_BROKER_URL, NVS_KEY_SYNC_MQTT_BROKER_URL, TYPE_STR}};

esp_err_t load_shared_config() {
	nvs_handle_t nvs_handle;
	esp_err_t ret = nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE, NVS_READONLY, &nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
		return ret;
	}

	for (size_t i = 0; i < sizeof(mappings) / sizeof(config_mapping_t); i++) {
		config_mapping_t *mapping = &mappings[i];

		switch (mapping->type) {
			case TYPE_U16: {
				uint16_t value;
				ret = nvs_get_u16(nvs_handle, mapping->nvs_key, &value);
				if (ret != ESP_OK) break;

				*(uint16_t *)(mapping->destination) = value;

				break;
			}

			case TYPE_STR: {
				size_t required_size = 0;
				ret = nvs_get_str(nvs_handle, mapping->nvs_key, NULL, &required_size);
				if (ret != ESP_OK) break;

				char *value = malloc(required_size);
				if (value == NULL) break;

				ret = nvs_get_str(nvs_handle, mapping->nvs_key, value, &required_size);
				if (ret != ESP_OK) break;

				strncpy((char *)mapping->destination, value, required_size);
				free(value);

				break;
			}
		}
	}

	nvs_close(nvs_handle);
	return ret;
}
