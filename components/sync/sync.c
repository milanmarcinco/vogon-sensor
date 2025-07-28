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

static EventGroupHandle_t connection_event_group;

const int WIFI_CONNECTED_BIT = BIT0;
const int MQTT_CONNECTED_BIT = BIT1;

static const char *TAG = "MODULE[SYNC]";

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		xEventGroupClearBits(connection_event_group, WIFI_CONNECTED_BIT);
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");
		xEventGroupSetBits(connection_event_group, WIFI_CONNECTED_BIT);
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
	case MQTT_EVENT_PUBLISHED:
		ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED");
		break;
	default:
		break;
	}
}

void get_mac_address_string(char *mac_str) {
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
		esp_mqtt_client_publish(*client, topic, message, 0, 1, 0);
		free(message);
	}

	cJSON_Delete(root);
}

void sync() {
	connection_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = CONFIG_SYNC_WIFI_SSID,
			.password = CONFIG_SYNC_WIFI_PASSWORD,
		},
	};

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

	esp_mqtt_client_config_t mqtt_cfg = {
		.broker.address.uri = CONFIG_SYNC_MQTT_BROKER,
	};
	esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
	ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client));

	ESP_ERROR_CHECK(esp_wifi_start());
	xEventGroupWaitBits(connection_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);

	esp_mqtt_client_start(client);
	xEventGroupWaitBits(connection_event_group, MQTT_CONNECTED_BIT, false, true, portMAX_DELAY);

	ESP_LOGI(TAG, "Sending temperature...");
	publish(&client, 0x01, 0x01, shared_data.temperature);

	ESP_LOGI(TAG, "Sending humidity...");
	publish(&client, 0x01, 0x02, shared_data.humidity);

	ESP_LOGI(TAG, "Sending PM2.5...");
	publish(&client, 0x02, 0x01, shared_data.pm25);

	ESP_LOGI(TAG, "Sending PM10...");
	publish(&client, 0x02, 0x02, shared_data.pm10);

	ESP_LOGI(TAG, "Data sent successfully!");

	esp_mqtt_client_stop(client);
	ESP_ERROR_CHECK(esp_wifi_stop());
}
