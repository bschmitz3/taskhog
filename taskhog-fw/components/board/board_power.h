#pragma once

#include "esp_err.h"

esp_err_t board_power_init(void);
/** GPIO17 BAT_Control — manter HIGH para não desligar (bateria). */
esp_err_t board_power_hold_on(void);
esp_err_t board_power_epd_on(void);
esp_err_t board_power_epd_off(void);
esp_err_t board_power_audio_on(void);
esp_err_t board_power_audio_off(void);
esp_err_t board_power_vbat_on(void);
/** Mantém GPIO17 HIGH através do deep sleep (Waveshare BAT_Control). */
esp_err_t board_power_sleep_latch(void);
