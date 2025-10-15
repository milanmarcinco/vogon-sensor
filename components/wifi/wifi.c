#include "esp_bit_defs.h"
#include "esp_eap_client.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "helpers.h"
#include "shared.h"

#include "wifi.h"

#define WIFI_CONNECTION_TIMEOUT 10 * 1000

static const char *TAG = "MODULE[wifi]";

EventGroupHandle_t wifi_connection_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	if (event_base == WIFI_EVENT) {
		switch (event_id) {
			case WIFI_EVENT_STA_START:
				ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
				esp_wifi_connect();
				break;

			case WIFI_EVENT_STA_DISCONNECTED:
				ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
				xEventGroupClearBits(wifi_connection_event_group, WIFI_CONNECTED_BIT);
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
				xEventGroupSetBits(wifi_connection_event_group, WIFI_CONNECTED_BIT);
				break;

			default:
				break;
		}
	}
}

esp_err_t init_tcp_ip() {
	ESP_LOGI(TAG, "Init TCP/IP");
	RETURN_ON_ERROR(esp_netif_init());
	RETURN_ON_ERROR(esp_event_loop_create_default());
	return ESP_OK;
}

esp_err_t wifi_connect() {
	wifi_connection_event_group = xEventGroupCreate();

	esp_netif_t *netif = esp_netif_create_default_wifi_sta();
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_netif_set_hostname(netif, DEVICE_NAME);
	RETURN_ON_ERROR(esp_wifi_init(&cfg));

	esp_event_handler_instance_t wifi_handler_event_instance;
	RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
														ESP_EVENT_ANY_ID,
														&wifi_event_handler,
														NULL,
														&wifi_handler_event_instance));

	esp_event_handler_instance_t got_ip_event_instance;
	RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
														IP_EVENT_STA_GOT_IP,
														&ip_event_handler,
														NULL,
														&got_ip_event_instance));

	wifi_config_t wifi_config = {};

	strncpy((char *)wifi_config.sta.ssid,
			shared_config.SYNC_WIFI_SSID,
			sizeof(wifi_config.sta.ssid));

	switch (shared_config.SYNC_WIFI_PROTOCOL) {
		case WIFI_AUTH_OPEN:
			break;

		case WIFI_AUTH_WPA2_PSK:
			strncpy((char *)wifi_config.sta.password,
					shared_config.SYNC_WIFI_PASSWORD,
					sizeof(wifi_config.sta.password));

			break;

		case WIFI_AUTH_WPA2_ENTERPRISE:
			RETURN_ON_ERROR(esp_eap_client_set_username(
				(uint8_t *)shared_config.SYNC_WIFI_USERNAME,
				strlen(shared_config.SYNC_WIFI_USERNAME)));

			RETURN_ON_ERROR(esp_eap_client_set_password(
				(uint8_t *)shared_config.SYNC_WIFI_PASSWORD,
				strlen(shared_config.SYNC_WIFI_PASSWORD)));

			RETURN_ON_ERROR(esp_eap_client_set_eap_methods(ESP_EAP_TYPE_PEAP | ESP_EAP_TYPE_TTLS));
			RETURN_ON_ERROR(esp_wifi_sta_enterprise_enable());

			break;

		default:
			ESP_LOGE(TAG, "Unsupported Wi-Fi protocol: %d", shared_config.SYNC_WIFI_PROTOCOL);
			return ESP_FAIL;
	}

	RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA));
	RETURN_ON_ERROR(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
	RETURN_ON_ERROR(esp_wifi_start());

	EventBits_t bits = xEventGroupWaitBits(
		wifi_connection_event_group,
		WIFI_CONNECTED_BIT,
		pdFALSE, pdTRUE,
		pdMS_TO_TICKS(WIFI_CONNECTION_TIMEOUT));

	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "Connected to Wi-Fi: %s", shared_config.SYNC_WIFI_SSID);
		return ESP_OK;
	} else {
		ESP_LOGE(TAG, "Failed to connect to Wi-Fi: %s", shared_config.SYNC_WIFI_SSID);
		return ESP_FAIL;
	}
}

esp_err_t wifi_disconnect() {
	return esp_wifi_stop();
}
