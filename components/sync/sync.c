#include "stdint.h"

#include "cJSON.h"
#include "esp_bit_defs.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#include "helpers.h"
#include "shared.h"

#include "sync.h"

#define MAC_LEN 18
#define TOPIC_LEN 100

#define MQTT_CONNECTION_TIMEOUT 60 * 1000
#define MQTT_CONCURRENT_MESSAGES 4
#define MQTT_MESSAGE_TIMEOUT_MS 10 * 1000
#define MQTT_MESSAGE_WAIT_TIME_MS 15 * 1000

static const char *TAG = "MODULE[sync]";

static EventGroupHandle_t mqtt_connection_event_group;
static SemaphoreHandle_t mqtt_publish_mutex;

static const int MQTT_CONNECTED_BIT = BIT0;

#define LEN_AUTO 0

enum {
	AT_MOST_ONCE,
	AT_LEAST_ONCE,
	EXACTLY_ONCE
};

enum {
	NOT_RETAIN,
	RETAIN
};

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
	// esp_mqtt_event_handle_t event = event_data;
	// esp_mqtt_client_handle_t client = event->client;

	switch (event_id) {
		case MQTT_EVENT_CONNECTED:
			xEventGroupSetBits(mqtt_connection_event_group, MQTT_CONNECTED_BIT);
			ESP_LOGD(TAG, "MQTT_EVENT_CONNECTED");
			break;
		case MQTT_EVENT_DISCONNECTED:
			xEventGroupClearBits(mqtt_connection_event_group, MQTT_CONNECTED_BIT);
			ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
			break;
		case MQTT_EVENT_PUBLISHED:
			// Message acknowledged by broker
			// Fired only for QoS>0
			ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED");
			xSemaphoreGive(mqtt_publish_mutex);
			break;
		case MQTT_EVENT_DELETED:
			// Message deleted from outbox (not acknowledged by broker)
			// Fired only if message couldn't have been sent or acknowledged before expiring
			ESP_LOGD(TAG, "MQTT_EVENT_DELETED");
			xSemaphoreGive(mqtt_publish_mutex);
			break;
		default:
			break;
	}
}

static void get_mac_address_string(char *mac_str) {
	uint8_t mac[6];
	ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_STA, mac));
	snprintf(mac_str, MAC_LEN, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void publish(esp_mqtt_client_handle_t *client, const uint16_t sensor, const uint8_t type, const double value) {
	char mac_address[MAC_LEN];
	char topic[TOPIC_LEN];
	get_mac_address_string(mac_address);

	cJSON *root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "address", mac_address);
	cJSON_AddNumberToObject(root, "sensor", sensor);
	cJSON_AddNumberToObject(root, "parameter", type);
	cJSON_AddNumberToObject(root, "value", value);

	char *message = cJSON_Print(root);
	snprintf(topic, sizeof(topic), "vogonair/%s/raw", mac_address);

	if (message) {
		BaseType_t ret = xSemaphoreTake(mqtt_publish_mutex, pdMS_TO_TICKS(MQTT_MESSAGE_WAIT_TIME_MS));

		if (ret == pdFALSE) {
			ESP_LOGE(TAG, "Failed to send MQTT message [sensor=%d, type=%d, value=%.2f] within timeout", sensor, type, value);

			// cJSON_Delete(root);
			// free(message);
			// return;
		}

		ESP_LOGI(TAG, "Publishing to topic %s: %s", topic, message);
		esp_mqtt_client_publish(*client, topic, message, LEN_AUTO, AT_LEAST_ONCE, NOT_RETAIN);
		free(message);
	}

	cJSON_Delete(root);
}

esp_err_t mqtt_sync() {
	mqtt_publish_mutex = xSemaphoreCreateCounting(MQTT_CONCURRENT_MESSAGES, MQTT_CONCURRENT_MESSAGES);

	esp_mqtt_client_config_t mqtt_cfg = {
		.broker.address.uri = shared_config.SYNC_MQTT_BROKER_URL,
		.network.timeout_ms = MQTT_MESSAGE_TIMEOUT_MS};

	esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
	RETURN_ON_ERROR(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client));
	RETURN_ON_ERROR(esp_mqtt_client_start(client));

	EventBits_t bits = xEventGroupWaitBits(
		mqtt_connection_event_group,
		MQTT_CONNECTED_BIT,
		pdFALSE, pdTRUE,
		pdMS_TO_TICKS(MQTT_CONNECTION_TIMEOUT));

	if (bits & MQTT_CONNECTED_BIT) {
		ESP_LOGI(TAG, "Connected to MQTT broker: %s", shared_config.SYNC_MQTT_BROKER_URL);
	} else {
		ESP_LOGE(TAG, "Failed to connect to MQTT broker: %s", shared_config.SYNC_MQTT_BROKER_URL);
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "Syncing data...");
	publish(&client, 0x01, 0x01, shared_data.temperature);
	publish(&client, 0x01, 0x02, shared_data.humidity);
	publish(&client, 0x02, 0x01, shared_data.pm25);
	publish(&client, 0x02, 0x02, shared_data.pm10);

	// Wait for all messages to be published
	xSemaphoreTake(mqtt_publish_mutex, portMAX_DELAY);
	ESP_LOGI(TAG, "Data synced successfully!");

	esp_mqtt_client_stop(client);
	esp_mqtt_client_destroy(client);
	return ESP_OK;
}
