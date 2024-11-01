#include <stdio.h>
#include <inttypes.h>
#include <esp_mac.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "bluetooth.h"
#include "nvs_flash.h"
#include "sniffer.h"
#include "management.h"

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

    // Sync time
    if (!management_obtain_time()) {
        esp_restart();
    }

    // Store MAC addresses in shared memory and print them
    if (!management_obtain_mac_addresses()) {
        esp_restart();
    }

    management_wifi_deinit();

    ESP_LOGI(TAG, "Starting Sniffer Phase");

    // Initialize Sniffer
    sniffer_init();

    // Suspend the main task
    vTaskSuspend(NULL);
}
