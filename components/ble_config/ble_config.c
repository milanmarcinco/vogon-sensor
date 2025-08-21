#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "ble_config.h"
#include "internal/led.h"

#include "shared.h"

// static const char *TAG = "MODULE[BLUETOOTH]";
static const char *TAG_MAIN = "MODULE[BLUETOOTH][MAIN]";
static const char *TAG_GATTS = "MODULE[BLUETOOTH][GATTS]";
static const char *TAG_GATTS_PROFILE = "MODULE[BLUETOOTH][GATTS_PROFILE]";
static const char *TAG_GAP = "MODULE[BLUETOOTH][GAP]";

#define PROFILE_NUM 1 // Number of profiles in total

#define PROFILE_APP_IDX 0
#define PROFILE_APP_ID 0x00

// Type declaration

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

typedef enum {
	TYPE_U16,
	TYPE_STR
} handle_type_t;

typedef struct {
	uint16_t handle;
	const char *key;
	handle_type_t type;
} handle_mapping_t;

// Constants

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t characteristic_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t characteristic_declaration_size = sizeof(uint8_t);
static uint8_t characteristic_prop_read_write = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE;

// Profiles setup

static struct gatts_profile_instance {
	esp_gatts_cb_t gatts_cb; // profile’s event callback
	uint16_t gatts_if;		 // the interface handle assigned by the stack
} profile_table[PROFILE_NUM] = {
	[PROFILE_APP_IDX] = {
		.gatts_cb = gatts_profile_event_handler,
		.gatts_if = ESP_GATT_IF_NONE,
	}};

// Configuration service constants

static uint8_t config_service_uuid[ESP_UUID_LEN_128] = {
	// Configuration service uuid: d0a823a6-fa98-4597-b0c1-d8577be0e158
	0x58, 0xE1, 0xE0, 0x7B, 0x57, 0xD8, 0xC1, 0xB0, 0x97, 0x45, 0x98, 0xFA, 0xA6, 0x23, 0xA8, 0xD0};

#define NUM_CHARACTERISTICS 9
static uint16_t general_config_measurement_interval_characteristic_uuid = 0x0101;

static uint16_t sensors_config_measurement_dht22_bulk_size_characteristic_uuid = 0x0201;
static uint16_t sensors_config_measurement_dht22_bulk_sleep_characteristic_uuid = 0x0202;
static uint16_t sensors_config_measurement_sds011_warm_up_characteristic_uuid = 0x0203;
static uint16_t sensors_config_measurement_sds011_bulk_size_characteristic_uuid = 0x0204;
static uint16_t sensors_config_measurement_sds011_bulk_sleep_characteristic_uuid = 0x0205;

static uint16_t sync_config_wifi_ssid_characteristic_uuid = 0x0701;
static uint16_t sync_config_wifi_password_characteristic_uuid = 0x0702;
static uint16_t sync_config_mqtt_broker_url_characteristic_uuid = 0x0703;

enum {
	CONFIG_SERVICE_DECLARATION_IDX,

	GENERAL_CONFIG_MEASUREMENT_INTERVAL_CHARACTERISTIC_IDX,
	GENERAL_CONFIG_MEASUREMENT_INTERVAL_VALUE_IDX,
	SENSORS_CONFIG_DHT22_MEASUREMENT_BULK_SIZE_CHARACTERISTIC_IDX,
	SENSORS_CONFIG_DHT22_MEASUREMENT_BULK_SIZE_VALUE_IDX,
	SENSORS_CONFIG_DHT22_MEASUREMENT_BULK_SLEEP_CHARACTERISTIC_IDX,
	SENSORS_CONFIG_DHT22_MEASUREMENT_BULK_SLEEP_VALUE_IDX,
	SENSORS_CONFIG_SDS011_WARM_UP_CHARACTERISTIC_IDX,
	SENSORS_CONFIG_SDS011_WARM_UP_VALUE_IDX,
	SENSORS_CONFIG_SDS011_MEASUREMENT_BULK_SIZE_CHARACTERISTIC_IDX,
	SENSORS_CONFIG_SDS011_MEASUREMENT_BULK_SIZE_VALUE_IDX,
	SENSORS_CONFIG_SDS011_MEASUREMENT_BULK_SLEEP_CHARACTERISTIC_IDX,
	SENSORS_CONFIG_SDS011_MEASUREMENT_BULK_SLEEP_VALUE_IDX,
	SYNC_CONFIG_WIFI_SSID_CHARACTERISTIC_IDX,
	SYNC_CONFIG_WIFI_SSID_VALUE_IDX,
	SYNC_CONFIG_WIFI_PASSWORD_CHARACTERISTIC_IDX,
	SYNC_CONFIG_WIFI_PASSWORD_VALUE_IDX,
	SYNC_CONFIG_MQTT_BROKER_URL_CHARACTERISTIC_IDX,
	SYNC_CONFIG_MQTT_BROKER_URL_VALUE_IDX,

