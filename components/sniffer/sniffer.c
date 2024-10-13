// sniffer.c

#include "sniffer.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sdmmc_cmd.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/sdmmc_defs.h"
#include "esp_vfs_fat.h"
#include "driver/spi_common.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// Constants and macros
#define WIFI_CHANNEL_MAX 13
#define WIFI_CHANNEL_SWITCH_INTERVAL_MS 1000 // milliseconds

// SD card SPI configuration (adjust pins according to your setup)
#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5

#define MOUNT_POINT "/sdcard"

// Queue sizes
#define PACKET_QUEUE_SIZE 100
#define CSI_QUEUE_SIZE 100

// Logger tag
static const char *TAG = "sniffer";

// Global variables
static QueueHandle_t packet_queue;
static QueueHandle_t csi_queue;

// SD card variables
static sdmmc_card_t* card;        // SD card handle
static sdmmc_host_t host = SDSPI_HOST_DEFAULT(); // SDMMC host

// Function prototypes
static void wifi_promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type);
static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *csi_info);
static void sdcard_writer_task(void *pvParameter);
static void csi_writer_task(void *pvParameter);
static esp_err_t sdcard_init(void);
static void sdcard_deinit(void);
static void get_synchronized_timestamp(int64_t *timestamp);

// Frame Control field structure
typedef struct {
    uint8_t protocol_version:2;
    uint8_t type:2;
    uint8_t subtype:4;
    uint8_t to_ds:1;
    uint8_t from_ds:1;
    uint8_t more_frag:1;
    uint8_t retry:1;
    uint8_t pwr_mgmt:1;
    uint8_t more_data:1;
    uint8_t protected_frame:1;
    uint8_t order:1;
} __attribute__((packed)) wifi_ieee80211_frame_ctrl_t;

// MAC header structure
typedef struct {
    wifi_ieee80211_frame_ctrl_t frame_ctrl;
    uint16_t duration_id;
    uint8_t addr1[6]; // Receiver address
    uint8_t addr2[6]; // Transmitter address
    uint8_t addr3[6]; // Filtering address
    uint16_t sequence_ctrl;
} __attribute__((packed)) wifi_ieee80211_mac_hdr_t;

// Implementations

void sniffer_wifi_init(void)
{
    // Initialize SD card
    ESP_ERROR_CHECK(sdcard_init());

    // Initialize queues
    packet_queue = xQueueCreate(PACKET_QUEUE_SIZE, sizeof(captured_packet_t));
    if (packet_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create packet queue");
    }

    csi_queue = xQueueCreate(CSI_QUEUE_SIZE, sizeof(csi_packet_t));
    if (csi_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create CSI queue");
    }

    // Start SD card writer tasks
    xTaskCreatePinnedToCore(sdcard_writer_task, "sdcard_writer_task", 4096, NULL, 5, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(csi_writer_task, "csi_writer_task", 4096, NULL, 5, NULL, tskNO_AFFINITY);

    // Initialize Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // Adjust Wi-Fi configuration to minimize IRAM usage
    cfg.nvs_enable = 0; // Disable NVS storage to save memory if not needed
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Disable default Wi-Fi logging
    esp_log_level_set("wifi", ESP_LOG_WARN);

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    // Set Wi-Fi to null mode (not station or AP)
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));

    // Start Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_start());

    // Set the Wi-Fi channel to 1 initially
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));

    // Register the RX callback
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_rx_cb));

    // Enable promiscuous mode
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    // Configure CSI collection and register callback
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
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));

    // Start channel hopping task
    xTaskCreatePinnedToCore(channel_hop_task, "channel_hop_task", 2048, NULL, 5, NULL, tskNO_AFFINITY);

    ESP_LOGI(TAG, "Sniffer initialized and running.");
}

void sniffer_wifi_deinit(void)
{
    // Disable CSI
    ESP_ERROR_CHECK(esp_wifi_set_csi(false));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(NULL, NULL));

    // Disable promiscuous mode
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(false));

    // Stop Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_stop());

    // Deinitialize Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_deinit());

    // Deinitialize SD card
    sdcard_deinit();

    // Delete queues
    if (packet_queue) {
        vQueueDelete(packet_queue);
        packet_queue = NULL;
    }
    if (csi_queue) {
        vQueueDelete(csi_queue);
        csi_queue = NULL;
    }

    ESP_LOGI(TAG, "Sniffer deinitialized.");
}

