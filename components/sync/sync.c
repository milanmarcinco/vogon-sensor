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

#include "shared.h"
#include "sync.h"

#define MAC_LEN 18
#define TOPIC_LEN 100

#define MAX_CONCURRENT_MQTT_MESSAGES 1

static const char *TAG = "MODULE[SYNC]";

static EventGroupHandle_t connection_event_group;
static SemaphoreHandle_t mqtt_publish_mutex;

static const int WIFI_CONNECTED_BIT = BIT0;
static const int MQTT_CONNECTED_BIT = BIT1;

enum {
	AT_MOST_ONCE,
	AT_LEAST_ONCE,
	EXACTLY_ONCE
};

enum {
	NOT_RETAIN,
	RETAIN
};

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	if (event_base == WIFI_EVENT) {
		switch (event_id) {
			case WIFI_EVENT_STA_START:
				ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
				esp_wifi_connect();
				break;
			case WIFI_EVENT_STA_DISCONNECTED:
				ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
				xEventGroupClearBits(connection_event_group, WIFI_CONNECTED_BIT);
				break;
			default:
				break;
		}
	}
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	if (event_base == IP_EVENT) {
		switch (event_id) {
			case IP_EVENT_STA_GOT_IP:
				ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");
				xEventGroupSetBits(connection_event_group, WIFI_CONNECTED_BIT);
				break;
			default:
				break;
		}
	}
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
	// esp_mqtt_event_handle_t event = event_data;
	// esp_mqtt_client_handle_t client = event->client;

	switch (event_id) {
		case MQTT_EVENT_CONNECTED:
			xEventGroupSetBits(connection_event_group, MQTT_CONNECTED_BIT);
			ESP_LOGD(TAG, "MQTT_EVENT_CONNECTED");
			break;
		case MQTT_EVENT_DISCONNECTED:
			xEventGroupClearBits(connection_event_group, MQTT_CONNECTED_BIT);
			ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
			break;
		case MQTT_EVENT_PUBLISHED: // Fired only for QoS>0
			ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED");
			xSemaphoreGive(mqtt_publish_mutex);
			break;
		case MQTT_EVENT_DELETED:
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
		BaseType_t ret = xSemaphoreTake(mqtt_publish_mutex, pdMS_TO_TICKS(60 * 1000));

		if (ret == pdFALSE) {
			ESP_LOGE(TAG, "Failed to send MQTT message [sensor=%d, type=%d, value=%.2f] within timeout", sensor, type, value);
			cJSON_Delete(root);
			free(message);
			return;
		}

		esp_mqtt_client_publish(*client, topic, message, 0, AT_LEAST_ONCE, NOT_RETAIN);
		free(message);
	}

	cJSON_Delete(root);
}

void mqtt_sync() {
	connection_event_group = xEventGroupCreate();
	mqtt_publish_mutex = xSemaphoreCreateCounting(MAX_CONCURRENT_MQTT_MESSAGES, MAX_CONCURRENT_MQTT_MESSAGES);

	// Init TCP/IP stack
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_t *netif = esp_netif_create_default_wifi_sta();
	esp_netif_set_hostname(netif, DEVICE_NAME);
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t wifi_handler_event_instance;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
														ESP_EVENT_ANY_ID,
														&wifi_event_handler,
														NULL,
														&wifi_handler_event_instance));

	esp_event_handler_instance_t got_ip_event_instance;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
														IP_EVENT_STA_GOT_IP,
														&ip_event_handler,
														NULL,
														&got_ip_event_instance));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = CONFIG_SYNC_WIFI_SSID,
			.password = CONFIG_SYNC_WIFI_PASSWORD,
		},
	};

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	EventBits_t bits = xEventGroupWaitBits(connection_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(60 * 1000));

	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "Connected to Wi-Fi: %s", CONFIG_SYNC_WIFI_SSID);
	} else {
		ESP_LOGE(TAG, "Failed to connect to Wi-Fi: %s", CONFIG_SYNC_WIFI_SSID);
		return;
	}

	esp_mqtt_client_config_t mqtt_cfg = {
		.broker.address.uri = CONFIG_SYNC_MQTT_BROKER,
	};

	esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
	ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client));
	ESP_ERROR_CHECK(esp_mqtt_client_start(client));

	bits = xEventGroupWaitBits(connection_event_group, MQTT_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(60 * 1000));

	if (bits & MQTT_CONNECTED_BIT) {
		ESP_LOGI(TAG, "Connected to MQTT broker: %s", CONFIG_SYNC_MQTT_BROKER);
	} else {
		ESP_LOGE(TAG, "Failed to connect to MQTT broker: %s", CONFIG_SYNC_MQTT_BROKER);
		return;
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
	ESP_ERROR_CHECK(esp_wifi_stop());
}
