#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "nvs_app.h"

#include "shared.h"

#define NVS_NAMESPACE "vogon"

static const char *TAG = "MODULE[NVS]";

uint16_t vogon_nvs_get_u16(const char *key, uint16_t default_value) {
	nvs_handle_t nvs_handle;
	esp_err_t ret = nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE, NVS_READONLY, &nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
		return default_value;
	}

	uint16_t value = default_value;
	ret = nvs_get_u16(nvs_handle, key, &value);
	nvs_close(nvs_handle);

	if (ret == ESP_ERR_NVS_NOT_FOUND) {
		return default_value;
	} else if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to read u16 from NVS for key '%s': %s", key, esp_err_to_name(ret));
		return default_value;
	}

	return value;
}

esp_err_t vogon_nvs_set_u16(const char *key, uint16_t value) {
	nvs_handle_t nvs_handle;
	esp_err_t ret = nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
		return ret;
	}

	ret = nvs_set_u16(nvs_handle, key, value);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to write u16 to NVS for key '%s': %s", key, esp_err_to_name(ret));
		nvs_close(nvs_handle);
		return ret;
	}

	ret = nvs_commit(nvs_handle);
	nvs_close(nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(ret));
		return ret;
	}

	return ESP_OK;
}

char *vogon_nvs_get_str(const char *key, const char *default_value) {
	nvs_handle_t nvs_handle;
	esp_err_t ret = nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
		return NULL;
	}

	size_t required_size = 0;
	ret = nvs_get_str(nvs_handle, key, NULL, &required_size);

	if (ret == ESP_ERR_NVS_NOT_FOUND) {
		nvs_close(nvs_handle);
		return strdup(default_value);
	} else if (ret != ESP_OK) {
		nvs_close(nvs_handle);
		ESP_LOGE(TAG, "Failed to get string size from NVS for key '%s': %s", key, esp_err_to_name(ret));
		return NULL;
	}

	char *value = malloc(required_size);

	if (value == NULL) {
		nvs_close(nvs_handle);
		ESP_LOGE(TAG, "Memory allocation failed for NVS string");
		return NULL;
	}

	ret = nvs_get_str(nvs_handle, key, value, &required_size);
	nvs_close(nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to read string from NVS for key '%s': %s", key, esp_err_to_name(ret));
		free(value);
		return NULL;
	}

	return value;
}

esp_err_t vogon_nvs_set_str(const char *key, const char *value) {
	nvs_handle_t nvs_handle;
	esp_err_t ret = nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
		return ret;
	}

	ret = nvs_set_str(nvs_handle, key, value);

	if (ret != ESP_OK) {
		nvs_close(nvs_handle);
		ESP_LOGE(TAG, "Failed to write string to NVS for key '%s': %s", key, esp_err_to_name(ret));
		return ret;
	}

	ret = nvs_commit(nvs_handle);
	nvs_close(nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(ret));
		return ret;
	}

	return ESP_OK;
}
