#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "shared.h"

void management_wifi_init(void);
void management_wifi_deinit(void);
bool management_obtain_time(void);
bool management_obtain_mac_addresses(void);