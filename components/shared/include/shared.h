#pragma once

#include <stdint.h>

typedef struct {
	float temperature;
	float humidity;
	uint16_t pm25;
	uint16_t pm10;
} shared_data_t;

extern shared_data_t shared_data;
