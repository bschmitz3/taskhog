#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_codec_dev.h"

#define AUDIO_CAPTURE_SAMPLE_RATE_HZ 16000
#define AUDIO_CAPTURE_BITS           16
#define AUDIO_CAPTURE_RING_BYTES     (128 * 1024)

esp_err_t audio_capture_init(void);
esp_err_t audio_capture_deinit(void);

esp_err_t audio_capture_open(void);
esp_err_t audio_capture_close(void);
esp_err_t audio_capture_read(void *buf, size_t len);

esp_err_t audio_capture_start(void);
esp_err_t audio_capture_stop(void);
bool audio_capture_is_running(void);

size_t audio_capture_ring_read(void *buf, size_t len);
size_t audio_capture_ring_available(void);
uint32_t audio_capture_ring_overruns(void);
size_t audio_capture_bytes_captured(void);

uint8_t audio_capture_channels(void);

esp_codec_dev_handle_t audio_capture_get_handle(void);
