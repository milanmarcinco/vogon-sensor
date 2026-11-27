#include "stdbool.h"
#include "stdint.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"

#include "sensors.h"
#include "shared.h"

#define SDA_PIN (GPIO_NUM_21)
#define SCL_PIN (GPIO_NUM_22)
#define SCD41_ADDR 0x62
#define SCD41_SCL_SPEED_HZ 100000
#define I2C_TIMEOUT_MS 100

#define CMD_START_PERIODIC_MEASUREMENT 0x21B1
#define CMD_STOP_PERIODIC_MEASUREMENT 0x3F86
#define CMD_SET_SENSOR_ALTITUDE 0x2427
#define CMD_GET_DATA_READY_STATUS 0xE4B8
#define CMD_READ_MEASUREMENT 0xEC05

#define CMD_EXECUTION_MS 1
#define STOP_PERIODIC_MS 500
#define DATA_READY_POLL_MS 500
#define DATA_READY_TIMEOUT_MS 7000

static const char *TAG = "MODULE[scd41]";

static uint8_t scd41_crc(const uint8_t *data) {
	uint8_t crc = 0xFF;

	for (int i = 0; i < 2; i++) {
		crc ^= data[i];

		for (int bit = 0; bit < 8; bit++) {
			crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
		}
	}

	return crc;
}

static esp_err_t scd41_send_command(i2c_master_dev_handle_t dev, uint16_t cmd) {
	const uint8_t buffer[] = {cmd >> 8, cmd & 0xFF};
	return i2c_master_transmit(dev, buffer, sizeof(buffer), I2C_TIMEOUT_MS);
}

static esp_err_t scd41_write_word(i2c_master_dev_handle_t dev, uint16_t cmd, uint16_t word) {
	uint8_t buffer[] = {cmd >> 8, cmd & 0xFF, word >> 8, word & 0xFF, 0x00};
	buffer[4] = scd41_crc(&buffer[2]);

	esp_err_t ret = i2c_master_transmit(dev, buffer, sizeof(buffer), I2C_TIMEOUT_MS);
	vTaskDelay(pdMS_TO_TICKS(CMD_EXECUTION_MS));
	return ret;
}

static esp_err_t scd41_read_words(i2c_master_dev_handle_t dev, uint16_t cmd, uint16_t *words, size_t count) {
	uint8_t buffer[9];
	size_t len = count * 3;

	if (count == 0 || len > sizeof(buffer)) {
		return ESP_ERR_INVALID_ARG;
	}

	esp_err_t ret = scd41_send_command(dev, cmd);
	if (ret != ESP_OK) return ret;

	vTaskDelay(pdMS_TO_TICKS(CMD_EXECUTION_MS));

	ret = i2c_master_receive(dev, buffer, len, I2C_TIMEOUT_MS);
	if (ret != ESP_OK) return ret;

	for (size_t i = 0; i < count; i++) {
		const uint8_t *word = &buffer[i * 3];

		if (scd41_crc(word) != word[2]) {
			ESP_LOGW(TAG, "Invalid CRC for word %d of command %04X", (int)i, cmd);
			return ESP_ERR_INVALID_CRC;
		}

		words[i] = (word[0] << 8) | word[1];
	}

	return ESP_OK;
}

static esp_err_t scd41_wait_data_ready(i2c_master_dev_handle_t dev) {
	TickType_t start = xTaskGetTickCount();

	while (xTaskGetTickCount() - start < pdMS_TO_TICKS(DATA_READY_TIMEOUT_MS)) {
		uint16_t status;

		if (scd41_read_words(dev, CMD_GET_DATA_READY_STATUS, &status, 1) == ESP_OK && (status & 0x07FF) != 0) {
			return ESP_OK;
		}

		vTaskDelay(pdMS_TO_TICKS(DATA_READY_POLL_MS));
	}

	return ESP_ERR_TIMEOUT;
}

static esp_err_t scd41_read_measurement(i2c_master_dev_handle_t dev, uint16_t *co2, float *temperature, float *humidity) {
	uint16_t words[3];

	esp_err_t ret = scd41_read_words(dev, CMD_READ_MEASUREMENT, words, 3);
	if (ret != ESP_OK) return ret;

	*co2 = words[0];
	*temperature = -45.0f + 175.0f * words[1] / 65535.0f;
	*humidity = 100.0f * words[2] / 65535.0f;

	return ESP_OK;
}

/**
 * Puts the sensor into a known state: idle, altitude set, periodic measurement running.
 * Altitude can only be written while the sensor is idle.
 */
