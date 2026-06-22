#pragma once

#include "esp_err.h"

esp_err_t audio_beep_init(void);
/** Beep de início bloqueante — usar antes de abrir captura (ES8311 não mux playback+record). */
esp_err_t audio_beep_rec_start_blocking(void);
esp_err_t audio_beep_rec_start(void);
esp_err_t audio_beep_saved(void);
