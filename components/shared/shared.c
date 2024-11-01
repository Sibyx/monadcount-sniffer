#include <esp_log.h>
#include "shared.h"

// Queue handles declared in header
QueueHandle_t l2_packet_queue = NULL;
QueueHandle_t csi_packet_queue = NULL;

uint8_t wifi_mac[6];
uint8_t bt_mac[6];