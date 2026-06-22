#pragma once

#include "esp_err.h"

#define M0_GATE_RECORD_SECONDS 8
#define M0_GATE_WAV_PATH       "/sdcard/m0_gate.wav"

esp_err_t m0_audio_gate_run(void);