static esp_err_t scd41_cold_start(i2c_master_dev_handle_t dev) {
	ESP_LOGI(TAG, "Starting SCD41 periodic measurement (altitude=%dm)", shared_config.SENSORS_CO2_ALTITUDE);

	// May fail when the sensor is already idle; the subsequent commands verify the state.
	scd41_send_command(dev, CMD_STOP_PERIODIC_MEASUREMENT);
	vTaskDelay(pdMS_TO_TICKS(STOP_PERIODIC_MS));

	esp_err_t ret = scd41_write_word(dev, CMD_SET_SENSOR_ALTITUDE, (uint16_t)shared_config.SENSORS_CO2_ALTITUDE);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Unable to set sensor altitude: %s", esp_err_to_name(ret));
		return ret;
	}

	ret = scd41_send_command(dev, CMD_START_PERIODIC_MEASUREMENT);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Unable to start periodic measurement: %s", esp_err_to_name(ret));
		return ret;
	}

	return ESP_OK;
}

/**
 * The sensor keeps measuring periodically while the ESP32 deep sleeps, so it stays
 * warmed up and its automatic self-calibration works as designed. On each wake-up
 * we only read the latest measurements.
 *
 * Datasheet: https://sensirion.com/media/documents/48C4B7FB/64C134E7/Sensirion_SCD4x_Datasheet.pdf
 */
void scd41_task(void *arg) {
	i2c_master_bus_handle_t bus = NULL;
	i2c_master_dev_handle_t dev = NULL;

	const i2c_master_bus_config_t bus_config = {
		.i2c_port = I2C_NUM_0,
		.sda_io_num = SDA_PIN,
		.scl_io_num = SCL_PIN,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.flags.enable_internal_pullup = false,
	};

	const i2c_device_config_t dev_config = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = SCD41_ADDR,
		.scl_speed_hz = SCD41_SCL_SPEED_HZ,
	};

	ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));
	ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_config, &dev));

	bool running = false;

	if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
		running = scd41_wait_data_ready(dev) == ESP_OK;

		if (!running) {
			ESP_LOGW(TAG, "SCD41 is not measuring after deep sleep");
		}
	}

	if (!running) {
		running = scd41_cold_start(dev) == ESP_OK;
	}

	uint32_t co2_total = 0;
	float temperature_total = 0;
	float humidity_total = 0;
	int valid_samples = 0;

	for (int i = 0; running && i < shared_config.SENSORS_CO2_MEASUREMENT_BULK_SIZE; i++) {
		uint16_t co2 = 0;
		float temperature = 0;
		float humidity = 0;

		ESP_LOGI(TAG, "Measuring [%d/%d]", i + 1, shared_config.SENSORS_CO2_MEASUREMENT_BULK_SIZE);

		if (scd41_wait_data_ready(dev) == ESP_OK &&
			scd41_read_measurement(dev, &co2, &temperature, &humidity) == ESP_OK) {
			ESP_LOGI(TAG, "Measured [%d/%d]: CO2=%dppm, temperature=%.2fC, humidity=%.2f%%",
					 i + 1, shared_config.SENSORS_CO2_MEASUREMENT_BULK_SIZE,
					 co2, temperature, humidity);

			co2_total += co2;
			temperature_total += temperature;
			humidity_total += humidity;
			valid_samples++;
		} else {
			ESP_LOGW(TAG, "Measurement [%d/%d] failed, skipping",
					 i + 1, shared_config.SENSORS_CO2_MEASUREMENT_BULK_SIZE);
		}

		vTaskDelay(pdMS_TO_TICKS(shared_config.SENSORS_CO2_MEASUREMENT_BULK_SLEEP * 1000));
	}

	if (valid_samples > 0) {
		shared_data.co2 = co2_total / valid_samples;
		shared_data.co2_temperature = temperature_total / valid_samples;
		shared_data.co2_humidity = humidity_total / valid_samples;

		ESP_LOGI(TAG, "Final measurements (%d valid samples): CO2=%dppm, temperature=%.2fC, humidity=%.2f%%",
				 valid_samples, shared_data.co2, shared_data.co2_temperature, shared_data.co2_humidity);
	} else {
		ESP_LOGE(TAG, "No valid SCD41 measurements");
	}

	// Leave the sensor measuring; only release the bus
	ESP_ERROR_CHECK(i2c_master_bus_rm_device(dev));
	ESP_ERROR_CHECK(i2c_del_master_bus(bus));

	xSemaphoreGive(sync_mutex);
	vTaskDelete(NULL);
}
