#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_pins.h"
#include "esp_err.h"

#define SDCARD_MOUNT_POINT BOARD_SD_MOUNT_POINT

esp_err_t sdcard_init(void);
esp_err_t sdcard_deinit(void);
bool sdcard_is_mounted(void);
