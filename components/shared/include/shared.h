#ifndef SHARED_H
#define SHARED_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define CSI_DATA_LEN 128 // Adjust based on your needs

// Packet data structure
typedef struct {
    int64_t timestamp;
    uint8_t src_mac[6];
    uint8_t dst_mac[6];
    int8_t rssi;
    uint8_t channel;
    uint16_t payload_len;
    uint8_t payload[256]; // Adjust size as needed
} captured_packet_t;

// CSI packet data structure
typedef struct {
    int64_t timestamp;
    uint8_t mac[6];
    int8_t rssi;
    uint8_t channel;
    uint16_t csi_len;
    uint8_t csi_data[CSI_DATA_LEN];
} csi_packet_t;

// File header for L2 capture file
typedef struct {
    char identifier[4]; // e.g., "L2PK"
    uint32_t version;   // e.g., 1
    uint64_t start_time; // Unix timestamp when capture started
} l2_file_header_t;

// File header for CSI capture file
typedef struct {
    char identifier[4]; // e.g., "CSIP"
    uint32_t version;   // e.g., 1
    uint64_t start_time; // Unix timestamp when capture started
} csi_file_header_t;

// Queue handles for L2 and CSI data
extern QueueHandle_t l2_packet_queue;
extern QueueHandle_t csi_packet_queue;

#endif // SHARED_H
