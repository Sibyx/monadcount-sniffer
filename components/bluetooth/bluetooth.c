#include <sys/cdefs.h>
#include "bluetooth.h"

static const char* TAG = "BLE";

static uint8_t ble_addr_type;

void print_bytes_in_hex(uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        printf("%02X", data[i]);
        if (i < size - 1) {
            printf(" ");
        }
    }
    printf("\n");
}

// Function to update advertisement data
void init_advertisement_data() {
    struct ble_hs_adv_fields adv_fields;
    memset(&adv_fields, 0, sizeof(adv_fields));
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.tx_pwr_lvl_is_present = 1;
    adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    const char *name = "MONAD"; // BLE Advertisement Device Name
    adv_fields.name = (uint8_t *)name;
    adv_fields.name_len = strlen(name);
    adv_fields.name_is_complete = 1;

    // Ensure the length of manufacturer data does not exceed 31 bytes
    if (adv_fields.mfg_data_len > BLE_HS_ADV_MAX_SZ) {
        ESP_LOGE(TAG, "Manufacturer data size exceeds 31 bytes");
        return;
    }

    int rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting advertisement data; rc=%d", rc);
    }
}

// Function to start advertising
static void start_advertising(void) {
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = CONFIG_BLUETOOTH_ADVERTISEMENT_MIN;
    adv_params.itvl_max = CONFIG_BLUETOOTH_ADVERTISEMENT_MAX;

    init_advertisement_data();

    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                               &adv_params, NULL, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error starting advertising; rc=%d", rc);
    }
}

// Callback for NimBLE host reset
void bleprph_on_reset(int reason) {
    ESP_LOGE(TAG, "BLE host reset: %d", reason);
}

// Callback for NimBLE host sync
void bleprph_on_sync(void) {
    ESP_LOGI(TAG, "BLE host synchronized");

    // Automatically determine address type and store it in ble_addr_type
    int rc = ble_hs_id_infer_auto(0, &ble_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error determining address type: %d", rc);
        return;
    }

    start_advertising();
}

// Function to initialize the NimBLE stack and GATT server
void bleprph_host_task(void *param) {
    ESP_LOGI(TAG, "BLE Host Task Started");

    // Initialize the NimBLE host configuration
    nimble_port_run();

    ESP_LOGI(TAG, "BLE Host Task Ended");
}
