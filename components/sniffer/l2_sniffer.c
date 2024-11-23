#include <sys/cdefs.h>
#include <esp_types.h>
#include <esp_timer.h>
#include <string.h>
#include <sys/time.h>
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "l2_sniffer.h"
#include "shared.h"

static const char* TAG = "L2_SNIFFER";

// Forward declaration
static void wifi_promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type);

void l2_sniffer_init(void) {
    // Register the RX callback
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_rx_cb));

    // Enable promiscuous mode
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    ESP_LOGI(TAG, "L2 sniffer initialized");
}

void l2_sniffer_deinit(void) {
    // Disable promiscuous mode
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(false));

    ESP_LOGI(TAG, "L2 sniffer deinitialized");
}

// Wi-Fi promiscuous RX callback
static void wifi_promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (!buf) {
        return;
    }

    // Cast to the incoming packet structure
    const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
    const wifi_pkt_rx_ctrl_t *rx_ctrl = &ppkt->rx_ctrl;

    // Prepare captured packet data
    captured_packet_t packet_data;
    packet_data.timestamp = get_wall_clock_time();  // Get wall-clock timestamp with ms precision
    packet_data.rssi = rx_ctrl->rssi;
    packet_data.channel = rx_ctrl->channel;

    // Extract the frame type and subtype from the first byte of the 802.11 header
    const uint8_t *packet_payload = ppkt->payload;
    uint8_t frame_control = packet_payload[0];
    packet_data.frame_type = (frame_control >> 2) & 0x03;    // Extract the frame type (bits 2-3)
    packet_data.frame_subtype = (frame_control >> 4) & 0x0F; // Extract the frame subtype (bits 4-7)

    // Determine processing based on the frame type
    if (type == WIFI_PKT_MGMT || type == WIFI_PKT_CTRL) {
        // For management and control frames, store the full payload
        packet_data.header_len = rx_ctrl->sig_len < 36 ? rx_ctrl->sig_len : 36;
        memcpy(packet_data.header, ppkt->payload, packet_data.header_len);

        // Calculate the payload length for control/management frames
        packet_data.payload_len = rx_ctrl->sig_len - packet_data.header_len;
        if (packet_data.payload_len > 128) {
            packet_data.payload_len = 128; // Truncate if payload is larger than buffer
        }
        memcpy(packet_data.payload, ppkt->payload + packet_data.header_len, packet_data.payload_len);

    } else if (type == WIFI_PKT_DATA) {
        // For data frames, only store the header
        packet_data.header_len = rx_ctrl->sig_len < 36 ? rx_ctrl->sig_len : 36;
        memcpy(packet_data.header, ppkt->payload, packet_data.header_len);

        // Set payload length to zero for data frames, as we’re excluding it
        packet_data.payload_len = 0;
    }

    // Enqueue the packet data
    if (xQueueSendFromISR(l2_packet_queue, &packet_data, NULL) != pdTRUE) {
        ESP_LOGW(TAG, "L2 Queue is full, packet is dropped");
    }
}
