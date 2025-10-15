typedef enum {
	WIFI_OPEN,
	WIFI_WPA2_PERSONAL,
	WIFI_WPA2_ENTERPRISE,
} wifi_protocol_t;

extern EventGroupHandle_t wifi_connection_event_group;
extern const int WIFI_CONNECTED_BIT;

esp_err_t init_tcp_ip();
esp_err_t wifi_connect();
esp_err_t wifi_disconnect();
