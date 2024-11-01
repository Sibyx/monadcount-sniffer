#include <stdio.h>
#include <inttypes.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "bluetooth.h"
#include "nvs_flash.h"
#include "sniffer.h"
#include "management.h"
#include "shared.h"

static const char* TAG = "MAIN_MODULE";

void app_main(void)
{
    int rc;

    ESP_LOGI(TAG, "Booting monadcount-sniffer");

    // Initialize NVS
    rc = nvs_flash_init();
    if (rc != ESP_OK && rc != ESP_ERR_NVS_NO_FREE_PAGES && rc != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(rc);
    }

    // Initialize the NimBLE host
    nimble_port_init();

    // Configure the host stack
    ble_hs_cfg.sync_cb = bleprph_on_sync;
    ble_hs_cfg.reset_cb = bleprph_on_reset;

    ESP_LOGD(TAG, "Look for monads initialized. Pinning tasks to core.");
    // Start the NimBLE host task
    nimble_port_freertos_init(bleprph_host_task);

    // Management Phase: Connect to Wi-Fi and synchronize time
    ESP_LOGI(TAG, "Starting Management Phase");
    management_wifi_init();

    if (!management_obtain_time()) {
        esp_restart();
    }

    rc = esp_wifi_get_mac(WIFI_IF_STA, wifi_mac);
    if (rc == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
                 wifi_mac[0], wifi_mac[1], wifi_mac[2],
                 wifi_mac[3], wifi_mac[4], wifi_mac[5]);
    } else {
        ESP_LOGE(TAG, "Failed to get Wi-Fi MAC address: %s", esp_err_to_name(rc));
    }

    // Obtain Bluetooth MAC address
    rc = esp_read_mac(bt_mac, ESP_MAC_BT);
    if (rc == ESP_OK) {
        ESP_LOGI(TAG, "Bluetooth MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
                 bt_mac[0], bt_mac[1], bt_mac[2],
                 bt_mac[3], bt_mac[4], bt_mac[5]);
    } else {
        ESP_LOGE(TAG, "Failed to get Bluetooth MAC address: %s", esp_err_to_name(rc));
    }

    management_wifi_deinit();

    ESP_LOGI(TAG, "Starting Sniffer Phase");

    // Initialize Sniffer
    sniffer_init();

    // Suspend the main task
    vTaskSuspend(NULL);
}
