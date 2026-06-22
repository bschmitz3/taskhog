#include "audio_beep.h"

#include "board_power.h"
#include "codec_init.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <math.h>

static const char *TAG = "audio_beep";

#define BEEP_SAMPLE_RATE_HZ 16000
#define BEEP_VOLUME         75.0f
#define BEEP_TASK_STACK     4096
#define BEEP_QUEUE_LEN      4

typedef enum {
    BEEP_CMD_REC_START = 0,
    BEEP_CMD_SAVED,
} beep_cmd_t;

static QueueHandle_t s_beep_queue;

static esp_err_t play_tone(uint16_t freq_hz, uint16_t duration_ms)
{
    esp_codec_dev_handle_t play = get_playback_handle();
    if (play == NULL) {
        ESP_LOGE(TAG, "playback handle nulo — codec init OK?");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(board_power_hold_on(), TAG, "power hold");
    ESP_RETURN_ON_ERROR(board_power_audio_on(), TAG, "audio rail");
    vTaskDelay(pdMS_TO_TICKS(50));

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = BEEP_SAMPLE_RATE_HZ,
        .channel = 1,
        .bits_per_sample = 16,
    };

    if (esp_codec_dev_open(play, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "playback open falhou");
        return ESP_FAIL;
    }

    esp_codec_dev_set_out_vol(play, BEEP_VOLUME);

    const size_t samples = (size_t)BEEP_SAMPLE_RATE_HZ * duration_ms / 1000;
    const size_t bytes = samples * sizeof(int16_t);
    int16_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL);
    if (buf == NULL) {
        esp_codec_dev_close(play);
        return ESP_ERR_NO_MEM;
    }

    const float omega = 2.0f * (float)M_PI * (float)freq_hz / (float)BEEP_SAMPLE_RATE_HZ;
    for (size_t i = 0; i < samples; i++) {
        float env = 1.0f;
        if (i < samples / 8) {
            env = (float)i / (float)(samples / 8);
        } else if (i > samples - samples / 8) {
            env = (float)(samples - i) / (float)(samples / 8);
        }
        buf[i] = (int16_t)(sinf(omega * (float)i) * 12000.0f * env);
    }

    esp_err_t err = ESP_OK;
    if (esp_codec_dev_write(play, buf, (int)bytes) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "playback write falhou");
        err = ESP_FAIL;
    }

    heap_caps_free(buf);
    esp_codec_dev_close(play);
    vTaskDelay(pdMS_TO_TICKS(10));
    return err;
}

static void beep_task(void *arg)
{
    (void)arg;
    beep_cmd_t cmd;

    while (true) {
        if (xQueueReceive(s_beep_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (cmd) {
        case BEEP_CMD_REC_START:
            ESP_LOGI(TAG, "beep início (880 Hz)");
            play_tone(880, 120);
            break;
        case BEEP_CMD_SAVED:
            ESP_LOGI(TAG, "beep duplo (salvo)");
            play_tone(988, 90);
            vTaskDelay(pdMS_TO_TICKS(80));
            play_tone(988, 90);
            break;
        default:
            break;
        }
    }
}

static esp_err_t queue_beep(beep_cmd_t cmd)
{
    if (s_beep_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_beep_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "fila de beep cheia — descartando");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t audio_beep_init(void)
{
    if (s_beep_queue != NULL) {
        return ESP_OK;
    }

    s_beep_queue = xQueueCreate(BEEP_QUEUE_LEN, sizeof(beep_cmd_t));
    if (s_beep_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(beep_task, "beep", BEEP_TASK_STACK, NULL, 5, NULL);
    if (ok != pdPASS) {
        vQueueDelete(s_beep_queue);
        s_beep_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t audio_beep_rec_start_blocking(void)
{
    ESP_LOGI(TAG, "beep início (880 Hz)");
    return play_tone(880, 120);
}

esp_err_t audio_beep_rec_start(void)
{
    return queue_beep(BEEP_CMD_REC_START);
}

esp_err_t audio_beep_saved(void)
{
    return queue_beep(BEEP_CMD_SAVED);
}