// SD card initialization for ESP-IDF 5.x
static esp_err_t sdcard_init(void)
{
    esp_err_t ret;

    // Options for mounting the filesystem.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 16 * 1024
    };

    // Use SPI interface
    host.flags = SDMMC_HOST_FLAG_SPI;
    host.slot = SPI3_HOST; // VSPI_HOST or HSPI_HOST

    // Configure the SPI bus
    spi_bus_config_t bus_cfg = {
            .mosi_io_num = PIN_NUM_MOSI,
            .miso_io_num = PIN_NUM_MISO,
            .sclk_io_num = PIN_NUM_CLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4000,
            .flags = 0,
            .intr_flags = 0
    };
    ret = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus.");
        return ret;
    }

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    // Mount the filesystem
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount filesystem. Error: %s", esp_err_to_name(ret));
        spi_bus_free(host.slot);
        return ret;
    }

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    ESP_LOGI(TAG, "SD card mounted at %s", MOUNT_POINT);
    return ESP_OK;
}

// SD card deinitialization
static void sdcard_deinit(void)
{
    // Unmount the filesystem
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    ESP_LOGI(TAG, "SD card unmounted");

    // Free SPI bus
    spi_bus_free(host.slot);
}

// Get synchronized timestamp
static void get_synchronized_timestamp(int64_t *timestamp)
{
    time_t now;
    time(&now); // Seconds since epoch

    int64_t uptime_us = esp_timer_get_time(); // Microseconds since boot

    static int64_t boot_time_us = 0;
    if (boot_time_us == 0) {
        boot_time_us = ((int64_t)now * 1000000LL) - uptime_us;
    }

    *timestamp = boot_time_us + uptime_us;
}

// Wi-Fi promiscuous RX callback
static void wifi_promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (!buf) {
        return;
    }

    const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
    const wifi_pkt_rx_ctrl_t *rx_ctrl = &ppkt->rx_ctrl;

    const uint8_t *packet = ppkt->payload;
    uint16_t len = rx_ctrl->sig_len;

    if (len < sizeof(wifi_ieee80211_mac_hdr_t)) {
        return; // Packet too short to contain header
    }

    const wifi_ieee80211_mac_hdr_t *hdr = (wifi_ieee80211_mac_hdr_t *)packet;

    // Extract source and destination MAC addresses
    uint8_t src_mac[6];
    uint8_t dst_mac[6];

    // Determine addressing based on To DS and From DS fields
    uint8_t to_ds = hdr->frame_ctrl.to_ds;
    uint8_t from_ds = hdr->frame_ctrl.from_ds;

    if (to_ds == 0 && from_ds == 0) {
        // Ad-hoc mode
        memcpy(dst_mac, hdr->addr1, 6);
        memcpy(src_mac, hdr->addr2, 6);
    } else if (to_ds == 0 && from_ds == 1) {
        // From AP to STA
        memcpy(dst_mac, hdr->addr1, 6);
        memcpy(src_mac, hdr->addr3, 6);
    } else if (to_ds == 1 && from_ds == 0) {
        // From STA to AP
        memcpy(dst_mac, hdr->addr3, 6);
        memcpy(src_mac, hdr->addr2, 6);
    } else {
        // WDS mode or other; not handled in this example
        return;
    }

    // Get the synchronized timestamp
    int64_t timestamp;
    get_synchronized_timestamp(&timestamp);

    // Get RSSI (signal strength)
    int8_t rssi = rx_ctrl->rssi;

    // Get current channel
    uint8_t channel = rx_ctrl->channel;

    // Prepare captured packet data
    captured_packet_t packet_data;
    packet_data.timestamp = timestamp;
    packet_data.channel = channel;
    packet_data.rssi = rssi;
    memcpy(packet_data.src_mac, src_mac, 6);
    memcpy(packet_data.dst_mac, dst_mac, 6);
    packet_data.payload_len = len;
    if (len > sizeof(packet_data.payload)) {
        packet_data.payload_len = sizeof(packet_data.payload);
    }
    memcpy(packet_data.payload, packet, packet_data.payload_len);

    // Enqueue the packet data
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xQueueSendFromISR(packet_queue, &packet_data, &xHigherPriorityTaskWoken) != pdTRUE) {
        // Queue is full, packet is dropped
    }

    // Yield to higher priority task if needed
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// Wi-Fi CSI RX callback
static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *csi_info)
{
    if (!csi_info) {
        return;
    }

    // Prepare a structure to hold CSI data
    csi_packet_t csi_packet;

    // Get the synchronized timestamp
    int64_t timestamp;
    get_synchronized_timestamp(&timestamp);

    csi_packet.timestamp = timestamp;
    csi_packet.channel = csi_info->rx_ctrl.channel;
    csi_packet.rssi = csi_info->rx_ctrl.rssi;
    memcpy(csi_packet.mac, csi_info->mac, 6);
    csi_packet.csi_len = csi_info->len;
    if (csi_packet.csi_len > CSI_DATA_LEN) {
        csi_packet.csi_len = CSI_DATA_LEN;
    }
    memcpy(csi_packet.csi_data, csi_info->buf, csi_packet.csi_len);

    // Enqueue the CSI data
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xQueueSendFromISR(csi_queue, &csi_packet, &xHigherPriorityTaskWoken) != pdTRUE) {
        // Queue is full, CSI packet is dropped
    }

    // Yield to higher priority task if needed
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// SD card writer task for packet data
static void sdcard_writer_task(void *pvParameter)
{
    FILE *f = NULL;
    const char *filename = MOUNT_POINT "/capture.bin";
    const TickType_t close_interval = pdMS_TO_TICKS(5 * 60 * 1000); // Close every 5 minutes
    TickType_t last_close_time = xTaskGetTickCount();

    while (1) {
        // Open file if not already open
        if (f == NULL) {
            f = fopen(filename, "ab");
            if (f == NULL) {
                ESP_LOGE(TAG, "Failed to open file %s for appending", filename);
                vTaskDelay(pdMS_TO_TICKS(1000)); // Wait before retrying
                continue;
            }

            // Write header if file is new
            fseek(f, 0, SEEK_END);
            if (ftell(f) == 0) {
                file_header_t header;
                ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_STA, header.mac));
                time_t now;
                time(&now);
                strftime(header.timestamp, sizeof(header.timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
                fwrite(&header, sizeof(file_header_t), 1, f);
                fflush(f);
                ESP_LOGI(TAG, "Header written to file %s", filename);
            }
        }

        // Receive packet data
        captured_packet_t packet_data;
        if (xQueueReceive(packet_queue, &packet_data, pdMS_TO_TICKS(100)) == pdTRUE) {
            fwrite(&packet_data, sizeof(captured_packet_t), 1, f);
        }

        // Periodically flush and close the file
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - last_close_time) >= close_interval) {
            fflush(f);
            fclose(f);
            f = NULL;
            last_close_time = current_time;
            ESP_LOGI(TAG, "File %s flushed and closed", filename);
        }
    }
}

