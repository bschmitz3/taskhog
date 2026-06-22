#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** Divisor VBAT→BAT_ADC: R38/R21 = 200k/200k → ratio 2.0 (calibrar M0-T7). */
#ifndef BOARD_BAT_DIVIDER_RATIO
#define BOARD_BAT_DIVIDER_RATIO 2.0f
#endif

typedef struct {
    int voltage_mv;
    int percent;
    bool valid;
} battery_reading_t;

esp_err_t battery_init(void);
esp_err_t battery_deinit(void);
esp_err_t battery_read(battery_reading_t *out);

/** Curva Li-Po 1S (interpolação linear entre pontos da spec). */
int battery_mv_to_percent(int voltage_mv);