	CONFIG_SERVICE_IDX_MAX
};

// GAP setup

static esp_ble_adv_data_t adv_data = {
	.set_scan_rsp = false,
	.include_name = false,
	.include_txpower = false,

	.min_interval = 0x0005,
	.max_interval = 0x0010,
	.appearance = 0x00,

	.manufacturer_len = 0,
	.p_manufacturer_data = NULL,

	.service_data_len = 0,
	.p_service_data = NULL,

	.service_uuid_len = 0,
	.p_service_uuid = NULL,

	.flag = (ESP_BLE_ADV_FLAG_LIMIT_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_data_t adv_data_scan_rsp = {
	.set_scan_rsp = true,
	.include_name = true,
	.include_txpower = true,
	.manufacturer_len = 0,
	.p_manufacturer_data = NULL,
	.service_uuid_len = ESP_UUID_LEN_128,
	.p_service_uuid = config_service_uuid,
};

static esp_ble_adv_params_t adv_params = {
	.adv_int_min = 0x20,
	.adv_int_max = 0x40,
	.adv_type = ADV_TYPE_IND,
	.own_addr_type = BLE_ADDR_TYPE_PUBLIC,
	.channel_map = ADV_CHNL_ALL,
	.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
	ESP_LOGD(TAG_GAP, "[GAP_EVT]: Event %d", event);

	switch (event) {
		case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
			if (esp_ble_gap_start_advertising(&adv_params) == ESP_OK) {
				ESP_LOGI(TAG_GAP, "Advertising started");
			} else {
				ESP_LOGE(TAG_GAP, "Failed to start advertising");
			}

			break;
		case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
			if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
				ESP_LOGE(TAG_GAP, "Advertising start failed");
			} else {
				ESP_LOGI(TAG_GAP, "Advertising started successfully");
			}

			break;
		case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
			if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
				ESP_LOGE(TAG_GAP, "Advertising stop failed");
			} else {
				ESP_LOGI(TAG_GAP, "Advertising stopped successfully");
			}

			break;
		default:
			break;
	}
}

// Configuration service setup

