#pragma once

#include "esp_err.h"

/** Valores espelham `taskhog_state_t` em main/state_machine.h. */
typedef enum {
    SCREEN_STATE_BOOT = 0,
    SCREEN_STATE_IDLE,
    SCREEN_STATE_RECORDING,
    SCREEN_STATE_FINALIZING,
    SCREEN_STATE_CONFIRM,
    SCREEN_STATE_SYNC,
    SCREEN_STATE_PROVISION,
    SCREEN_STATE_OTA,
    SCREEN_STATE_SAFE_OFF,
} screen_state_t;

esp_err_t screens_init(void);
esp_err_t screens_deinit(void);

void screens_on_state_changed(screen_state_t state);
void screens_set_last_duration(float duration_s);
