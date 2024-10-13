#include <esp_timer.h>
#include <string.h>
#include "csi_sniffer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char* TAG = "CSI_SNIFFER";

#define CSI_QUEUE_SIZE 100
#define CSI_DATA_LEN 128 // Adjust based on your needs

// CSI packet data structure
typedef struct {
    int64_t timestamp;
    uint8_t mac[6];
    int8_t rssi;
    uint8_t channel;
    uint16_t csi_len;
    uint8_t csi_data[CSI_DATA_LEN];
} csi_packet_t;

static QueueHandle_t csi_queue = NULL;

// Forward declarations
static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *csi_info);
static void csi_data_handler_task(void *pvParameter);

void csi_sniffer_init(void) {
    // Initialize CSI queue
    csi_queue = xQueueCreate(CSI_QUEUE_SIZE, sizeof(csi_packet_t));
    if (csi_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create CSI queue");
        return;
    }

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

    // Start CSI data handler task
    xTaskCreate(csi_data_handler_task, "csi_data_handler_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "CSI sniffer initialized");
}

void csi_sniffer_deinit(void) {
    // Disable CSI
    ESP_ERROR_CHECK(esp_wifi_set_csi(false));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(NULL, NULL));

    // Delete CSI queue
    if (csi_queue) {
        vQueueDelete(csi_queue);
        csi_queue = NULL;
    }

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
    if (xQueueSendFromISR(csi_queue, &csi_packet, NULL) != pdTRUE) {
        // Queue is full, CSI packet is dropped
    }
}

// CSI data handler task
static void csi_data_handler_task(void *pvParameter) {
    csi_packet_t csi_packet;
    while (1) {
        if (xQueueReceive(csi_queue, &csi_packet, portMAX_DELAY) == pdTRUE) {
            // Process CSI data
            // For example, log the RSSI and timestamp
            ESP_LOGI(TAG, "CSI data received: RSSI=%d, Timestamp=%lld", csi_packet.rssi, csi_packet.timestamp);

            // You can add code here to save CSI data to SD card or process it further
        }
    }
}
