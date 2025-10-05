#define RETURN_ON_ERROR(x)                                              \
	do {                                                                \
		esp_err_t __err = (x);                                          \
		if (__err != ESP_OK) {                                          \
			ESP_LOGE(TAG, "%s failed: %s", #x, esp_err_to_name(__err)); \
			return ESP_FAIL;                                            \
		}                                                               \
	} while (0)

#define RETURN_ON_ERROR_RET(x, retval)                                  \
	do {                                                                \
		esp_err_t __err = (x);                                          \
		if (__err != ESP_OK) {                                          \
			ESP_LOGE(TAG, "%s failed: %s", #x, esp_err_to_name(__err)); \
			return (retval);                                            \
		}                                                               \
	} while (0)

#define BREAK_ON_ERROR(x)                                               \
	do {                                                                \
		esp_err_t __err = (x);                                          \
		if (__err != ESP_OK) {                                          \
			ESP_LOGE(TAG, "%s failed: %s", #x, esp_err_to_name(__err)); \
			break;                                                      \
		}                                                               \
	} while (0)

char *dynamic_format(const char *fmt, ...);
