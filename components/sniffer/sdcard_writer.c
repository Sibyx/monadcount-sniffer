#include <sdmmc_cmd.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include "freertos/FreeRTOS.h"
#include "sdcard_writer.h"
#include "esp_log.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "shared.h"

static const char* TAG = "SDCARD_WRITER";

// SD card mount point
#define MOUNT_POINT "/sdcard"

// Task handles
static TaskHandle_t l2_writer_task_handle = NULL;
static TaskHandle_t csi_writer_task_handle = NULL;

// Forward declarations
static void l2_writer_task(void *pvParameter);
static void csi_writer_task(void *pvParameter);

static sdmmc_card_t* card = NULL;

// Timer callback to fsync data to SD card
static void fsync_timer_callback(TimerHandle_t xTimer) {
    int fd = (int) pvTimerGetTimerID(xTimer);
    if (fd >= 0) {
        fsync(fd);
        ESP_LOGI(TAG, "fsync called on fd %d to ensure data is committed to SD card", fd);
    }
}

bool sdcard_writer_init(void)
{
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
    }

    // Create queues for L2 and CSI data
    l2_packet_queue = xQueueCreate(CONFIG_SNIFFER_PACKET_QUEUE_SIZE, sizeof(captured_packet_t));
    if (l2_packet_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create L2 packet queue");
        return false;
    }
    csi_packet_queue = xQueueCreate(CONFIG_SNIFFER_CSI_QUEUE_SIZE, sizeof(csi_packet_t));
    if (csi_packet_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create CSI packet queue");
        return false;
    }

    // Create writer tasks
    xTaskCreate(l2_writer_task, "l2_writer_task", 8192, NULL, 5, &l2_writer_task_handle);
    xTaskCreate(csi_writer_task, "csi_writer_task", 8192, NULL, 5, &csi_writer_task_handle);

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

    // Unmount SD card
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, NULL);
    ESP_LOGI(TAG, "SD card unmounted");

    // Release SPI bus
    spi_bus_free(SPI3_HOST);
}

// L2 writer task
static void l2_writer_task(void *pvParameter)
{
    FILE *file = fopen("/sdcard/l2.bin", "ab");
    int fd = fileno(file);
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open L2 capture file: %s", strerror(errno));
        vTaskDelete(NULL);
        return;
    }

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
    FILE *file = fopen("/sdcard/csi.bin", "ab");
    int fd = fileno(file);
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open CSI capture file: %s", strerror(errno));
        vTaskDelete(NULL);
        return;
    }

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
