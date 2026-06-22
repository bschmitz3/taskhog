#pragma once

#include "esp_err.h"

esp_err_t board_power_init(void);
/** GPIO17 BAT_Control — manter HIGH para não desligar (bateria). */
esp_err_t board_power_hold_on(void);
esp_err_t board_power_epd_on(void);
esp_err_t board_power_audio_on(void);
esp_err_t board_power_vbat_on(void);
