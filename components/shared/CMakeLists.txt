idf_component_register(
  SRCS "shared.c"
  INCLUDE_DIRS "include"
  REQUIRES nvs_flash esp_wifi json wifi helpers
)
