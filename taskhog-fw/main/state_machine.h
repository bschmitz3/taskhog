#pragma once

#include "esp_err.h"
#include "events.h"

typedef enum {
    TASKHOG_STATE_BOOT = 0,
    TASKHOG_STATE_IDLE,
    TASKHOG_STATE_RECORDING,
    TASKHOG_STATE_FINALIZING,
    TASKHOG_STATE_CONFIRM,
    TASKHOG_STATE_SYNC,
    TASKHOG_STATE_PROVISION,
    TASKHOG_STATE_OTA,
    TASKHOG_STATE_SAFE_OFF,
} taskhog_state_t;

esp_err_t state_machine_init(void);
taskhog_state_t state_machine_get(void);
const char *state_machine_state_name(taskhog_state_t state);
esp_err_t state_machine_post_event(taskhog_event_id_t event, void *event_data);