// SD card writer task for CSI data
static void csi_writer_task(void *pvParameter)
{
    FILE *f = NULL;
    const char *filename = MOUNT_POINT "/csi.bin";
    const TickType_t close_interval = pdMS_TO_TICKS(5 * 60 * 1000); // Close every 5 minutes
    TickType_t last_close_time = xTaskGetTickCount();

    while (1) {
        // Open file if not already open
        if (f == NULL) {
            f = fopen(filename, "ab");
            if (f == NULL) {
                ESP_LOGE(TAG, "Failed to open CSI file %s for appending", filename);
                vTaskDelay(pdMS_TO_TICKS(1000)); // Wait before retrying
                continue;
            }

            // Write header if file is new
            fseek(f, 0, SEEK_END);
            if (ftell(f) == 0) {
                file_header_t header;
                ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_STA, header.mac));
                time_t now;
                time(&now);
                strftime(header.timestamp, sizeof(header.timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
                fwrite(&header, sizeof(file_header_t), 1, f);
                fflush(f);
                ESP_LOGI(TAG, "Header written to CSI file %s", filename);
            }
        }

        // Receive CSI data
        csi_packet_t csi_packet;
        if (xQueueReceive(csi_queue, &csi_packet, pdMS_TO_TICKS(100)) == pdTRUE) {
            fwrite(&csi_packet, sizeof(csi_packet_t), 1, f);
        }

        // Periodically flush and close the file
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - last_close_time) >= close_interval) {
            fflush(f);
            fclose(f);
            f = NULL;
            last_close_time = current_time;
            ESP_LOGI(TAG, "CSI file %s flushed and closed", filename);
        }
    }
}

// Channel hopping task
void channel_hop_task(void *pvParameter)
{
    uint8_t channel = 1;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(WIFI_CHANNEL_SWITCH_INTERVAL_MS));
        channel = (channel % WIFI_CHANNEL_MAX) + 1; // Loop from 1 to 13
        ESP_ERROR_CHECK(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE));
    }
    vTaskDelete(NULL);
}
