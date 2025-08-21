#include "stdio.h"

#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"

#include "ble_config.h"
#include "helpers.h"
#include "sensors.h"
#include "shared.h"
#include "sync.h"

static const char *TAG = "MODULE[MAIN]";

// Number of concurrent tasks running measurements to wait for before MQTT sync
#define TASK_COUNT 2

#define WAKE_GPIO GPIO_NUM_0

void app_main(void) {
	ESP_LOGI(TAG, "Booting Vogon...");
	esp_err_t ret;

	// NVS flash required by WiFi, MQTT and BLE
	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ESP_ERROR_CHECK(nvs_flash_init());
	}

	// Also flash app-specific NVS partition
	ret = nvs_flash_init_partition(NVS_PARTITION);
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase_partition(NVS_PARTITION));
		ESP_ERROR_CHECK(nvs_flash_init_partition(NVS_PARTITION));
	}

	// Detect wakeup cause and choose device mode
	// - boot button press - bluetooth configuration mode
	// - otherwise normal operation
	esp_reset_reason_t reset_reason = esp_reset_reason();
	esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();

	rtc_gpio_hold_dis(WAKE_GPIO);	  // Allow GPIO pin reconfiguration
	rtc_gpio_pullup_dis(WAKE_GPIO);	  // Clear any previous pull-ups
	rtc_gpio_pulldown_dis(WAKE_GPIO); // Clear any previous pull-downs

	ESP_LOGI(TAG, "Reset reason: %d", reset_reason);
	ESP_LOGI(TAG, "Wakeup cause: %d", wakeup_cause);

	if (wakeup_cause == ESP_SLEEP_WAKEUP_EXT0) {
		ESP_LOGI(TAG, "Woke up from BOOT button press - starting Bluetooth configuration mode");
		ble_config_gatt_server_start();
		return;
	}

	// Initialize semaphore to number of concurrent tasks
	sync_mutex = xSemaphoreCreateCounting(TASK_COUNT, 0);

	// Load configuration from NVS
	ESP_ERROR_CHECK(load_shared_config());

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

	// Wait for concurrent tasks to finish
	for (int i = 0; i < TASK_COUNT; i++) {
		if (xSemaphoreTake(sync_mutex, portMAX_DELAY) != pdTRUE) {
			ESP_LOGE(TAG, "Failed to take semaphore!");
		}
	}

	ESP_LOGI(TAG, "Sending data to MQTT broker...");
	mqtt_sync();

	vTaskDelay(pdMS_TO_TICKS(10000));

	ESP_LOGI(TAG, "Going to sleep for 10 minutes...");

	rtc_gpio_pullup_dis(WAKE_GPIO);	 // Make sure pull-up is off
	rtc_gpio_pulldown_en(WAKE_GPIO); // Have GPIO pin default to LOW
	esp_sleep_enable_ext0_wakeup(WAKE_GPIO, 0);
	rtc_gpio_hold_en(WAKE_GPIO); // Freeze GPIO configuration
	esp_deep_sleep(shared_config.GENERAL_MEASUREMENT_INTERVAL * 60 * 1000000);
}