static const esp_gatts_attr_db_t gatts_attr_db[CONFIG_SERVICE_IDX_MAX] =
	{
		// Configuration service declaration
		[CONFIG_SERVICE_DECLARATION_IDX] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ, ESP_UUID_LEN_128, sizeof(config_service_uuid), config_service_uuid}},

		[GENERAL_CONFIG_MEASUREMENT_INTERVAL_CHARACTERISTIC_IDX] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&characteristic_declaration_uuid, ESP_GATT_PERM_READ, characteristic_declaration_size, characteristic_declaration_size, &characteristic_prop_read_write}},
		[GENERAL_CONFIG_MEASUREMENT_INTERVAL_VALUE_IDX] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&general_config_measurement_interval_characteristic_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint8_t), 0, NULL}},
		[SENSORS_CONFIG_DHT22_MEASUREMENT_BULK_SIZE_CHARACTERISTIC_IDX] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&characteristic_declaration_uuid, ESP_GATT_PERM_READ, characteristic_declaration_size, characteristic_declaration_size, &characteristic_prop_read_write}},
		[SENSORS_CONFIG_DHT22_MEASUREMENT_BULK_SIZE_VALUE_IDX] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&sensors_config_measurement_dht22_bulk_size_characteristic_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint8_t), 0, NULL}},
		[SENSORS_CONFIG_DHT22_MEASUREMENT_BULK_SLEEP_CHARACTERISTIC_IDX] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&characteristic_declaration_uuid, ESP_GATT_PERM_READ, characteristic_declaration_size, characteristic_declaration_size, &characteristic_prop_read_write}},
		[SENSORS_CONFIG_DHT22_MEASUREMENT_BULK_SLEEP_VALUE_IDX] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&sensors_config_measurement_dht22_bulk_sleep_characteristic_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint8_t), 0, NULL}},
		[SENSORS_CONFIG_SDS011_WARM_UP_CHARACTERISTIC_IDX] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&characteristic_declaration_uuid, ESP_GATT_PERM_READ, characteristic_declaration_size, characteristic_declaration_size, &characteristic_prop_read_write}},
		[SENSORS_CONFIG_SDS011_WARM_UP_VALUE_IDX] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&sensors_config_measurement_sds011_warm_up_characteristic_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint8_t), 0, NULL}},
		[SENSORS_CONFIG_SDS011_MEASUREMENT_BULK_SIZE_CHARACTERISTIC_IDX] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&characteristic_declaration_uuid, ESP_GATT_PERM_READ, characteristic_declaration_size, characteristic_declaration_size, &characteristic_prop_read_write}},
		[SENSORS_CONFIG_SDS011_MEASUREMENT_BULK_SIZE_VALUE_IDX] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&sensors_config_measurement_sds011_bulk_size_characteristic_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint8_t), 0, NULL}},
		[SENSORS_CONFIG_SDS011_MEASUREMENT_BULK_SLEEP_CHARACTERISTIC_IDX] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&characteristic_declaration_uuid, ESP_GATT_PERM_READ, characteristic_declaration_size, characteristic_declaration_size, &characteristic_prop_read_write}},
		[SENSORS_CONFIG_SDS011_MEASUREMENT_BULK_SLEEP_VALUE_IDX] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&sensors_config_measurement_sds011_bulk_sleep_characteristic_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint8_t), 0, NULL}},
		[SYNC_CONFIG_WIFI_SSID_CHARACTERISTIC_IDX] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&characteristic_declaration_uuid, ESP_GATT_PERM_READ, characteristic_declaration_size, characteristic_declaration_size, &characteristic_prop_read_write}},
		[SYNC_CONFIG_WIFI_SSID_VALUE_IDX] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&sync_config_wifi_ssid_characteristic_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(char) * 31, 0, NULL}},
		[SYNC_CONFIG_WIFI_PASSWORD_CHARACTERISTIC_IDX] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&characteristic_declaration_uuid, ESP_GATT_PERM_READ, characteristic_declaration_size, characteristic_declaration_size, &characteristic_prop_read_write}},
		[SYNC_CONFIG_WIFI_PASSWORD_VALUE_IDX] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&sync_config_wifi_password_characteristic_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(char) * 63, 0, NULL}},
		[SYNC_CONFIG_MQTT_BROKER_URL_CHARACTERISTIC_IDX] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&characteristic_declaration_uuid, ESP_GATT_PERM_READ, characteristic_declaration_size, characteristic_declaration_size, &characteristic_prop_read_write}},
		[SYNC_CONFIG_MQTT_BROKER_URL_VALUE_IDX] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&sync_config_mqtt_broker_url_characteristic_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(char) * 255, 0, NULL}},

		// Characteristic user description (user-readable name) descriptor
		// [XXXXX] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&characteristic_description_uuid, ESP_GATT_PERM_READ, sizeof(characteristic_name), sizeof(characteristic_name) - 1, characteristic_name}},
};

