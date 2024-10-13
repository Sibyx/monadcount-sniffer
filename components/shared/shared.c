#include <esp_log.h>
#include <string.h>
#include "shared.h"

static const char* TAG = "SHARED";

shared_data_t shared_data;
SemaphoreHandle_t data_mutex;

