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
#include "esp_http_client.h"

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

static void restart_timer_callback(TimerHandle_t xTimer) {
    esp_restart();
}

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

void upload_files_to_server(void) {
    // Obtain MAC address (Device ID)
    char device_id[18];
    snprintf(device_id, sizeof(device_id), "%02X:%02X:%02X:%02X:%02X:%02X",
             wifi_mac[0], wifi_mac[1], wifi_mac[2],
             wifi_mac[3], wifi_mac[4], wifi_mac[5]);

    // Prepare the 'Authorization' header value
    char auth_header_value[128];
    snprintf(auth_header_value, sizeof(auth_header_value), "Basic %s", CONFIG_MANAGEMENT_SERVER_BASIC_AUTH);

    // Define files to upload
    const char *files_to_upload[] = {"/sdcard/l2.bin", "/sdcard/csi.bin"};
    const char *file_types[] = {"l2", "csi"};

    for (int i = 0; i < 2; i++) {
        const char *filepath = files_to_upload[i];
        const char *file_type = file_types[i];

        struct stat st;
        if (stat(filepath, &st) == 0) {
            ESP_LOGI(TAG, "File %s exists, size: %ld bytes", filepath, st.st_size);

            // Open file
            FILE *file = fopen(filepath, "rb");
            if (file == NULL) {
                ESP_LOGE(TAG, "Failed to open file %s", filepath);
                continue;
            }

            // Configure HTTP client
            esp_http_client_config_t config = {
                    .url = CONFIG_MANAGEMENT_SERVER_URL,
                    .method = HTTP_METHOD_POST,
                    .transport_type = HTTP_TRANSPORT_OVER_TCP,
                    .timeout_ms = 600000
            };

            esp_http_client_handle_t client = esp_http_client_init(&config);

            // Set HTTP headers
            esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
            esp_http_client_set_header(client, "Device-ID", device_id);
            esp_http_client_set_header(client, "File-Type", file_type);
            esp_http_client_set_header(client, "Authorization", auth_header_value);

            // Start HTTP connection and write headers
            esp_err_t err = esp_http_client_open(client, st.st_size);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
                esp_http_client_cleanup(client);
                fclose(file);
                continue;
            }

            // Read from file and write to HTTP client
            size_t buffer_size = 1024 * 50;  // Adjust as needed
            uint8_t *buffer = malloc(buffer_size);
            if (buffer == NULL) {
                ESP_LOGE(TAG, "Failed to allocate buffer");
                esp_http_client_cleanup(client);
                fclose(file);
                continue;
            }

            uint64_t total_uploaded = 0;
            uint64_t read_bytes = 0;
            bool upload_failed = false;
            int last_reported_percentage = -1;

            ESP_LOGI(TAG, "Uploading: %s", filepath);
            while ((read_bytes = fread(buffer, 1, buffer_size, file)) > 0) {
                int wlen = esp_http_client_write(client, (char *) buffer, read_bytes);
                if (wlen < 0) {
                    ESP_LOGE(TAG, "Error writing data to HTTP stream");
                    upload_failed = true;
                    break;
                }
                total_uploaded += wlen;

                // Calculate and display percentage (with casting to prevent overflow)
                int percentage = (int)((total_uploaded * 100) / (uint64_t)st.st_size);
                if (percentage != last_reported_percentage) {
                    ESP_LOGI(TAG, "Progress (%s): %d%%", filepath, percentage);
                    last_reported_percentage = percentage;
                }
            }
            ESP_LOGI(TAG, "Upload complete for %s", filepath);

            free(buffer);
            fclose(file);

            if (!upload_failed) {
                // Finish the HTTP request
                esp_http_client_fetch_headers(client);
                int status = esp_http_client_get_status_code(client);
                if (status == 200) {
                    ESP_LOGI(TAG, "File %s uploaded successfully", filepath);

                    // Delete the file after successful upload
                    if (unlink(filepath) == 0) {
                        ESP_LOGI(TAG, "File %s deleted after upload", filepath);
                    } else {
                        ESP_LOGE(TAG, "Failed to delete file %s", filepath);
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to upload file %s, HTTP status code: %d", filepath, status);
                }
            } else {
                ESP_LOGW(TAG, "Upload failed for file %s. Will retry later.", filepath);
            }

            esp_http_client_close(client);
            esp_http_client_cleanup(client);
        } else {
            ESP_LOGI(TAG, "File %s does not exist", filepath);
        }
    }
}



void init_restart_timer(void) {
    TimerHandle_t restart_timer = xTimerCreate(
            "restart_timer",
            pdMS_TO_TICKS(CONFIG_MANAGEMENT_REBOOT_INTERVAL * 60 * 1000),
            pdTRUE,
            NULL,
            restart_timer_callback
    );

    if (restart_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create restart timer");
        return;
    }

    // Start the timer
    if (xTimerStart(restart_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start restart timer");
    } else {
        ESP_LOGI(TAG, "Restart timer initialized to trigger every %d minutes.", CONFIG_MANAGEMENT_REBOOT_INTERVAL);
    }
}
