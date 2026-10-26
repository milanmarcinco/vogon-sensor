idf_component_register(
  SRCS "sync.c"
  INCLUDE_DIRS "include"
  REQUIRES esp_wifi mqtt espressif__cjson shared helpers
  EMBED_TXTFILES
    "${PROJECT_DIR}/certs/ca.pem"
    "${PROJECT_DIR}/certs/client.pem"
    "${PROJECT_DIR}/certs/client.key"
)
