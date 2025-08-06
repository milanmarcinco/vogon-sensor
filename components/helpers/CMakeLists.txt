idf_component_register(
  SRCS "helpers.c" "vogon_nvs.c"
  INCLUDE_DIRS "include"
  REQUIRES esp_wifi nvs_flash shared
)
