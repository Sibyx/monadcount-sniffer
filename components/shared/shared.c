#include <esp_log.h>
#include <string.h>
#include "shared.h"

// Queue handles declared in header
QueueHandle_t l2_packet_queue = NULL;
QueueHandle_t csi_packet_queue = NULL;

