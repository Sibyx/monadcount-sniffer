#include "management.h"
#include <string.h>
#include <time.h>
#include <esp_netif_sntp.h>
#include <esp_mac.h>
#include <sys/stat.h>
#include "esp_wifi.h"
#include "esp_eap_client.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"

#define MAX_RETRY      5

static const char *TAG = "MANAGEMENT";

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;

// Declare variables to store handler instances
static esp_event_handler_instance_t instance_wifi_event;
static esp_event_handler_instance_t instance_ip_event;

// Forward declaration of event handlers
static void management_wifi_event_handler(void *arg, esp_event_base_t event_base,
                                          int32_t event_id, void *event_data);
static void management_ip_event_handler(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data);

void management_wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    // Initialize TCP/IP network interface
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default Wi-Fi station
    esp_netif_create_default_wifi_sta();

    // Configure Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register Event Handlers and store the instance handles
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &management_wifi_event_handler,
                                                        NULL,
                                                        &instance_wifi_event)); // Store instance

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &management_ip_event_handler,
                                                        NULL,
                                                        &instance_ip_event)); // Store instance

    // Set Wi-Fi configuration
    // Configure Wi-Fi connection
    wifi_config_t wifi_config = { 0 };

    // Set Wi-Fi SSID
    strncpy((char*)wifi_config.sta.ssid, CONFIG_MANAGEMENT_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';

    // Set authentication mode to WPA2 Enterprise
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_ENTERPRISE;

    // Set Wi-Fi mode and configuration
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_eap_client_set_username((uint8_t *)CONFIG_MANAGEMENT_WIFI_USERNAME, strlen(CONFIG_MANAGEMENT_WIFI_USERNAME)) );
    ESP_ERROR_CHECK(esp_eap_client_set_password((uint8_t *)CONFIG_MANAGEMENT_WIFI_PASSWORD, strlen(CONFIG_MANAGEMENT_WIFI_PASSWORD)) );

    // Initialize WPA2 Enterprise client
    ESP_ERROR_CHECK(esp_wifi_sta_enterprise_enable());

    // Avoid storing Wi-Fi credentials in NVS
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); // Station mode
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config)); // Set configuration
    ESP_ERROR_CHECK(esp_wifi_start()); // Start Wi-Fi

    ESP_LOGI(TAG, "Wi-Fi initialization completed in management mode.");

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    // Check connection result
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to SSID:%s", CONFIG_MANAGEMENT_WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID:%s", CONFIG_MANAGEMENT_WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "Unexpected event");
    }
}

void management_wifi_deinit(void)
{
    // Unregister event handlers using stored instances
    if (instance_ip_event != NULL) {
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_ip_event));
        instance_ip_event = NULL;
    }
    if (instance_wifi_event != NULL) {
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_wifi_event));
        instance_wifi_event = NULL;
    }

    // Delete event group
    vEventGroupDelete(s_wifi_event_group);

    // Stop Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_stop());

    // Deinitialize Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_deinit());

    ESP_LOGI(TAG, "Wi-Fi deinitialized from management mode.");
}

bool management_obtain_time(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);

    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(5000)) == ESP_OK) {
        ESP_LOGI(TAG, "System time is set from NTP server");

        time_t now;
        char strftime_buf[64];
        struct tm timeinfo;

        time(&now);
        setenv("TZ", "Etc/UTC", 1);
        tzset();

        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "The current date/time in UTC is: %s", strftime_buf);

        // Deinitialize SNTP after synchronization
        esp_netif_sntp_deinit();

        return true;
    }

    // Deinitialize SNTP in case of failure
    esp_netif_sntp_deinit();

    return false;
}

bool management_obtain_mac_addresses(void) {
    int rc;

    rc = esp_wifi_get_mac(WIFI_IF_STA, wifi_mac);
    if (rc == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
                 wifi_mac[0], wifi_mac[1], wifi_mac[2],
                 wifi_mac[3], wifi_mac[4], wifi_mac[5]);
    } else {
        ESP_LOGE(TAG, "Failed to get Wi-Fi MAC address: %s", esp_err_to_name(rc));
        return false;
    }

    // Obtain Bluetooth MAC address
    rc = esp_read_mac(bt_mac, ESP_MAC_BT);
    if (rc == ESP_OK) {
        ESP_LOGI(TAG, "Bluetooth MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
                 bt_mac[0], bt_mac[1], bt_mac[2],
                 bt_mac[3], bt_mac[4], bt_mac[5]);
    } else {
        ESP_LOGE(TAG, "Failed to get Bluetooth MAC address: %s", esp_err_to_name(rc));
        return false;
    }

    return true;
}

// Wi-Fi event handler for management phase
static void management_wifi_event_handler(void *arg, esp_event_base_t event_base,
                                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG, "Failed to connect to the AP");
        }
    }
}

// IP event handler for management phase
static void management_ip_event_handler(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool sdcard_init(void) {
    esp_err_t ret;

    // Filesystem mount config
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 10,
            .allocation_unit_size = 64 * 1024
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    host.max_freq_khz = 4000;

    spi_bus_config_t bus_cfg = {
            .mosi_io_num = CONFIG_SNIFFER_SDCARD_MOSI,
            .miso_io_num = CONFIG_SNIFFER_SDCARD_MISO,
            .sclk_io_num = CONFIG_SNIFFER_SDCARD_CLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4000,
    };

    // Initialize the SPI bus
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return false;
    }

    // Configure SD card slot
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_SNIFFER_SDCARD_CS;
    slot_config.host_id = host.slot;

    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount filesystem: %s", esp_err_to_name(ret));
        spi_bus_free(SDSPI_DEFAULT_HOST);
        return false;
    }

    ESP_LOGI(TAG, "SD card mounted at %s", MOUNT_POINT);
    sdmmc_card_print_info(stdout, card);

    struct stat st;
    if (stat(MOUNT_POINT, &st) == 0) {
        ESP_LOGI(TAG, "Mount point %s exists", MOUNT_POINT);
    } else {
        ESP_LOGE(TAG, "Mount point %s does not exist", MOUNT_POINT);
        return false;
    }

    return true;
}


bool sdcard_deinit(void) {
    // Unmount SD card
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, NULL);
    ESP_LOGI(TAG, "SD card unmounted");

    // Release SPI bus
    spi_bus_free(SPI3_HOST);

    return true;
}