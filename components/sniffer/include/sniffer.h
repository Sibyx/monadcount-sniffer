#ifndef SNIFFER_H
#define SNIFFER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

void sniffer_init(void);
void sniffer_deinit(void);

void channel_hop_task(void *pvParameter);

#endif // SNIFFER_H
