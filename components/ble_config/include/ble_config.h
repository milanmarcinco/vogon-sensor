extern QueueHandle_t gpio_evt_queue;

void ble_config_gatt_server_start();

void gpio_isr_handler(void *arg);
void ble_config_gat_server_trigger_task();
