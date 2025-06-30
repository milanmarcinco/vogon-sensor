idf_component_register(
  SRCS "dht22.c" "sds011.c"
  INCLUDE_DIRS "include"
  REQUIRES dht shared
)