typedef enum {
	LED_OFF,
	LED_BLINK_SLOW,
	LED_BLINK_FAST,
	LED_ON
} bt_led_state_t;

extern volatile bt_led_state_t bt_led_state;
void led_task(void *arg);
