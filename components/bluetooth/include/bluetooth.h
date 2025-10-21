extern QueueHandle_t gpio_evt_queue;

void bluetooth_gatt_server_start();

void gpio_isr_handler(void *arg);
void bluetooth_gat_server_trigger_task();