static uint16_t config_service_handle_table[CONFIG_SERVICE_IDX_MAX];

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
	switch (event) {
		case ESP_GATTS_REG_EVT: {
			ESP_LOGI(TAG_GATTS_PROFILE, "[REGISTER_APP_EVT]: Status %d, app_id %d", param->reg.status, param->reg.app_id);
			esp_ble_gap_set_device_name(DEVICE_NAME);

			esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
			if (ret) {
				ESP_LOGE(TAG_GATTS_PROFILE, "[REGISTER_APP_EVT]: Config adv data failed, error code = %x", ret);
			}

			ret = esp_ble_gap_config_adv_data(&adv_data_scan_rsp);
			if (ret) {
				ESP_LOGE(TAG_GATTS_PROFILE, "[REGISTER_APP_EVT]: Config adv data scan rsp failed, error code = %x", ret);
			}

			esp_ble_gatts_create_attr_tab(gatts_attr_db, gatts_if, CONFIG_SERVICE_IDX_MAX, 0x00);
			break;
		}

		case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
			ESP_LOGD(TAG_GATTS_PROFILE, "[ESP_GATTS_CREAT_ATTR_TAB_EVT]: Number handle %x", param->add_attr_tab.num_handle);

			if (param->add_attr_tab.status != ESP_GATT_OK) {
				ESP_LOGE(TAG_GATTS_PROFILE, "[ESP_GATTS_CREAT_ATTR_TAB_EVT]: Create attribute table failed, error code=0x%x", param->add_attr_tab.status);
			} else if (param->add_attr_tab.num_handle != CONFIG_SERVICE_IDX_MAX) {
				ESP_LOGE(TAG_GATTS_PROFILE, "[ESP_GATTS_CREAT_ATTR_TAB_EVT]: Create attribute table abnormally, num_handle (%d) doesn't equal to CONFIG_SERVICE_IDX_MAX(%d)",
						 param->add_attr_tab.num_handle, CONFIG_SERVICE_IDX_MAX);
			} else {
				memcpy(config_service_handle_table, param->add_attr_tab.handles, sizeof(config_service_handle_table));
				esp_ble_gatts_start_service(config_service_handle_table[CONFIG_SERVICE_DECLARATION_IDX]);
			}

			break;
		}

		case ESP_GATTS_START_EVT:
			ESP_LOGI(TAG_GATTS_PROFILE, "[ESP_GATTS_START_EVT]: Service started");
			bt_led_state = LED_BLINK_SLOW;
			break;

		case ESP_GATTS_STOP_EVT:
			ESP_LOGI(TAG_GATTS_PROFILE, "[ESP_GATTS_STOP_EVT]: Service stopped");
			bt_led_state = LED_OFF;
			break;

		case ESP_GATTS_CONNECT_EVT: {
			const uint8_t *bda = param->connect.remote_bda;
			ESP_LOGI(TAG_GATTS_PROFILE, "[ESP_GATTS_CONNECT_EVT]: Client %02x:%02x:%02x:%02x:%02x:%02x", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
			esp_ble_gap_stop_advertising();
			bt_led_state = LED_ON;
			break;
		}

		case ESP_GATTS_DISCONNECT_EVT:
			ESP_LOGI(TAG_GATTS_PROFILE, "[ESP_GATTS_DISCONNECT_EVT]: Restarting advertising");
			esp_ble_gap_start_advertising(&adv_params);
			bt_led_state = LED_BLINK_SLOW;
			break;

		case ESP_GATTS_READ_EVT: {
			uint16_t conn_id = param->read.conn_id;
			uint16_t trans_id = param->read.trans_id;
			uint16_t handle = param->read.handle;
			uint16_t offset = param->read.offset;

			ESP_LOGI(TAG_GATTS_PROFILE, "[ESP_GATTS_READ_EVT]: conn %d, trans %d, handle %d, offset %d",
					 conn_id, trans_id, handle, offset);

			handle_mapping_t mappings[] = {
				{config_service_handle_table[GENERAL_CONFIG_MEASUREMENT_INTERVAL_VALUE_IDX], NVS_KEY_GENERAL_MEASUREMENT_INTERVAL, TYPE_U16},
				{config_service_handle_table[SENSORS_CONFIG_DHT22_MEASUREMENT_BULK_SIZE_VALUE_IDX], NVS_KEY_SENSORS_DHT22_MEASUREMENT_BULK_SIZE, TYPE_U16},
				{config_service_handle_table[SENSORS_CONFIG_DHT22_MEASUREMENT_BULK_SLEEP_VALUE_IDX], NVS_KEY_SENSORS_DHT22_MEASUREMENT_BULK_SLEEP, TYPE_U16},
				{config_service_handle_table[SENSORS_CONFIG_SDS011_WARM_UP_VALUE_IDX], NVS_KEY_SENSORS_SDS011_WARM_UP, TYPE_U16},
				{config_service_handle_table[SENSORS_CONFIG_SDS011_MEASUREMENT_BULK_SIZE_VALUE_IDX], NVS_KEY_SENSORS_SDS011_MEASUREMENT_BULK_SIZE, TYPE_U16},
				{config_service_handle_table[SENSORS_CONFIG_SDS011_MEASUREMENT_BULK_SLEEP_VALUE_IDX], NVS_KEY_SENSORS_SDS011_MEASUREMENT_BULK_SLEEP, TYPE_U16},
				{config_service_handle_table[SYNC_CONFIG_WIFI_SSID_VALUE_IDX], NVS_KEY_SYNC_WIFI_SSID, TYPE_STR},
				{config_service_handle_table[SYNC_CONFIG_WIFI_PASSWORD_VALUE_IDX], NVS_KEY_SYNC_WIFI_PASSWORD, TYPE_STR},
				{config_service_handle_table[SYNC_CONFIG_MQTT_BROKER_URL_VALUE_IDX], NVS_KEY_SYNC_MQTT_BROKER_URL, TYPE_STR},
			};

			for (size_t i = 0; i < sizeof(mappings) / sizeof(handle_mapping_t); i++) {
				handle_mapping_t *mapping = &mappings[i];
				if (handle != mapping->handle) continue;

				esp_gatt_rsp_t rsp;
				memset(&rsp, 0, sizeof(rsp));
				rsp.attr_value.handle = handle;

				uint8_t *data = NULL;
				size_t len = 0;

				nvs_handle_t nvs_handle;
				esp_err_t ret = nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE, NVS_READONLY, &nvs_handle);

				if (ret != ESP_OK) {
					ESP_LOGE(TAG_GATTS_PROFILE, "Error opening NVS handle: %s", esp_err_to_name(ret));
					esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_INTERNAL_ERROR, NULL);
					return;
				}

				switch (mapping->type) {
					case TYPE_U16: {
						uint16_t *value = malloc(sizeof(uint16_t));
						ret = nvs_get_u16(nvs_handle, mapping->key, value);

						if (ret == ESP_ERR_NVS_NOT_FOUND) {
							free(value);

							data = NULL;
							len = 0;

							goto send_response;
						} else if (ret != ESP_OK) {
							ESP_LOGE(TAG_GATTS_PROFILE, "Failed to read from NVS: %s", esp_err_to_name(ret));
							esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_INTERNAL_ERROR, NULL);
							return;
						}

						data = (uint8_t *)value;
						len = sizeof(uint16_t);

						break;
					}

					case TYPE_STR: {
						size_t required_size = 0;
						ret = nvs_get_str(nvs_handle, mapping->key, NULL, &required_size);

						if (ret == ESP_ERR_NVS_NOT_FOUND) {
							data = NULL;
							len = 0;

							goto send_response;

							break;
						} else if (ret != ESP_OK) {
							ESP_LOGE(TAG_GATTS_PROFILE, "Failed to read from NVS: %s", esp_err_to_name(ret));
							esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_INTERNAL_ERROR, NULL);
							return;
						}

						char *value = malloc(required_size);
						if (value == NULL) {
							ESP_LOGE(TAG_GATTS_PROFILE, "Failed to allocate memory");
							esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_INTERNAL_ERROR, NULL);
							return;
						};

						ret = nvs_get_str(nvs_handle, mapping->key, value, &required_size);
						if (ret != ESP_OK) {
							ESP_LOGE(TAG_GATTS_PROFILE, "Failed to read from NVS: %s", esp_err_to_name(ret));
							esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_INTERNAL_ERROR, NULL);
							return;
						}

						data = (uint8_t *)value;
						len = required_size - 1;

						break;
					}
				}

			send_response:
				nvs_close(nvs_handle);

				if (offset < len) {
					len -= offset;
					memcpy(rsp.attr_value.value, data + offset, len);
				} else
					len = 0;

				rsp.attr_value.len = len;
				esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_OK, &rsp);
				free(data);

				return;
			}

			break;
		}

		case ESP_GATTS_WRITE_EVT: {
			uint16_t conn_id = param->write.conn_id;
			uint16_t trans_id = param->write.trans_id;
			uint16_t handle = param->write.handle;
			uint16_t len = param->write.len;
			const uint8_t *data = param->write.value;

			ESP_LOGI(TAG_GATTS_PROFILE, "[ESP_GATTS_WRITE_EVT]: Length %d", len);
			ESP_LOG_BUFFER_HEXDUMP(TAG_GATTS_PROFILE, data, len, ESP_LOG_INFO);

			esp_err_t ret;

			if (param->write.is_prep) {
				esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_WRITE_NOT_PERMIT, NULL);
				return;
			}

			handle_mapping_t mappings[] = {
				{config_service_handle_table[GENERAL_CONFIG_MEASUREMENT_INTERVAL_VALUE_IDX], NVS_KEY_GENERAL_MEASUREMENT_INTERVAL, TYPE_U16},
				{config_service_handle_table[SENSORS_CONFIG_DHT22_MEASUREMENT_BULK_SIZE_VALUE_IDX], NVS_KEY_SENSORS_DHT22_MEASUREMENT_BULK_SIZE, TYPE_U16},
				{config_service_handle_table[SENSORS_CONFIG_DHT22_MEASUREMENT_BULK_SLEEP_VALUE_IDX], NVS_KEY_SENSORS_DHT22_MEASUREMENT_BULK_SLEEP, TYPE_U16},
				{config_service_handle_table[SENSORS_CONFIG_SDS011_WARM_UP_VALUE_IDX], NVS_KEY_SENSORS_SDS011_WARM_UP, TYPE_U16},
				{config_service_handle_table[SENSORS_CONFIG_SDS011_MEASUREMENT_BULK_SIZE_VALUE_IDX], NVS_KEY_SENSORS_SDS011_MEASUREMENT_BULK_SIZE, TYPE_U16},
				{config_service_handle_table[SENSORS_CONFIG_SDS011_MEASUREMENT_BULK_SLEEP_VALUE_IDX], NVS_KEY_SENSORS_SDS011_MEASUREMENT_BULK_SLEEP, TYPE_U16},
				{config_service_handle_table[SYNC_CONFIG_WIFI_SSID_VALUE_IDX], NVS_KEY_SYNC_WIFI_SSID, TYPE_STR},
				{config_service_handle_table[SYNC_CONFIG_WIFI_PASSWORD_VALUE_IDX], NVS_KEY_SYNC_WIFI_PASSWORD, TYPE_STR},
				{config_service_handle_table[SYNC_CONFIG_MQTT_BROKER_URL_VALUE_IDX], NVS_KEY_SYNC_MQTT_BROKER_URL, TYPE_STR},
			};

			for (size_t i = 0; i < sizeof(mappings) / sizeof(mappings[0]); i++) {
				handle_mapping_t *mapping = &mappings[i];
				if (handle != mapping->handle) continue;

				nvs_handle_t nvs_handle;
				ret = nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);

				if (ret != ESP_OK) {
					ESP_LOGE(TAG_GATTS_PROFILE, "Error opening NVS handle: %s", esp_err_to_name(ret));
					esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_INTERNAL_ERROR, NULL);
					return;
				}

				switch (mapping->type) {
					case TYPE_U16: {
						if (len != sizeof(uint16_t)) {
							ESP_LOGE(TAG_GATTS_PROFILE, "Invalid data length for u16 value: %d", len);
							esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_WRITE_NOT_PERMIT, NULL);
							return;
						}

						uint16_t value;
						memcpy(&value, data, sizeof(uint16_t));
						ret = nvs_set_u16(nvs_handle, mapping->key, value);
						ret |= nvs_commit(nvs_handle);

						if (ret != ESP_OK) {
							ESP_LOGE(TAG_GATTS_PROFILE, "Failed to write to NVS: %s", esp_err_to_name(ret));
							esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_INTERNAL_ERROR, NULL);
							return;
						}

						esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_OK, NULL);
						return;
					}

					case TYPE_STR: {
						char null_terminated_data[256];

						if (len >= sizeof(null_terminated_data)) {
							ESP_LOGE(TAG_GATTS_PROFILE, "String data too long");
							esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_WRITE_NOT_PERMIT, NULL);
							return;
						}

						memcpy(null_terminated_data, data, len);
						null_terminated_data[len] = '\0';

						ret = nvs_set_str(nvs_handle, mapping->key, null_terminated_data);
						ret |= nvs_commit(nvs_handle);

						if (ret != ESP_OK) {
							ESP_LOGE(TAG_GATTS_PROFILE, "Failed to write to NVS: %s", esp_err_to_name(ret));
							esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_INTERNAL_ERROR, NULL);
							return;
						}

						esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_OK, NULL);
						return;
					}
				}
			}

			ESP_LOGE(TAG_GATTS_PROFILE, "Unhandled write event for handle %d", handle);
			esp_ble_gatts_send_response(gatts_if, conn_id, trans_id, ESP_GATT_WRITE_NOT_PERMIT, NULL);
			break;
		}

		case ESP_GATTS_EXEC_WRITE_EVT:
			break;

		case ESP_GATTS_MTU_EVT: {
			uint16_t mtu = param->mtu.mtu;
			ESP_LOGI(TAG_GATTS_PROFILE, "[ESP_GATTS_MTU_EVT]: MTU set to %d", mtu);
			break;
		}

		default:
			ESP_LOGW(TAG_GATTS_PROFILE, "Unhandled event %d", event);
			break;
	}
}

