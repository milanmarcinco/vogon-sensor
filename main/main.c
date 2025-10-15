#include "stdio.h"

#include "driver/gpio.h"
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
#include "wifi.h"

static const char *TAG = "MODULE[main]";

// Number of concurrent tasks running measurements to wait for before MQTT sync
#define TASK_COUNT 2

#define BLE_CONFIG_TRIGGER_GPIO GPIO_NUM_0

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

	rtc_gpio_hold_dis(BLE_CONFIG_TRIGGER_GPIO);		// Allow GPIO pin reconfiguration
	rtc_gpio_pullup_dis(BLE_CONFIG_TRIGGER_GPIO);	// Clear any previous pull-ups
	rtc_gpio_pulldown_dis(BLE_CONFIG_TRIGGER_GPIO); // Clear any previous pull-downs

	ESP_LOGI(TAG, "Reset reason: %d", reset_reason);
	ESP_LOGI(TAG, "Wakeup cause: %d", wakeup_cause);

	if (wakeup_cause == ESP_SLEEP_WAKEUP_EXT0) {
		ESP_LOGI(TAG, "Woke up from BOOT button press - starting Bluetooth configuration mode");
		ble_config_gatt_server_start();
		return;
	}

	// Load configuration from NVS
	ret = load_shared_config();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Vogon not yet configured. Entering Bluetooth configuration mode.");
		ble_config_gatt_server_start();
		return;
	}

	// Start BLE configuration server on EN button press (START_BLE_CONFIG_GPIO)
	gpio_evt_queue = xQueueCreate(1, sizeof(int));

	xTaskCreatePinnedToCore(
		ble_config_gat_server_trigger_task,
		"gpio_task",
		configMINIMAL_STACK_SIZE * 8,
		NULL,
		10,
		NULL,
		APP_CPU_NUM);

	gpio_config_t io_conf = {
		.intr_type = GPIO_INTR_POSEDGE,
		.mode = GPIO_MODE_INPUT,
		.pin_bit_mask = (1ULL << BLE_CONFIG_TRIGGER_GPIO),
		.pull_down_en = GPIO_PULLDOWN_ENABLE};

	ESP_ERROR_CHECK(gpio_config(&io_conf));
	ESP_ERROR_CHECK(gpio_install_isr_service(0));
	ESP_ERROR_CHECK(gpio_isr_handler_add(
		BLE_CONFIG_TRIGGER_GPIO,
		gpio_isr_handler,
		(void *)BLE_CONFIG_TRIGGER_GPIO));

	// Initialize sync semaphore to number of concurrent tasks
	// sync_mutex = xSemaphoreCreateCounting(TASK_COUNT, 0);

	// Initialize shared data
	shared_data.temperature = 0;
	shared_data.humidity = 0,
	shared_data.pm25 = 0;
	shared_data.pm10 = 0;

	// ESP_LOGI(TAG, "Warming up...");
	// vTaskDelay(pdMS_TO_TICKS(5 * 1000));

	// ESP_LOGI(TAG, "Starting DHT22 task!");
	// xTaskCreatePinnedToCore(
	// 	dht22_task,
	// 	"dht22",
	// 	configMINIMAL_STACK_SIZE * 8,
	// 	NULL,
	// 	10,
	// 	NULL,
	// 	APP_CPU_NUM);

	// ESP_LOGI(TAG, "Starting SDS011 task!");
	// xTaskCreatePinnedToCore(
	// 	sds011_task,
	// 	"sds011",
	// 	configMINIMAL_STACK_SIZE * 8,
	// 	NULL,
	// 	10,
	// 	NULL,
	// 	APP_CPU_NUM);

	// ESP_LOGI(TAG, "All tasks are pinned!");

	// // Wait for concurrent tasks to finish
	// for (int i = 0; i < TASK_COUNT; i++) {
	// 	if (xSemaphoreTake(sync_mutex, portMAX_DELAY) != pdTRUE) {
	// 		ESP_LOGE(TAG, "Failed to take semaphore!");
	// 	}
	// }

	init_tcp_ip();
	ret = wifi_connect();

	if (ret == ESP_OK) {
		mqtt_sync();
		wifi_disconnect();
	}

	uint64_t sleep_time = shared_config.GENERAL_MEASUREMENT_INTERVAL * 60 * 1000000;

	ESP_LOGI(TAG, "Going to sleep for %d minutes...", shared_config.GENERAL_MEASUREMENT_INTERVAL);
	rtc_gpio_pullup_dis(BLE_CONFIG_TRIGGER_GPIO);  // Make sure pull-up is off
	rtc_gpio_pulldown_en(BLE_CONFIG_TRIGGER_GPIO); // Have GPIO pin default to LOW
	esp_sleep_enable_ext0_wakeup(BLE_CONFIG_TRIGGER_GPIO, 0);
	rtc_gpio_hold_en(BLE_CONFIG_TRIGGER_GPIO); // Freeze GPIO configuration
	esp_deep_sleep(sleep_time);
}
