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
	char *json_string = NULL;
	size_t json_len = 0;
	esp_err_t ret = nvs_read_str(NVS_KEY_CONFIG, &json_string, &json_len, NULL);

	if (json_string == NULL)
		return ESP_FAIL;

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
					size_t max_size = sizeof(mapping->destination) - 1; // Ensure space for null terminator
					strncpy((char *)mapping->destination, item->valuestring, max_size);
					((char *)mapping->destination)[strlen(item->valuestring)] = '\0';
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

esp_err_t nvs_read_str(const char *key, char **value, size_t *len, const char *default_value) {
	nvs_handle_t nvs_handle;
	esp_err_t ret = nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
	RETURN_ON_ERROR(ret);

	ret = nvs_get_str(nvs_handle, key, NULL, len);

	if (ret == ESP_ERR_NVS_NOT_FOUND) {
		nvs_close(nvs_handle);

		if (default_value == NULL) {
			*value = NULL;
			*len = 0;
			return ESP_ERR_NVS_NOT_FOUND;
		}

		*len = strlen(default_value) + 1;
		*value = malloc(*len);

		if (*value == NULL) {
			return ESP_ERR_NO_MEM;
		}

		memcpy(*value, default_value, *len);
		return ESP_ERR_NVS_NOT_FOUND;
	}

	if (ret != ESP_OK) {
		nvs_close(nvs_handle);
		return ret;
	}

	*value = malloc(*len);
	if (*value == NULL) {
		nvs_close(nvs_handle);
		return ESP_ERR_NO_MEM;
	}

	ret = nvs_get_str(nvs_handle, key, *value, len);
	nvs_close(nvs_handle);

	if (ret != ESP_OK) {
		free(*value);
		*value = NULL;
		*len = 0;
		return ret;
	}

	return ESP_OK;
}
