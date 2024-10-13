// sniffer.h

#ifndef SNIFFER_H
#define SNIFFER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Packet data structure
typedef struct {
    int64_t timestamp;     // Microseconds since epoch
    uint8_t channel;
    int8_t rssi;
    uint8_t src_mac[6];
    uint8_t dst_mac[6];
    size_t payload_len;
    uint8_t payload[256];  // Adjust size as needed
} captured_packet_t;

// CSI data structure
#define CSI_DATA_LEN 128  // Adjust based on expected CSI data length

typedef struct {
    int64_t timestamp; // Microseconds since epoch
    uint8_t channel;
    int8_t rssi;
    uint8_t mac[6];
    uint16_t csi_len;
    uint8_t csi_data[CSI_DATA_LEN];
} csi_packet_t;

// File header structure
typedef struct {
    uint8_t mac[6];        // Device's Wi-Fi MAC address
    char timestamp[32];    // Timestamp when file was created
} file_header_t;

// Function declarations
void sniffer_wifi_init(void);
void sniffer_wifi_deinit(void);
void channel_hop_task(void *pvParameter);

#endif // SNIFFER_H
