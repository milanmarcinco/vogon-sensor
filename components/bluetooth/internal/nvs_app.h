#define NVS_KEY_GENERAL_MEASUREMENT_INTERVAL "measurement_interval"

#define NVS_KEY_SENSORS_DHT22_MEASUREMENT_BULK_SIZE "dht22_measurement_bulk_size"
#define NVS_KEY_SENSORS_DHT22_MEASUREMENT_BULK_SLEEP "dht22_measurement_bulk_sleep"
#define NVS_KEY_SENSORS_SDS011_WARM_UP "sds011_warm_up"
#define NVS_KEY_SENSORS_SDS011_MEASUREMENT_BULK_SIZE "sds011_measurement_bulk_size"
#define NVS_KEY_SENSORS_SDS011_MEASUREMENT_BULK_SLEEP "sds011_measurement_bulk_sleep"

#define NVS_KEY_SYNC_WIFI_SSID "wifi_ssid"
#define NVS_KEY_SYNC_WIFI_PASSWORD "wifi_password"
#define NVS_KEY_SYNC_MQTT_BROKER_URL "mqtt_broker_url"

char *vogon_nvs_get_str(const char *key, const char *default_value);
esp_err_t vogon_nvs_set_str(const char *key, const char *value);
