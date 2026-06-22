#pragma once

#include "esp_err.h"

/** Poll de 15 s — pressione BOOT e PWR durante o monitor serial. */
esp_err_t m0_buttons_gate_run(void);
