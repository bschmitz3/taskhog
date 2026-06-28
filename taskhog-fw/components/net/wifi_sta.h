#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t wifi_sta_init(void);
esp_err_t wifi_sta_deinit(void);

/** Conecta à melhor rede conhecida (scan + maior RSSI) e aguarda IP até timeout_ms. */
esp_err_t wifi_sta_connect(uint32_t timeout_ms);

/** Desassocia (mantém o driver inicializado). */
esp_err_t wifi_sta_drop(void);

bool wifi_sta_is_connected(void);
