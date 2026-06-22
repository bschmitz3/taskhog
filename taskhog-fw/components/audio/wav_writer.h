#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    uint16_t sample_rate_hz;
    uint8_t channels;
    uint8_t bits_per_sample;
} wav_format_t;

esp_err_t wav_writer_init(void);
esp_err_t wav_writer_deinit(void);

esp_err_t wav_writer_open(const char *path, const wav_format_t *fmt);
esp_err_t wav_writer_write(const void *pcm, size_t bytes);
esp_err_t wav_writer_close(void);
uint32_t wav_writer_pcm_bytes(void);

/** Gera path 8.3 (`/sdcard/rNNNN.wav`) e inicia task tk_writer (ring → SD). */
esp_err_t wav_writer_session_start(void);
/** Inicia sessão gravando no path informado (ex. `/sdcard/queue/q0001.wav`). */
esp_err_t wav_writer_session_start_with_path(const char *path);
esp_err_t wav_writer_session_finalize(size_t *out_pcm_bytes);
esp_err_t wav_writer_session_abort(void);
bool wav_writer_session_active(void);
const char *wav_writer_session_path(void);