// Global GATT setup

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
	ESP_LOGD(TAG_GATTS, "EVT %d", event);

	/* If event is register event, store the gatts_if for each profile */
	if (event == ESP_GATTS_REG_EVT) {
		if (param->reg.status == ESP_GATT_OK) {
			profile_table[param->reg.app_id].gatts_if = gatts_if;
		} else {
			ESP_LOGI(TAG_GATTS, "Reg app failed, app_id %04x, status %d",
					 param->reg.app_id,
					 param->reg.status);

			return;
		}
	}

	for (int i = 0; i < PROFILE_NUM; i++) {
		if (gatts_if == ESP_GATT_IF_NONE || gatts_if == profile_table[i].gatts_if) {
			profile_table[i].gatts_cb(event, gatts_if, param);
		}
	}
}

void ble_config_gatt_server_start() {
	bt_led_state = LED_OFF;

	xTaskCreatePinnedToCore(
		led_task,
		"led",
		configMINIMAL_STACK_SIZE * 8,
		NULL,
		10,
		NULL,
		APP_CPU_NUM);

	esp_err_t ret;

	esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
	ret = esp_bt_controller_init(&bt_cfg);
	if (ret) {
		ESP_LOGE(TAG_MAIN, "%s enable controller failed", __func__);
		return;
	}

	ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
	if (ret) {
		ESP_LOGE(TAG_MAIN, "%s enable controller failed", __func__);
		return;
	}

	ESP_LOGI(TAG_MAIN, "%s init bluetooth", __func__);

	ret = esp_bluedroid_init();
	if (ret) {
		ESP_LOGE(TAG_MAIN, "%s init bluetooth failed", __func__);
		return;
	}
	ret = esp_bluedroid_enable();
	if (ret) {
		ESP_LOGE(TAG_MAIN, "%s enable bluetooth failed", __func__);
		return;
	}

	esp_ble_gatts_register_callback(gatts_event_handler);
	esp_ble_gap_register_callback(gap_event_handler);

	ret = esp_ble_gatts_app_register(PROFILE_APP_ID);
	if (ret) {
		ESP_LOGE(TAG_MAIN, "gatts app register error, error code = %x", ret);
		return;
	}

	esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(512);
	if (local_mtu_ret) {
		ESP_LOGE(TAG_MAIN, "set local  MTU failed, error code = %x", local_mtu_ret);
	}

	ESP_LOGI(TAG_MAIN, "Bluetooth initialized successfully");

	return;
}
