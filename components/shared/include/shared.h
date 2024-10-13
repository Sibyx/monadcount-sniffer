#ifndef SHARED_H
#define SHARED_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    float temperature; // Start byte: 0
    float humidity;  // Start byte: 4
    float pressure;  // Start byte: 8
    uint16_t moisture;  // Start byte: 12
    uint16_t moisture_analog;  // Start byte: 14
    uint16_t lux;  // Start byte: 16
    uint16_t nitrogen;  // Start byte: 18
    uint16_t phosphorous;  // Start byte: 20
    uint16_t potassium;  // Start byte: 22
} shared_data_t;

extern shared_data_t shared_data;
extern SemaphoreHandle_t data_mutex;

#endif // SHARED_H
