#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "shared.h"

// WiFi
void management_wifi_init(void);
void management_wifi_deinit(void);

// Storage (SD Card)
bool sdcard_init(void);
bool sdcard_deinit(void);

// Helpers
bool management_obtain_time(void);
bool management_obtain_mac_addresses(void);
void upload_files_to_server(void);
void init_restart_timer(void);