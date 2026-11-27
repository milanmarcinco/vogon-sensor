idf_component_register(
  SRCS "dht22.c" "sds011.c" "scd41.c"
  INCLUDE_DIRS "include"
  REQUIRES esp-idf-lib__dht shared
  PRIV_REQUIRES esp_driver_uart esp_driver_gpio esp_driver_i2c esp_system
)
