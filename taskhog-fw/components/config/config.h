#pragma once

#include "esp_err.h"

esp_err_t config_init(void);
esp_err_t config_deinit(void);

const char *config_device_id(void);
const char *config_fw_version(void);
