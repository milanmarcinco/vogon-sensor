#include "esp_wifi.h"
#include "stdarg.h"
#include "stdint.h"
#include "stdio.h"

#include "helpers.h"

static void get_mac_address_string(char *mac_str) {
	uint8_t mac[6];
	ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_STA, mac));
	snprintf(mac_str, MAC_LEN, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

char *dynamic_format(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	if (len < 0)
		return NULL;

	char *buffer = malloc(len + 1);
	if (!buffer)
		return NULL;

	va_start(args, fmt);
	vsnprintf(buffer, len + 1, fmt, args);
	va_end(args);

	return buffer;
}
