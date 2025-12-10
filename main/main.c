#include "stdio.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "helpers.h"
#include "sensors.h"
#include "shared.h"

#define PRINT_FREQUENCY 3

static const char *TAG = "MODULE[main]";

void app_main(void) {
	ESP_LOGI(TAG, "Booting Vogon...");

	// Initialize shared data
	shared_data.temperature = 0;
	shared_data.humidity = 0,
	shared_data.pm25 = 0;
	shared_data.pm10 = 0;

	ESP_LOGI(TAG, "Warming up...");
	vTaskDelay(pdMS_TO_TICKS(5 * 1000));

	ESP_LOGI(TAG, "Starting DHT22 task!");
	xTaskCreatePinnedToCore(
		dht22_task,
		"dht22",
		configMINIMAL_STACK_SIZE * 8,
		NULL,
		10,
		NULL,
		APP_CPU_NUM);

	ESP_LOGI(TAG, "Starting SDS011 task!");
	xTaskCreatePinnedToCore(
		sds011_task,
		"sds011",
		configMINIMAL_STACK_SIZE * 8,
		NULL,
		10,
		NULL,
		APP_CPU_NUM);

	ESP_LOGI(TAG, "All tasks are pinned!");

	while (true) {
		ESP_LOGI(TAG,
				 "Current measurements:\n\ttemperature=%.2fC\n\thumidity=%.2f%%\n\tPM2.5=%d\n\tPM10=%d",
				 shared_data.temperature,
				 shared_data.humidity,
				 shared_data.pm25,
				 shared_data.pm10);

		vTaskDelay(pdMS_TO_TICKS(PRINT_FREQUENCY * 1000));
	}
}
