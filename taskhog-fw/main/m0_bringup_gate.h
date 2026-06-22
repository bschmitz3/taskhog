#pragma once

#include "esp_err.h"

/** Gates pendentes M0: T6 (SD) + T8 (RTC). */
esp_err_t m0_bringup_gate_run(void);
