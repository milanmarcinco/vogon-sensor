#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

extern EventGroupHandle_t wifi_connection_event_group;
extern const int WIFI_CONNECTED_BIT;

esp_err_t init_tcp_ip();
esp_err_t wifi_connect();
esp_err_t wifi_disconnect();
