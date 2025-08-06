idf_component_register(
  SRCS "ble_config.c" "internal/led.c"
  INCLUDE_DIRS "include"
  REQUIRES bt esp_driver_gpio shared helpers
)
