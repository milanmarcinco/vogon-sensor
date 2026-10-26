idf_component_register(
  SRCS "main.c"
  INCLUDE_DIRS "."
  REQUIRES bt esp_netif esp_driver_gpio esp_hw_support esp_system nvs_flash sensors shared helpers wifi sync bluetooth
)
