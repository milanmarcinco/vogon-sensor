idf_component_register(
  SRCS "bluetooth.c" "internal/led.c"
  INCLUDE_DIRS "include"
  REQUIRES bt esp_driver_gpio nvs_flash shared
)
