#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "helpers.h"
#include "shared.h"

static const char *TAG = "MODULE[shared]";

SemaphoreHandle_t sync_mutex;
shared_data_t shared_data;
shared_config_t shared_config;

typedef enum {
	TYPE_INT,
	TYPE_STR
} handle_type_t;

typedef struct {
	void *destination;
	char *json_key;
	handle_type_t type;
} config_mapping_t;

config_mapping_t mappings[] = {
	{&shared_config.GENERAL_MEASUREMENT_INTERVAL, CFG_KEY_SENSORS_GENERAL_MEASUREMENT_INTERVAL, TYPE_INT},

	{&shared_config.SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SIZE, CFG_KEY_SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SIZE, TYPE_INT},
	{&shared_config.SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SLEEP, CFG_KEY_SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SLEEP, TYPE_INT},

	{&shared_config.SENSORS_PARTICULATE_WARM_UP, CFG_KEY_SENSORS_PARTICULATE_WARM_UP, TYPE_INT},
	{&shared_config.SENSORS_PARTICULATE_MEASUREMENT_BULK_SIZE, CFG_KEY_SENSORS_PARTICULATE_MEASUREMENT_BULK_SIZE, TYPE_INT},
	{&shared_config.SENSORS_PARTICULATE_MEASUREMENT_BULK_SLEEP, CFG_KEY_SENSORS_PARTICULATE_MEASUREMENT_BULK_SLEEP, TYPE_INT},

	{shared_config.SYNC_WIFI_SSID, CFG_KEY_SYNC_WIFI_SSID, TYPE_STR},
	{&shared_config.SYNC_WIFI_PROTOCOL, CFG_KEY_SYNC_WIFI_PROTOCOL, TYPE_INT},
	{shared_config.SYNC_WIFI_USERNAME, CFG_KEY_SYNC_WIFI_USERNAME, TYPE_STR},
	{shared_config.SYNC_WIFI_PASSWORD, CFG_KEY_SYNC_WIFI_PASSWORD, TYPE_STR},

	{shared_config.SYNC_MQTT_BROKER_URL, CFG_KEY_SYNC_MQTT_BROKER_URL, TYPE_STR}};

esp_err_t load_shared_config() {
	nvs_handle_t nvs_handle;
	esp_err_t ret = nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE, NVS_READONLY, &nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
		return ret;
	}

	size_t required_size = 0;
	ret = nvs_get_str(nvs_handle, NVS_KEY_CONFIG, NULL, &required_size);
	RETURN_ON_ERROR(ret);

	char *json_string = malloc(required_size);
	if (json_string == NULL) {
		ESP_LOGE(TAG, "Failed to allocate memory for config string");
		nvs_close(nvs_handle);
		return ESP_ERR_NO_MEM;
	}

	ret = nvs_get_str(nvs_handle, NVS_KEY_CONFIG, json_string, &required_size);
	RETURN_ON_ERROR(ret);

	nvs_close(nvs_handle);

	cJSON *root = cJSON_Parse(json_string);
	if (root == NULL) {
		ESP_LOGE(TAG, "Error before: [%s]\n", cJSON_GetErrorPtr());
		return ESP_FAIL;
	}

	for (size_t i = 0; i < sizeof(mappings) / sizeof(config_mapping_t); i++) {
		config_mapping_t *mapping = &mappings[i];

		switch (mapping->type) {
			case TYPE_INT: {
				cJSON *item = cJSON_GetObjectItemCaseSensitive(root, mapping->json_key);

				if (cJSON_IsNumber(item)) {
					*((int *)mapping->destination) = item->valueint;
				}

				break;
			}

			case TYPE_STR: {
				cJSON *item = cJSON_GetObjectItemCaseSensitive(root, mapping->json_key);

				if (cJSON_IsString(item) && (item->valuestring != NULL)) {
					strncpy((char *)mapping->destination, item->valuestring, required_size);
				}

				break;
			}

			default:
				ESP_LOGW(TAG, "Unknown mapping type for key %s", mapping->json_key);
				break;
		}
	}

	return ESP_OK;
}
