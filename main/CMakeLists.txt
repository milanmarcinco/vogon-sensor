idf_component_register(
  SRCS "main.c"
  INCLUDE_DIRS "."
  REQUIRES bt nvs_flash sensors shared helpers sync bluetooth led
)
