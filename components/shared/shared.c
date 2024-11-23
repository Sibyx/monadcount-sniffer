#include <esp_log.h>
#include <sys/time.h>
#include "shared.h"

// Queue handles declared in header
QueueHandle_t l2_packet_queue = NULL;
QueueHandle_t csi_packet_queue = NULL;

uint8_t wifi_mac[6];
uint8_t bt_mac[6];

sdmmc_card_t* card = NULL;

uint64_t get_wall_clock_time() {
    struct timeval now;
    gettimeofday(&now, NULL);  // Get current time
    return (uint64_t)now.tv_sec * 1000ULL + now.tv_usec / 1000ULL;  // Convert to milliseconds
}