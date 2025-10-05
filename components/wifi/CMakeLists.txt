idf_component_register(
  SRCS "wifi.c"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES esp_event esp_netif esp_wifi shared helpers
)
