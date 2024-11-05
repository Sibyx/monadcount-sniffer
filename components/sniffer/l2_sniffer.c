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

    // Minimal processing in the callback
    const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
    const wifi_pkt_rx_ctrl_t *rx_ctrl = &ppkt->rx_ctrl;

    // Prepare captured packet data
    captured_packet_t packet_data;
    packet_data.timestamp = time(NULL);
    packet_data.rssi = rx_ctrl->rssi;
    packet_data.channel = rx_ctrl->channel;
    packet_data.payload_len = rx_ctrl->sig_len;

    // Enqueue the packet data
    if (xQueueSendFromISR(l2_packet_queue, &packet_data, NULL) != pdTRUE) {
        ESP_LOGW(TAG, "L2 Queue is full, packet is dropped");
    }
}
