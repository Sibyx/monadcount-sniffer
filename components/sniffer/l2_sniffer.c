#include <sys/cdefs.h>
#include <esp_types.h>
#include <esp_timer.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "l2_sniffer.h"

static const char* TAG = "L2_SNIFFER";

static QueueHandle_t packet_queue;
#define PACKET_QUEUE_SIZE 100

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

// Forward declaration
static void wifi_promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type);

_Noreturn static void l2_packet_handler_task(void *pvParameter);

void l2_sniffer_init(void) {
    // Initialize packet queue
    packet_queue = xQueueCreate(PACKET_QUEUE_SIZE, sizeof(captured_packet_t));
    if (packet_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create packet queue");
    }

    // Register the RX callback
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_rx_cb));

    // Enable promiscuous mode
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    // Start packet handler task
    xTaskCreate(l2_packet_handler_task, "l2_packet_handler_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "L2 sniffer initialized");
}

void l2_sniffer_deinit(void) {
    // Disable promiscuous mode
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(false));

    // Delete packet queue
    if (packet_queue) {
        vQueueDelete(packet_queue);
        packet_queue = NULL;
    }

    ESP_LOGI(TAG, "L2 sniffer deinitialized");
}

// Wi-Fi promiscuous RX callback
static void wifi_promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (!buf) {
        return;
    }

    // Minimal processing in the callback
    const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
    const wifi_pkt_rx_ctrl_t *rx_ctrl = &ppkt->rx_ctrl;

    // Prepare captured packet data
    captured_packet_t packet_data;
    packet_data.timestamp = esp_timer_get_time();
    packet_data.rssi = rx_ctrl->rssi;
    packet_data.channel = rx_ctrl->channel;

    // Copy packet payload
    uint16_t len = rx_ctrl->sig_len;
    if (len > sizeof(packet_data.payload)) {
        len = sizeof(packet_data.payload);
    }
    packet_data.payload_len = len;
    memcpy(packet_data.payload, ppkt->payload, len);

    // Enqueue the packet data
    if (xQueueSendFromISR(packet_queue, &packet_data, NULL) != pdTRUE) {
        // Queue is full, packet is dropped
    }
}

// Packet handler task
_Noreturn static void l2_packet_handler_task(void *pvParameter) {
    captured_packet_t packet_data;

    while (1) {
        if (xQueueReceive(packet_queue, &packet_data, portMAX_DELAY) == pdTRUE) {
            // Process packet data
            // For now, just log the RSSI and timestamp
            ESP_LOGI(TAG, "Packet received: RSSI=%d, Timestamp=%lld", packet_data.rssi, packet_data.timestamp);
        }
    }
}
