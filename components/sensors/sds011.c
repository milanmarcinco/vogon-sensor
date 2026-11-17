#include "stdint.h"
#include "string.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "sensors.h"
#include "shared.h"

#define TXD_PIN (GPIO_NUM_17) // Use GPIO17 for TX
#define RXD_PIN (GPIO_NUM_16) // Use GPIO16 for RX
#define UART_BUFFER (1024)

static const char *TAG = "MODULE[sds011]";

static const uint8_t ACTIVE_MODE = 0x00;
static const uint8_t QUERY_MODE = 0x01;

static const uint8_t WORK_STATE = 0x01;
static const uint8_t SLEEP_STATE = 0x00;

#define RESPONSE_LEN 10
#define RESPONSE_HEAD 0xAA
#define RESPONSE_TAIL 0xAB
#define RESPONSE_CMD_REPLY 0xC5
#define RESPONSE_CMD_DATA 0xC0
#define RESPONSE_TIMEOUT_MS 1500

static esp_err_t sds011_send_command(const uint8_t payload[13], uint8_t *response, uint8_t expected_cmd) {
	int len;
	uint8_t command[] = {
		0xAA, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0xAB};

	memcpy(&command[2], payload, 13);

	// Checksum
	uint16_t checksum = 0;
	for (int i = 2; i <= 16; i++) {
		checksum += command[i];
	}
	command[17] = (uint8_t)(checksum & 0xFF);

	uart_flush_input(UART_NUM_2);

	uart_write_bytes(UART_NUM_2, (const char *)command, 19);
	ESP_ERROR_CHECK(uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(250)));

	TickType_t start = xTaskGetTickCount();
	while (xTaskGetTickCount() - start < pdMS_TO_TICKS(RESPONSE_TIMEOUT_MS)) {
		len = uart_read_bytes(UART_NUM_2, response, 1, pdMS_TO_TICKS(100));
		if (len != 1 || response[0] != RESPONSE_HEAD) continue;

		len = uart_read_bytes(UART_NUM_2, response + 1, RESPONSE_LEN - 1, pdMS_TO_TICKS(250));
		if (len != RESPONSE_LEN - 1) {
			ESP_LOGE(TAG, "Error reading response: %d", len + 1);
			return ESP_FAIL;
		}

		if (response[9] != RESPONSE_TAIL) continue;

		uint8_t response_checksum = 0;
		for (int i = 2; i <= 7; i++) {
			response_checksum += response[i];
		}

		if (response_checksum != response[8]) {
			ESP_LOGW(TAG, "Invalid response checksum: expected %02X, got %02X", response_checksum, response[8]);
			continue;
		}

		if (response[1] != expected_cmd) continue;

		return ESP_OK;
	}

	ESP_LOGE(TAG, "No valid response (cmd %02X) within timeout", expected_cmd);
	return ESP_FAIL;
}

static esp_err_t sds011_read_reporting_mode(uint8_t *buffer, uint8_t *mode) {
	const uint8_t payload[] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
							   0x00, 0x00, 0x00, 0x00, 0x00};

	if (sds011_send_command(payload, buffer, RESPONSE_CMD_REPLY) != ESP_OK) {
		return ESP_FAIL;
	}

	*mode = buffer[4];
	return ESP_OK;
}

static esp_err_t sds011_write_reporting_mode(uint8_t *buffer, uint8_t mode) {
	const uint8_t payload[] = {0x02, 0x01, mode, 0x00, 0x00, 0x00, 0x00, 0x00,
							   0x00, 0x00, 0x00, 0x00, 0x00};

	if (sds011_send_command(payload, buffer, RESPONSE_CMD_REPLY) != ESP_OK) {
		return ESP_FAIL;
	}

	uint8_t result = buffer[4];

	if (result != mode) {
		return ESP_FAIL;
	}

	return ESP_OK;
}

static esp_err_t sds011_query_data(uint8_t *buffer, uint16_t *pm25, uint16_t *pm10) {
	const uint8_t payload[] = {0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
							   0x00, 0x00, 0x00, 0x00, 0x00};

	if (sds011_send_command(payload, buffer, RESPONSE_CMD_DATA) != ESP_OK) {
		return ESP_FAIL;
	}

	uint16_t pm25_raw = (buffer[3] << 8) | buffer[2];
	uint16_t pm10_raw = (buffer[5] << 8) | buffer[4];

	*pm25 = pm25_raw / 10;
	*pm10 = pm10_raw / 10;

	return ESP_OK;
}

