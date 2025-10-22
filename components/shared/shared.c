#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "helpers.h"
#include "shared.h"

static const char *TAG = "MODULE[shared]";

SemaphoreHandle_t sync_mutex;
shared_data_t shared_data = {0};
shared_config_t shared_config = {0};

typedef enum {
	TYPE_INT,
	TYPE_STR
} handle_type_t;

typedef struct {
	void *destination;
	size_t destination_size;
	char *json_key;
	handle_type_t type;

	union {
		int default_int;
		const char *default_str;
	};
} config_mapping_t;

config_mapping_t mappings[] = {
	{&shared_config.SENSORS_GENERAL_MEASUREMENT_INTERVAL, sizeof(int), CFG_KEY_SENSORS_GENERAL_MEASUREMENT_INTERVAL, TYPE_INT, .default_int = CONFIG_SENSORS_GENERAL_MEASUREMENT_INTERVAL},

	{&shared_config.SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SIZE, sizeof(int), CFG_KEY_SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SIZE, TYPE_INT, .default_int = CONFIG_SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SIZE},
	{&shared_config.SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SLEEP, sizeof(int), CFG_KEY_SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SLEEP, TYPE_INT, .default_int = CONFIG_SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SLEEP},

	{&shared_config.SENSORS_PARTICULATE_WARM_UP, sizeof(int), CFG_KEY_SENSORS_PARTICULATE_WARM_UP, TYPE_INT, .default_int = CONFIG_SENSORS_PARTICULATE_WARM_UP},
	{&shared_config.SENSORS_PARTICULATE_MEASUREMENT_BULK_SIZE, sizeof(int), CFG_KEY_SENSORS_PARTICULATE_MEASUREMENT_BULK_SIZE, TYPE_INT, .default_int = CONFIG_SENSORS_PARTICULATE_MEASUREMENT_BULK_SIZE},
	{&shared_config.SENSORS_PARTICULATE_MEASUREMENT_BULK_SLEEP, sizeof(int), CFG_KEY_SENSORS_PARTICULATE_MEASUREMENT_BULK_SLEEP, TYPE_INT, .default_int = CONFIG_SENSORS_PARTICULATE_MEASUREMENT_BULK_SIZE},

	{shared_config.SYNC_WIFI_SSID, sizeof(char) * 32, CFG_KEY_SYNC_WIFI_SSID, TYPE_STR, .default_str = ""},
	{shared_config.SYNC_WIFI_USERNAME, sizeof(char) * 64, CFG_KEY_SYNC_WIFI_USERNAME, TYPE_STR, .default_str = ""},
	{shared_config.SYNC_WIFI_PASSWORD, sizeof(char) * 64, CFG_KEY_SYNC_WIFI_PASSWORD, TYPE_STR, .default_str = ""},
	{&shared_config.SYNC_WIFI_PROTOCOL, sizeof(int), CFG_KEY_SYNC_WIFI_PROTOCOL, TYPE_INT, .default_int = CONFIG_SYNC_WIFI_PROTOCOL},

	{shared_config.SYNC_MQTT_BROKER_URL, sizeof(char) * 256, CFG_KEY_SYNC_MQTT_BROKER_URL, TYPE_STR, .default_str = CONFIG_SYNC_MQTT_BROKER_URL}};

static bool ensure_config() {
	bool conditions[] = {
		shared_config.SENSORS_GENERAL_MEASUREMENT_INTERVAL > 0,
		shared_config.SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SIZE > 0,
		shared_config.SENSORS_ENVIRONMENTAL_MEASUREMENT_BULK_SLEEP > 0,
		shared_config.SENSORS_PARTICULATE_WARM_UP >= 0,
		shared_config.SENSORS_PARTICULATE_MEASUREMENT_BULK_SIZE > 0,
		shared_config.SENSORS_PARTICULATE_MEASUREMENT_BULK_SLEEP > 0,

		strlen(shared_config.SYNC_MQTT_BROKER_URL) > 0,
		strlen(shared_config.SYNC_WIFI_SSID) > 0,

		(shared_config.SYNC_WIFI_PROTOCOL == WIFI_AUTH_WPA2_PSK
			 ? strlen(shared_config.SYNC_WIFI_PASSWORD) > 0
			 : true),

		(shared_config.SYNC_WIFI_PROTOCOL == WIFI_AUTH_WPA2_ENTERPRISE
			 ? strlen(shared_config.SYNC_WIFI_USERNAME) > 0 && strlen(shared_config.SYNC_WIFI_PASSWORD) > 0
			 : true)};

	for (size_t i = 0; i < sizeof(conditions) / sizeof(bool); i++)
		if (!conditions[i])
			return false;

	return true;
}

wifi_auth_mode_t wifi_auth_mode_from_string(const char *str) {
	if (strcmp(str, "open") == 0) {
		return WIFI_AUTH_OPEN;
	} else if (strcmp(str, "wpa2") == 0) {
		return WIFI_AUTH_WPA2_PSK;
	} else if (strcmp(str, "wpa2e") == 0) {
		return WIFI_AUTH_WPA2_ENTERPRISE;
	} else {
		// Default to open if unknown
		return WIFI_AUTH_OPEN;
	}
}

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
		cJSON *item = cJSON_GetObjectItemCaseSensitive(root, mapping->json_key);

		if (strcmp(mapping->json_key, CFG_KEY_SYNC_WIFI_PROTOCOL) == 0) {
			if (cJSON_IsString(item) && (item->valuestring != NULL)) {
				wifi_auth_mode_t protocol = wifi_auth_mode_from_string(item->valuestring);
				*((wifi_auth_mode_t *)mapping->destination) = protocol;
			}

			continue;
		}

		switch (mapping->type) {
			case TYPE_INT: {
				if (cJSON_IsNumber(item)) {
					*((int *)mapping->destination) = item->valueint;
				} else {
					*((int *)mapping->destination) = mapping->default_int;
				}

				break;
			}

			case TYPE_STR: {
				char *value = NULL;

				if (cJSON_IsString(item) && (item->valuestring != NULL)) {
					value = item->valuestring;
				} else {
					value = (char *)mapping->default_str;
				}

				strncpy((char *)mapping->destination, value, mapping->destination_size - 1);
				((char *)mapping->destination)[strlen(value)] = '\0';

				break;
			}

			default:
				ESP_LOGW(TAG, "Unknown mapping type for key %s", mapping->json_key);
				break;
		}
	}

	return ensure_config() ? ESP_OK : ESP_FAIL;
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
