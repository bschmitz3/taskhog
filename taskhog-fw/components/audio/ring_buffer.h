#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    SemaphoreHandle_t lock;
    uint32_t overruns;
} audio_ring_t;

esp_err_t audio_ring_init(audio_ring_t *rb, size_t capacity_bytes);
void audio_ring_deinit(audio_ring_t *rb);
void audio_ring_reset(audio_ring_t *rb);

size_t audio_ring_write(audio_ring_t *rb, const void *src, size_t len);
size_t audio_ring_read(audio_ring_t *rb, void *dst, size_t len);
size_t audio_ring_available(const audio_ring_t *rb);
uint32_t audio_ring_overruns(const audio_ring_t *rb);
