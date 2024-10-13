#include "management.h"
#include <string.h>
#include <time.h>
#include <esp_netif_sntp.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"

#define WIFI_SSID      "Kapucinska 1 5 9"
#define WIFI_PASS      "smecarovni"
#define MAX_RETRY      5

static const char *TAG = "management_wifi";

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
    wifi_config_t wifi_config = {
            .sta = {
                    .ssid = WIFI_SSID,
                    .password = WIFI_PASS,
                    // Additional parameters if needed
                    .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                    .pmf_cfg = {
                            .capable = true,
                            .required = false
                    },
            },
    };

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
        ESP_LOGI(TAG, "Connected to SSID:%s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID:%s", WIFI_SSID);
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

    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) == ESP_OK) {
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
