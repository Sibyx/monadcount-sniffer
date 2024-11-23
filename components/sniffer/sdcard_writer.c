#include <sys/stat.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include "freertos/FreeRTOS.h"
#include "sdcard_writer.h"
#include "esp_log.h"
#include "driver/spi_common.h"
#include "shared.h"

static const char* TAG = "SDCARD_WRITER";

// Task handles
static TaskHandle_t l2_writer_task_handle = NULL;
static TaskHandle_t csi_writer_task_handle = NULL;

// Forward declarations
static void l2_writer_task(void *pvParameter);
static void csi_writer_task(void *pvParameter);

// Timer callback to fsync data to SD card
static void fsync_timer_callback(TimerHandle_t xTimer) {
    int fd = (int) pvTimerGetTimerID(xTimer);
    if (fd >= 0) {
        fsync(fd);
    }
}

bool sdcard_writer_init(void)
{
    // L2 sniffer
    #ifdef CONFIG_SNIFFER_ENABLE_L2
    l2_packet_queue = xQueueCreate(CONFIG_SNIFFER_PACKET_QUEUE_SIZE, sizeof(captured_packet_t));
    if (l2_packet_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create L2 packet queue");
        return false;
    }
    xTaskCreate(l2_writer_task, "l2_writer_task", 8192, NULL, 5, &l2_writer_task_handle);
    #endif

    // CSI Sniffer
    #ifdef CONFIG_SNIFFER_ENABLE_CSI
    csi_packet_queue = xQueueCreate(CONFIG_SNIFFER_CSI_QUEUE_SIZE, sizeof(csi_packet_t));
    if (csi_packet_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create CSI packet queue");
        return false;
    }
    xTaskCreate(csi_writer_task, "csi_writer_task", 8192, NULL, 5, &csi_writer_task_handle);
    #endif

    return true;
}

void sdcard_writer_deinit(void)
{
    // Signal tasks to stop and wait for them to finish
    if (l2_writer_task_handle) {
        vTaskDelete(l2_writer_task_handle);
        l2_writer_task_handle = NULL;
    }
    if (csi_writer_task_handle) {
        vTaskDelete(csi_writer_task_handle);
        csi_writer_task_handle = NULL;
    }

    // Delete queues
    if (l2_packet_queue) {
        vQueueDelete(l2_packet_queue);
        l2_packet_queue = NULL;
    }
    if (csi_packet_queue) {
        vQueueDelete(csi_packet_queue);
        csi_packet_queue = NULL;
    }
}

// L2 writer task
static void l2_writer_task(void *pvParameter)
{
    const char *filename = "/sdcard/l2.bin";
    struct stat st;
    FILE *file;

    if (stat(filename, &st) == 0) {
        // File exists, open in append mode
        file = fopen(filename, "ab");
        if (file == NULL) {
            ESP_LOGE(TAG, "Failed to open L2 capture file: %s", strerror(errno));
            vTaskDelete(NULL);
            return;
        }
    }
    else {
        // File does not exist, open in write mode and write header
        file = fopen(filename, "wb");
        if (file == NULL) {
            ESP_LOGE(TAG, "Failed to open L2 capture file: %s", strerror(errno));
            vTaskDelete(NULL);
            return;
        }

        // Prepare and write the file header
        file_header_t header;
        memcpy(header.identifier, "L2PK", 4);
        header.version = 2;
        header.start_time = time(NULL);
        memcpy(header.wifi_mac, wifi_mac, 6);
        memcpy(header.bt_mac, bt_mac, 6);

        fwrite(&header, sizeof(header), 1, file);
        fflush(file);
    }


    int fd = fileno(file);

    // Create a timer to periodically call fsync on the file descriptor
    TimerHandle_t fsync_timer = xTimerCreate(
            "l2_fsync_timer",
            pdMS_TO_TICKS(5000),
            pdTRUE,
            (void *) fd,
            fsync_timer_callback
    );
    xTimerStart(fsync_timer, 0);

    ESP_LOGI(TAG, "L2 writer task started");

    captured_packet_t packet_data;

    while (1) {
        if (xQueueReceive(l2_packet_queue, &packet_data, portMAX_DELAY) == pdTRUE) {
            // Write packet data to file
            fwrite(&packet_data, sizeof(packet_data), 1, file);
            fflush(file);
        }
    }

}

// CSI writer task
static void csi_writer_task(void *pvParameter)
{
    const char *filename = "/sdcard/csi.bin";
    struct stat st;
    FILE *file;

    if (stat(filename, &st) == 0) {
        // File exists, open in append mode
        file = fopen(filename, "ab");
        if (file == NULL) {
            ESP_LOGE(TAG, "Failed to open CSI capture file: %s", strerror(errno));
            vTaskDelete(NULL);
            return;
        }
    } else {
        // File does not exist, open in write mode and write header
        file = fopen(filename, "wb");
        if (file == NULL) {
            ESP_LOGE(TAG, "Failed to open CSI capture file: %s", strerror(errno));
            vTaskDelete(NULL);
            return;
        }

        // Prepare and write the file header
        file_header_t header;
        memcpy(header.identifier, "CSIP", 4);
        header.version = 1;
        header.start_time = time(NULL);
        memcpy(header.wifi_mac, wifi_mac, 6);
        memcpy(header.bt_mac, bt_mac, 6);

        fwrite(&header, sizeof(header), 1, file);
        fflush(file);
    }

    int fd = fileno(file);

    // Create a timer to periodically call fsync on the file descriptor
    TimerHandle_t fsync_timer = xTimerCreate(
            "l2_fsync_timer",
            pdMS_TO_TICKS(5000),
            pdTRUE,
            (void *) fd,
            fsync_timer_callback
    );
    xTimerStart(fsync_timer, 0);

    ESP_LOGI(TAG, "CSI writer task started");

    csi_packet_t csi_data;

    while (1) {
        if (xQueueReceive(csi_packet_queue, &csi_data, portMAX_DELAY) == pdTRUE) {
            // Write CSI data to file
            fwrite(&csi_data, sizeof(csi_data), 1, file);
            fflush(file);
        }
    }
}
