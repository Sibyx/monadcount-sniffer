#include <esp_timer.h>
#include <string.h>
#include "csi_sniffer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "shared.h"

static const char* TAG = "CSI_SNIFFER";

// Forward declarations
static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *csi_info);

void csi_sniffer_init(void) {
    // Configure CSI collection
    wifi_csi_config_t csi_config = {
            .lltf_en = true,
            .htltf_en = true,
            .stbc_htltf2_en = true,
            .ltf_merge_en = true,
            .channel_filter_en = false,
            .manu_scale = false,
            .shift = false,
    };
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));

    // Register CSI callback (do not use IRAM_ATTR)
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(&wifi_csi_rx_cb, NULL));

    // Enable CSI
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));

    ESP_LOGI(TAG, "CSI sniffer initialized");
}

void csi_sniffer_deinit(void) {
    // Disable CSI
    ESP_ERROR_CHECK(esp_wifi_set_csi(false));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(NULL, NULL));

    ESP_LOGI(TAG, "CSI sniffer deinitialized");
}

// Wi-Fi CSI RX callback
static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *csi_info) {
    if (!csi_info) {
        return;
    }

    // Minimal processing in the callback
    csi_packet_t csi_packet;

    // Get timestamp
    csi_packet.timestamp = esp_timer_get_time();

    csi_packet.channel = csi_info->rx_ctrl.channel;
    csi_packet.rssi = csi_info->rx_ctrl.rssi;
    memcpy(csi_packet.mac, csi_info->mac, 6);
    csi_packet.csi_len = csi_info->len;
    if (csi_packet.csi_len > CSI_DATA_LEN) {
        csi_packet.csi_len = CSI_DATA_LEN;
    }
    memcpy(csi_packet.csi_data, csi_info->buf, csi_packet.csi_len);

    // Enqueue the CSI data
    if (xQueueSendFromISR(csi_packet_queue, &csi_packet, NULL) != pdTRUE) {
        ESP_LOGW(TAG, "CSI Queue is full, packet is dropped");
    }
}
