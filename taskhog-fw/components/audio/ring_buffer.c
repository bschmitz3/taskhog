#include "ring_buffer.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "ring";

esp_err_t audio_ring_init(audio_ring_t *rb, size_t capacity_bytes)
{
    if (rb == NULL || capacity_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    rb->data = heap_caps_malloc(capacity_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rb->data == NULL) {
        ESP_LOGE(TAG, "PSRAM alloc %u bytes failed", (unsigned)capacity_bytes);
        return ESP_ERR_NO_MEM;
    }

    rb->lock = xSemaphoreCreateMutex();
    if (rb->lock == NULL) {
        heap_caps_free(rb->data);
        rb->data = NULL;
        return ESP_ERR_NO_MEM;
    }

    rb->capacity = capacity_bytes;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    rb->overruns = 0;

    ESP_LOGI(TAG, "ring %u bytes in PSRAM", (unsigned)capacity_bytes);
    return ESP_OK;
}

void audio_ring_deinit(audio_ring_t *rb)
{
    if (rb == NULL) {
        return;
    }
    if (rb->lock != NULL) {
        vSemaphoreDelete(rb->lock);
        rb->lock = NULL;
    }
    heap_caps_free(rb->data);
    rb->data = NULL;
    rb->capacity = 0;
    rb->count = 0;
}

void audio_ring_reset(audio_ring_t *rb)
{
    if (rb == NULL || rb->lock == NULL) {
        return;
    }
    xSemaphoreTake(rb->lock, portMAX_DELAY);
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    xSemaphoreGive(rb->lock);
}

size_t audio_ring_write(audio_ring_t *rb, const void *src, size_t len)
{
    if (rb == NULL || src == NULL || len == 0 || rb->data == NULL || rb->lock == NULL) {
        return 0;
    }

    const uint8_t *in = src;
    size_t written = 0;

    xSemaphoreTake(rb->lock, portMAX_DELAY);
    while (written < len) {
        if (rb->count >= rb->capacity) {
            rb->overruns++;
            break;
        }
        rb->data[rb->head] = in[written];
        rb->head = (rb->head + 1) % rb->capacity;
        rb->count++;
        written++;
    }
    xSemaphoreGive(rb->lock);

    return written;
}

size_t audio_ring_read(audio_ring_t *rb, void *dst, size_t len)
{
    if (rb == NULL || dst == NULL || len == 0 || rb->data == NULL || rb->lock == NULL) {
        return 0;
    }

    uint8_t *out = dst;
    size_t read = 0;

    xSemaphoreTake(rb->lock, portMAX_DELAY);
    while (read < len && rb->count > 0) {
        out[read] = rb->data[rb->tail];
        rb->tail = (rb->tail + 1) % rb->capacity;
        rb->count--;
        read++;
    }
    xSemaphoreGive(rb->lock);

    return read;
}

size_t audio_ring_available(const audio_ring_t *rb)
{
    if (rb == NULL) {
        return 0;
    }
    return rb->count;
}

uint32_t audio_ring_overruns(const audio_ring_t *rb)
{
    if (rb == NULL) {
        return 0;
    }
    return rb->overruns;
}
