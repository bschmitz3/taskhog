#include "m0_audio_gate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_capture.h"
#include "board_power.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdcard.h"
#include "wav_writer.h"

static const char *TAG = "m0_gate";

static size_t stereo_to_mono(const int16_t *in, size_t in_samples, int16_t *out)
{
    size_t frames = in_samples / 2;
    for (size_t i = 0; i < frames; i++) {
        out[i] = in[i * 2];
    }
    return frames;
}

esp_err_t m0_audio_gate_run(void)
{
    ESP_LOGI(TAG, "=== Gate M0-T4: gravação WAV ===");

    ESP_RETURN_ON_ERROR(board_power_init(), TAG, "power init");
    ESP_RETURN_ON_ERROR(board_power_audio_on(), TAG, "audio rail");
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_RETURN_ON_ERROR(audio_capture_init(), TAG, "codec init");
    ESP_RETURN_ON_ERROR(audio_capture_open(), TAG, "capture open");

    esp_err_t err = sdcard_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD não montado — inserir cartão FAT32 e reiniciar");
        audio_capture_close();
        return err;
    }

    wav_format_t fmt = {
        .sample_rate_hz = AUDIO_CAPTURE_SAMPLE_RATE_HZ,
        .channels = 1,
        .bits_per_sample = AUDIO_CAPTURE_BITS,
    };

    err = wav_writer_open(M0_GATE_WAV_PATH, &fmt);
    if (err != ESP_OK) {
        audio_capture_close();
        return err;
    }

    const size_t read_chunk = 1024;
    int16_t *read_buf = heap_caps_malloc(read_chunk, MALLOC_CAP_INTERNAL);
    int16_t *mono_buf = heap_caps_malloc(read_chunk / 2, MALLOC_CAP_INTERNAL);
    if (read_buf == NULL || mono_buf == NULL) {
        wav_writer_close();
        audio_capture_close();
        free(read_buf);
        free(mono_buf);
        return ESP_ERR_NO_MEM;
    }

    const bool stereo_in = audio_capture_channels() > 1;
    const int64_t end_us = esp_timer_get_time() + (int64_t)M0_GATE_RECORD_SECONDS * 1000000LL;
    size_t total_mono_bytes = 0;

    ESP_LOGI(TAG, "Gravando %d s — fale a ~30 cm do microfone...", M0_GATE_RECORD_SECONDS);

    while (esp_timer_get_time() < end_us) {
        if (audio_capture_read(read_buf, read_chunk) != ESP_OK) {
            continue;
        }
        size_t bytes;
        if (stereo_in) {
            size_t mono_samples = stereo_to_mono(read_buf, read_chunk / sizeof(int16_t), mono_buf);
            bytes = mono_samples * sizeof(int16_t);
            wav_writer_write(mono_buf, bytes);
        } else {
            bytes = read_chunk;
            wav_writer_write(read_buf, bytes);
        }
        total_mono_bytes += bytes;
    }

    wav_writer_close();
    audio_capture_close();

    /* Garantir persistência antes de remover o cartão. */
    FILE *probe = fopen(M0_GATE_WAV_PATH, "rb");
    if (probe) {
        fclose(probe);
        ESP_LOGI(TAG, "Arquivo verificado no SD: %s", M0_GATE_WAV_PATH);
    } else {
        ESP_LOGW(TAG, "Arquivo não encontrado após gravação — verificar FAT32");
    }

    free(read_buf);
    free(mono_buf);

    ESP_LOGI(TAG, "Gate M0-T4: WAV salvo em %s (%u bytes PCM)", M0_GATE_WAV_PATH, (unsigned)total_mono_bytes);
    ESP_LOGI(TAG, "Próximo passo: copiar WAV do SD para o Mac e validar G1–G5 (PRD §5.2)");
    return ESP_OK;
}
