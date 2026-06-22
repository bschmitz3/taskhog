#include "audio_capture.h"

#include "board_power.h"
#include "codec_board.h"
#include "codec_init.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ring_buffer.h"

static const char *TAG = "audio_capture";

#define TK_AUDIO_STACK_WORDS 4096
#define TK_AUDIO_PRIORITY    10
#define TK_AUDIO_CORE        1
#define READ_CHUNK_BYTES     1024

static esp_codec_dev_handle_t s_record;
static esp_codec_dev_handle_t s_playback;
static bool s_codec_open;
static uint8_t s_channels;

static audio_ring_t s_ring;
static bool s_ring_ready;

static TaskHandle_t s_audio_task;
static volatile bool s_capture_run;
static SemaphoreHandle_t s_stop_done;
static size_t s_bytes_captured;

static size_t stereo_to_mono(const int16_t *in, size_t in_samples, int16_t *out)
{
    size_t frames = in_samples / 2;
    for (size_t i = 0; i < frames; i++) {
        int32_t l = in[i * 2];
        int32_t r = in[i * 2 + 1];
        out[i] = (int16_t)((l + r) / 2);
    }
    return frames;
}

static void tk_audio(void *arg)
{
    (void)arg;

    int16_t *read_buf = heap_caps_malloc(READ_CHUNK_BYTES, MALLOC_CAP_INTERNAL);
    int16_t *mono_buf = heap_caps_malloc(READ_CHUNK_BYTES / 2, MALLOC_CAP_INTERNAL);
    if (read_buf == NULL || mono_buf == NULL) {
        ESP_LOGE(TAG, "tk_audio: sem memória interna");
        s_capture_run = false;
        if (s_stop_done != NULL) {
            xSemaphoreGive(s_stop_done);
        }
        vTaskDelete(NULL);
        return;
    }

    const bool stereo_in = s_channels > 1;
    ESP_LOGI(TAG, "tk_audio started (core %d)", TK_AUDIO_CORE);

    while (s_capture_run) {
        if (audio_capture_read(read_buf, READ_CHUNK_BYTES) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        const void *pcm = read_buf;
        size_t pcm_bytes = READ_CHUNK_BYTES;
        if (stereo_in) {
            size_t mono_samples = stereo_to_mono(read_buf, READ_CHUNK_BYTES / sizeof(int16_t), mono_buf);
            pcm = mono_buf;
            pcm_bytes = mono_samples * sizeof(int16_t);
        }

        size_t written = audio_ring_write(&s_ring, pcm, pcm_bytes);
        s_bytes_captured += written;
        if (written < pcm_bytes) {
            ESP_LOGW(TAG, "ring overrun (total=%lu)", (unsigned long)audio_ring_overruns(&s_ring));
        }
    }

    free(read_buf);
    free(mono_buf);
    ESP_LOGI(TAG, "tk_audio stopped (%u bytes captured)", (unsigned)s_bytes_captured);

    if (s_stop_done != NULL) {
        xSemaphoreGive(s_stop_done);
    }
    s_audio_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t audio_capture_init(void)
{
    set_codec_board_type("S3_ePaper_1_54");

    codec_init_cfg_t cfg = {
        .in_mode = CODEC_I2S_MODE_STD,
        .out_mode = CODEC_I2S_MODE_STD,
        .in_use_tdm = false,
        .reuse_dev = false,
    };

    if (init_codec(&cfg) != 0) {
        ESP_LOGE(TAG, "init_codec falhou");
        return ESP_FAIL;
    }

    s_playback = get_playback_handle();
    s_record = get_record_handle();
    if (s_record == NULL) {
        ESP_LOGE(TAG, "handle de gravação nulo");
        return ESP_FAIL;
    }

    if (!s_ring_ready) {
        esp_err_t err = audio_ring_init(&s_ring, AUDIO_CAPTURE_RING_BYTES);
        if (err != ESP_OK) {
            return err;
        }
        s_ring_ready = true;
    }

    if (s_stop_done == NULL) {
        s_stop_done = xSemaphoreCreateBinary();
        if (s_stop_done == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "ES8311 codec pronto (record=%p playback=%p)", s_record, s_playback);
    return ESP_OK;
}

esp_codec_dev_handle_t audio_capture_get_handle(void)
{
    return s_record;
}

esp_err_t audio_capture_open(void)
{
    if (s_record == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_codec_open) {
        return ESP_OK;
    }

    esp_codec_dev_set_in_gain(s_record, 30.0);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = AUDIO_CAPTURE_SAMPLE_RATE_HZ,
        .channel = 1,
        .bits_per_sample = AUDIO_CAPTURE_BITS,
    };

    if (esp_codec_dev_open(s_record, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Mono falhou — tentando stereo");
        fs.channel = 2;
        if (esp_codec_dev_open(s_record, &fs) != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "esp_codec_dev_open falhou");
            return ESP_FAIL;
        }
    }

    s_channels = fs.channel;
    s_codec_open = true;
    ESP_LOGI(TAG, "Captura aberta: %lu Hz, %u ch, %u bit",
             (unsigned long)fs.sample_rate, fs.channel, fs.bits_per_sample);
    return ESP_OK;
}

esp_err_t audio_capture_close(void)
{
    if (s_record != NULL && s_codec_open) {
        esp_codec_dev_close(s_record);
        s_codec_open = false;
    }
    return ESP_OK;
}

esp_err_t audio_capture_read(void *buf, size_t len)
{
    if (!s_codec_open || buf == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_read(s_record, buf, len) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

uint8_t audio_capture_channels(void)
{
    return s_channels;
}

esp_err_t audio_capture_start(void)
{
    if (s_capture_run) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(board_power_init(), TAG, "power init");
    ESP_RETURN_ON_ERROR(board_power_audio_on(), TAG, "audio rail");
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_RETURN_ON_ERROR(audio_capture_open(), TAG, "open");

    audio_ring_reset(&s_ring);
    s_bytes_captured = 0;
    xSemaphoreTake(s_stop_done, 0);

    s_capture_run = true;
    BaseType_t ok = xTaskCreatePinnedToCore(tk_audio,
                                            "tk_audio",
                                            TK_AUDIO_STACK_WORDS,
                                            NULL,
                                            TK_AUDIO_PRIORITY,
                                            &s_audio_task,
                                            TK_AUDIO_CORE);
    if (ok != pdPASS) {
        s_capture_run = false;
        audio_capture_close();
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "capture started");
    return ESP_OK;
}

esp_err_t audio_capture_stop(void)
{
    if (!s_capture_run && s_audio_task == NULL) {
        audio_capture_close();
        return ESP_OK;
    }

    s_capture_run = false;
    if (s_audio_task != NULL) {
        xSemaphoreTake(s_stop_done, pdMS_TO_TICKS(2000));
    }

    audio_capture_close();
    ESP_LOGI(TAG, "capture stopped (ring=%u bytes, overruns=%lu)",
             (unsigned)audio_ring_available(&s_ring),
             (unsigned long)audio_ring_overruns(&s_ring));
    return ESP_OK;
}

bool audio_capture_is_running(void)
{
    return s_capture_run;
}

size_t audio_capture_ring_read(void *buf, size_t len)
{
    return audio_ring_read(&s_ring, buf, len);
}

size_t audio_capture_ring_available(void)
{
    return audio_ring_available(&s_ring);
}

uint32_t audio_capture_ring_overruns(void)
{
    return audio_ring_overruns(&s_ring);
}

size_t audio_capture_bytes_captured(void)
{
    return s_bytes_captured;
}

esp_err_t audio_capture_deinit(void)
{
    audio_capture_stop();
    audio_capture_close();
    if (s_ring_ready) {
        audio_ring_deinit(&s_ring);
        s_ring_ready = false;
    }
    return ESP_OK;
}
