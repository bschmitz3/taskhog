#include "wav_writer.h"

#include "audio_capture.h"
#include "board_pins.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "wav_writer";

#define FLUSH_EVERY_BYTES     8192
#define WRITER_READ_CHUNK     1024
#define TK_WRITER_STACK_WORDS 4096
#define TK_WRITER_PRIORITY    5

typedef struct __attribute__((packed)) {
    char riff[4];
    uint32_t chunk_size;
    char wave[4];
    char fmt_id[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_id[4];
    uint32_t data_size;
} wav_header_t;

static FILE *s_file;
static wav_format_t s_fmt;
static uint32_t s_pcm_bytes;
static uint32_t s_since_flush;

static char s_session_path[48];
static volatile bool s_writer_run;
static TaskHandle_t s_writer_task;
static SemaphoreHandle_t s_writer_done;
static uint16_t s_rec_seq;

static void fill_header(wav_header_t *h, uint32_t data_bytes)
{
    memcpy(h->riff, "RIFF", 4);
    h->chunk_size = 36 + data_bytes;
    memcpy(h->wave, "WAVE", 4);
    memcpy(h->fmt_id, "fmt ", 4);
    h->fmt_size = 16;
    h->audio_format = 1;
    h->num_channels = s_fmt.channels;
    h->sample_rate = s_fmt.sample_rate_hz;
    h->bits_per_sample = s_fmt.bits_per_sample;
    h->block_align = (uint16_t)(s_fmt.channels * (s_fmt.bits_per_sample / 8));
    h->byte_rate = s_fmt.sample_rate_hz * h->block_align;
    memcpy(h->data_id, "data", 4);
    h->data_size = data_bytes;
}

static void tk_writer(void *arg)
{
    (void)arg;

    uint8_t *buf = heap_caps_malloc(WRITER_READ_CHUNK, MALLOC_CAP_INTERNAL);
    if (buf == NULL) {
        ESP_LOGE(TAG, "tk_writer: sem memória");
        s_writer_run = false;
        if (s_writer_done != NULL) {
            xSemaphoreGive(s_writer_done);
        }
        s_writer_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "tk_writer started");

    while (s_writer_run || audio_capture_ring_available() > 0) {
        size_t n = audio_capture_ring_read(buf, WRITER_READ_CHUNK);
        if (n > 0) {
            if (wav_writer_write(buf, n) != ESP_OK) {
                ESP_LOGE(TAG, "falha ao escrever SD");
                break;
            }
        } else if (s_writer_run) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    free(buf);

    esp_err_t err = wav_writer_close();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tk_writer: close falhou");
    }

    ESP_LOGI(TAG, "tk_writer done");
    if (s_writer_done != NULL) {
        xSemaphoreGive(s_writer_done);
    }
    s_writer_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t wav_writer_init(void)
{
    if (s_writer_done == NULL) {
        s_writer_done = xSemaphoreCreateBinary();
        if (s_writer_done == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t wav_writer_deinit(void)
{
    wav_writer_session_abort();
    return ESP_OK;
}

esp_err_t wav_writer_open(const char *path, const wav_format_t *fmt)
{
    if (s_file != NULL) {
        wav_writer_close();
    }
    if (fmt == NULL || path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_fmt = *fmt;
    s_pcm_bytes = 0;
    s_since_flush = 0;

    s_file = fopen(path, "wb");
    if (s_file == NULL) {
        ESP_LOGE(TAG, "Não abriu %s", path);
        return ESP_FAIL;
    }

    wav_header_t header = {0};
    fill_header(&header, 0);
    if (fwrite(&header, sizeof(header), 1, s_file) != 1) {
        fclose(s_file);
        s_file = NULL;
        return ESP_FAIL;
    }
    fflush(s_file);

    ESP_LOGI(TAG, "WAV aberto: %s (%u Hz, %u ch, %u bit)",
             path, fmt->sample_rate_hz, fmt->channels, fmt->bits_per_sample);
    return ESP_OK;
}

esp_err_t wav_writer_write(const void *pcm, size_t bytes)
{
    if (s_file == NULL || pcm == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (fwrite(pcm, 1, bytes, s_file) != bytes) {
        return ESP_FAIL;
    }
    s_pcm_bytes += (uint32_t)bytes;
    s_since_flush += (uint32_t)bytes;
    if (s_since_flush >= FLUSH_EVERY_BYTES) {
        fflush(s_file);
        s_since_flush = 0;
    }
    return ESP_OK;
}

esp_err_t wav_writer_close(void)
{
    if (s_file == NULL) {
        return ESP_OK;
    }

    wav_header_t header;
    fill_header(&header, s_pcm_bytes);
    if (fseek(s_file, 0, SEEK_SET) != 0 ||
        fwrite(&header, sizeof(header), 1, s_file) != 1) {
        fclose(s_file);
        s_file = NULL;
        return ESP_FAIL;
    }
    fflush(s_file);
    fclose(s_file);
    s_file = NULL;
    s_since_flush = 0;

    ESP_LOGI(TAG, "WAV finalizado (%lu bytes PCM, header patch OK)",
             (unsigned long)s_pcm_bytes);
    return ESP_OK;
}

uint32_t wav_writer_pcm_bytes(void)
{
    return s_pcm_bytes;
}

esp_err_t wav_writer_session_start(void)
{
    s_rec_seq++;
    char path[64];
    snprintf(path, sizeof(path), BOARD_SD_MOUNT_POINT "/r%04u.wav", s_rec_seq);
    return wav_writer_session_start_with_path(path);
}

esp_err_t wav_writer_session_start_with_path(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_writer_task != NULL || s_file != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    strncpy(s_session_path, path, sizeof(s_session_path) - 1);
    s_session_path[sizeof(s_session_path) - 1] = '\0';

    wav_format_t fmt = {
        .sample_rate_hz = AUDIO_CAPTURE_SAMPLE_RATE_HZ,
        .channels = 1,
        .bits_per_sample = AUDIO_CAPTURE_BITS,
    };

    esp_err_t err = wav_writer_open(s_session_path, &fmt);
    if (err != ESP_OK) {
        s_session_path[0] = '\0';
        return err;
    }

    xSemaphoreTake(s_writer_done, 0);
    s_writer_run = true;

    BaseType_t ok = xTaskCreate(tk_writer, "tk_writer", TK_WRITER_STACK_WORDS, NULL,
                                TK_WRITER_PRIORITY, &s_writer_task);
    if (ok != pdPASS) {
        s_writer_run = false;
        wav_writer_close();
        s_session_path[0] = '\0';
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "sessão iniciada: %s", s_session_path);
    return ESP_OK;
}

esp_err_t wav_writer_session_finalize(size_t *out_pcm_bytes)
{
    if (s_writer_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_writer_run = false;
    if (xSemaphoreTake(s_writer_done, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "tk_writer timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (out_pcm_bytes != NULL) {
        *out_pcm_bytes = wav_writer_pcm_bytes();
    }
    return ESP_OK;
}

esp_err_t wav_writer_session_abort(void)
{
    if (s_writer_task != NULL) {
        s_writer_run = false;
        xSemaphoreTake(s_writer_done, pdMS_TO_TICKS(2000));
    }
    if (s_file != NULL) {
        fclose(s_file);
        s_file = NULL;
        remove(s_session_path);
    }
    s_session_path[0] = '\0';
    s_pcm_bytes = 0;
    return ESP_OK;
}

bool wav_writer_session_active(void)
{
    return s_writer_task != NULL;
}

const char *wav_writer_session_path(void)
{
    return s_session_path[0] ? s_session_path : NULL;
}
