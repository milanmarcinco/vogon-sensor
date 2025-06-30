#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "dht.h"

#include "shared.h"

#define DHT22_PIN 23

static const char *TAG = "MODULE[DHT22]";

void dht22_task()
{
  float temperature_total = 0;
  float humidity_total = 0;

  for (int i = 0; i < CONFIG_SENSORS_DHT22_MEASUREMENT_BULK_SIZE; i++)
  {
    float temperature = 0;
    float humidity = 0;

    ESP_LOGI(TAG, "Measuring [%d/%d]", i + 1, CONFIG_SENSORS_DHT22_MEASUREMENT_BULK_SIZE);

    esp_err_t result = dht_read_float_data(
        DHT_TYPE_AM2301, DHT22_PIN,
        &humidity, &temperature);

    if (result != ESP_OK)
    {
      ESP_LOGW(TAG, "Temperature/humidity reading failed");
      ESP_ERROR_CHECK(ESP_FAIL);
    }

    ESP_LOGI(TAG, "Measured [%d/%d]: temperature=%.2fC, humidity=%.2f%%",
             i + 1, CONFIG_SENSORS_DHT22_MEASUREMENT_BULK_SIZE,
             temperature, humidity);

    temperature_total += temperature;
    humidity_total += humidity;

    vTaskDelay(pdMS_TO_TICKS(CONFIG_SENSORS_DHT22_MEASUREMENT_BULK_SLEEP * 1000));
  }

  float temperature = temperature_total / CONFIG_SENSORS_DHT22_MEASUREMENT_BULK_SIZE;
  float humidity = humidity_total / CONFIG_SENSORS_DHT22_MEASUREMENT_BULK_SIZE;

  ESP_LOGI(TAG, "Final measurements: temperature=%.2fC, humidity=%.2f%%", temperature, humidity);

  shared_data.temperature = temperature;
  shared_data.humidity = humidity;

  xSemaphoreGive(sync_mutex);
  vTaskDelete(NULL);
}
