#include "stdarg.h"
#include "stdint.h"
#include "stdio.h"

#include "esp_wifi.h"

#define MAC_LEN 18

char *dynamic_format(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	if (len < 0)
		return NULL;

	char *buffer = malloc(len + 1);
	if (!buffer)
		return NULL;

	va_start(args, fmt);
	vsnprintf(buffer, len + 1, fmt, args);
	va_end(args);

	return buffer;
}
