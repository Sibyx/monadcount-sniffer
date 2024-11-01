#ifndef SDCARD_WRITER_H
#define SDCARD_WRITER_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/queue.h"

// Function to initialize SD card and writer tasks
bool sdcard_writer_init(void);

// Function to deinitialize SD card and writer tasks
void sdcard_writer_deinit(void);

#endif // SDCARD_WRITER_H
