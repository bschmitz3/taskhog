#pragma once

#include "esp_err.h"
#include "state_machine.h"

esp_err_t rec_worker_init(void);

/** Enfileira trabalho pesado fora de sys_evt (não bloqueia). */
void rec_worker_on_state_changed(taskhog_state_t state);
