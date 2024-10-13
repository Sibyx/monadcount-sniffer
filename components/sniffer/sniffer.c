#include "sniffer.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include "l2_sniffer.h"
#include "csi_sniffer.h"

static const char* TAG = "SNIFFER";

void sniffer_init(void) {
    ESP_LOGI(TAG, "Initializing sniffer");

    // Initialize Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Set Wi-Fi channel to 1 initially
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));

    // Initialize L2 sniffer
    l2_sniffer_init();

    // Initialize CSI sniffer
    csi_sniffer_init();

    // Start channel hopping task
    xTaskCreate(channel_hop_task, "channel_hop_task", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Sniffer initialized");
}

void sniffer_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing sniffer");

    // Deinitialize CSI sniffer
    csi_sniffer_deinit();

    // Deinitialize L2 sniffer
    l2_sniffer_deinit();

    // Stop Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());

    ESP_LOGI(TAG, "Sniffer deinitialized");
}

// Channel hopping task
void channel_hop_task(void *pvParameter) {
    uint8_t channel = 1;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Change channel every 1 second
        channel = (channel % 13) + 1; // Loop from 1 to 13
        ESP_ERROR_CHECK(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE));
    }
}