static esp_err_t sds011_write_state(uint8_t *buffer, uint8_t state) {
	const uint8_t payload[] = {0x06, 0x01, state, 0x00, 0x00, 0x00, 0x00, 0x00,
							   0x00, 0x00, 0x00, 0x00, 0x00};

	if (sds011_send_command(payload, buffer, RESPONSE_CMD_REPLY) != ESP_OK) {
		return ESP_FAIL;
	}

	uint8_t result = buffer[4];

	if (result != state) {
		return ESP_FAIL;
	}

	return ESP_OK;
}

/**
 * Protocol description: https://sensebox.kaufen/assets/datenblatt/SDS011_Control_Protocol.pdf
 */
void sds011_task(void *arg) {
	const uart_config_t uart_config = {
		.baud_rate = 9600,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
	};

	ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));
	ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
	ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, UART_BUFFER * 2, 0, 0, NULL, 0));

	uint8_t reporting_mode;

	uint8_t data[UART_BUFFER];
	memset(&data, 0, UART_BUFFER);

	ESP_LOGI(TAG, "Waking up SDS011");
	sds011_write_state(data, WORK_STATE);
	vTaskDelay(pdMS_TO_TICKS(shared_config.SENSORS_PARTICULATE_WARM_UP * 1000));

	if (sds011_read_reporting_mode(data, &reporting_mode) != ESP_OK) {
		ESP_LOGE(TAG, "Unable to read SDS011 reporting mode! Commiting suicide...");
		ESP_ERROR_CHECK(ESP_FAIL);
	}

	if (reporting_mode == ACTIVE_MODE) {
		ESP_LOGW(TAG, "SDS011 is currently in ACTIVE reporting mode. Switching to QUERY reporting mode.");

		if (sds011_write_reporting_mode(data, QUERY_MODE) != ESP_OK) {
			ESP_LOGE(TAG, "Unable to set SDS011 QUERY reporting mode. Commiting suicide...");
			ESP_ERROR_CHECK(ESP_FAIL);
		}
	}

	uint32_t pm25_total = 0;
	uint32_t pm10_total = 0;
	int valid_samples = 0;

	for (int i = 0; i < shared_config.SENSORS_PARTICULATE_MEASUREMENT_BULK_SIZE; i++) {
		uint16_t pm25 = 0;
		uint16_t pm10 = 0;

		ESP_LOGI(TAG, "Measuring [%d/%d]", i + 1, shared_config.SENSORS_PARTICULATE_MEASUREMENT_BULK_SIZE);
		if (sds011_query_data(data, &pm25, &pm10) == ESP_OK) {
			ESP_LOGI(TAG, "Measured [%d/%d]: PM2.5=%d, PM10=%d",
					 i + 1, shared_config.SENSORS_PARTICULATE_MEASUREMENT_BULK_SIZE,
					 pm25, pm10);

			pm25_total += pm25;
			pm10_total += pm10;
			valid_samples++;
		} else {
			ESP_LOGW(TAG, "Measurement [%d/%d] failed, skipping",
					 i + 1, shared_config.SENSORS_PARTICULATE_MEASUREMENT_BULK_SIZE);
		}

		vTaskDelay(pdMS_TO_TICKS(shared_config.SENSORS_PARTICULATE_MEASUREMENT_BULK_SLEEP * 1000));
	}

	uint16_t pm25 = 0;
	uint16_t pm10 = 0;

	if (valid_samples > 0) {
		pm25 = pm25_total / valid_samples;
		pm10 = pm10_total / valid_samples;
		ESP_LOGI(TAG, "Final measurements (%d valid samples): PM2.5=%d, PM10=%d", valid_samples, pm25, pm10);
	} else {
		ESP_LOGE(TAG, "No valid SDS011 measurements");
	}

	ESP_LOGI(TAG, "Setting SDS011 to sleep");
	sds011_write_state(data, SLEEP_STATE);

	shared_data.pm25 = pm25;
	shared_data.pm10 = pm10;

	xSemaphoreGive(sync_mutex);
	vTaskDelete(NULL);
}
