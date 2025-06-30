#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct
{
    float temperature;
    float humidity;
    uint16_t pm25;
    uint16_t pm10;
} shared_data_t;

extern SemaphoreHandle_t sync_mutex;
extern shared_data_t shared_data;
