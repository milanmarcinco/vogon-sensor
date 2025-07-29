#include "stdint.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "led.h"

#define BLINK_SLOW_PERIOD_MS 1000
#define BLINK_FAST_PERIOD_MS 250
#define DEFAULT_PERIOD_MS 100

#define LED_GPIO GPIO_NUM_2

bt_led_state_t bt_led_state = LED_OFF;

void led_task() {
	gpio_reset_pin(LED_GPIO);
	gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

	while (true) {
		switch (bt_led_state) {
		case LED_OFF:
			gpio_set_level(LED_GPIO, 0);
			vTaskDelay(pdMS_TO_TICKS(DEFAULT_PERIOD_MS));
			break;

		case LED_BLINK_SLOW:
			gpio_set_level(LED_GPIO, 1);
			vTaskDelay(pdMS_TO_TICKS(BLINK_SLOW_PERIOD_MS));
			gpio_set_level(LED_GPIO, 0);
			vTaskDelay(pdMS_TO_TICKS(BLINK_SLOW_PERIOD_MS));
			break;

		case LED_BLINK_FAST:
			gpio_set_level(LED_GPIO, 1);
			vTaskDelay(pdMS_TO_TICKS(BLINK_FAST_PERIOD_MS));
			gpio_set_level(LED_GPIO, 0);
			vTaskDelay(pdMS_TO_TICKS(BLINK_FAST_PERIOD_MS));
			break;

		case LED_ON:
			gpio_set_level(LED_GPIO, 1);
			vTaskDelay(pdMS_TO_TICKS(DEFAULT_PERIOD_MS));
			break;
		}
	}
}
