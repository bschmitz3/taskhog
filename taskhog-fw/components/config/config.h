#pragma once

#include "esp_err.h"

esp_err_t config_init(void);
esp_err_t config_deinit(void);

const char *config_device_id(void);
const char *config_fw_version(void);

/* Rede / Hub (M3) — valores de Kconfig (main/Kconfig.projbuild). */
const char *config_wifi_ssid(void);
const char *config_wifi_password(void);
const char *config_hub_url(void);
const char *config_device_token(void);
int config_sync_max_attempts(void);
