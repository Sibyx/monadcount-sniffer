#ifndef SHARED_H
#define SHARED_H

#include <sdmmc_cmd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define MOUNT_POINT "/sdcard"
#define CSI_DATA_LEN 128 // Adjust based on your needs

// Packet data structure
typedef struct {
    int64_t timestamp;
    uint8_t src_mac[6];
    uint8_t dst_mac[6];
    int8_t rssi;
    uint8_t channel;
    uint16_t payload_len;
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

// File header for capture file
typedef struct {
    char identifier[4];   // e.g., "L2PK" or "CSIP"
    uint32_t version;     // e.g., 1
    uint64_t start_time;  // Unix timestamp when capture started
    uint8_t wifi_mac[6];  // Wi-Fi MAC address
    uint8_t bt_mac[6];    // Bluetooth MAC address
} file_header_t;

// Queue handles for L2 and CSI data
extern QueueHandle_t l2_packet_queue;
extern QueueHandle_t csi_packet_queue;

// MAC addresses
extern uint8_t wifi_mac[6];
extern uint8_t bt_mac[6];

// Storage
extern sdmmc_card_t* card;

#endif // SHARED_H
