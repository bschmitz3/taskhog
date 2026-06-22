#include "rec_worker.h"

#include "audio_beep.h"
#include "audio_capture.h"
#include "battery.h"
#include "config.h"
#include "esp_log.h"
#include "events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pcf85063.h"
#include "queue.h"
#include "rec_session.h"
#include "screens.h"
#include "sdcard.h"
#include "state_machine.h"
#include "wav_writer.h"

#include <string.h>
#include <time.h>

static const char *TAG = "rec_worker";

#define WORKER_STACK_WORDS 8192
#define WORKER_QUEUE_LEN   4

typedef enum {
    WORK_RECORDING = 0,
    WORK_FINALIZING,
    WORK_CONFIRM,
} work_cmd_t;

static QueueHandle_t s_work_queue;
static char s_cap_job_id[24];
static char s_cap_wav_path[64];

static void format_iso8601_brt(char *buf, size_t len)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%S", &tm);
    size_t used = strlen(buf);
    if (used + 7 < len) {
        snprintf(buf + used, len - used, "-03:00");
    }
}

static float pcm_duration_s(size_t pcm_bytes)
{
    const float bytes_per_sec = (float)AUDIO_CAPTURE_SAMPLE_RATE_HZ
                                * (float)AUDIO_CAPTURE_BITS / 8.0f
                                * (float)audio_capture_channels();
    if (bytes_per_sec <= 0.0f) {
        return 0.0f;
    }
    return (float)pcm_bytes / bytes_per_sec;
}

static void handle_recording(void)
{
    const uint32_t session = rec_session_current();
    s_cap_job_id[0] = '\0';
    s_cap_wav_path[0] = '\0';

    if (!sdcard_is_mounted()) {
        ESP_LOGE(TAG, "SD não montado — inserir cartão FAT32");
        state_machine_post_event(TASKHOG_EVENT_ERROR, NULL);
        return;
    }
    if (!rec_session_active(session)) {
        ESP_LOGI(TAG, "gravação cancelada (toque curto)");
        return;
    }
    if (queue_prepare_capture(s_cap_job_id, sizeof(s_cap_job_id),
                              s_cap_wav_path, sizeof(s_cap_wav_path)) != ESP_OK) {
        ESP_LOGE(TAG, "queue_prepare_capture falhou");
        state_machine_post_event(TASKHOG_EVENT_ERROR, NULL);
        return;
    }
    if (!rec_session_active(session)) {
        ESP_LOGI(TAG, "gravação cancelada após preparar fila");
        return;
    }
    if (audio_beep_rec_start_blocking() != ESP_OK) {
        ESP_LOGW(TAG, "beep início falhou (gravando sem som)");
    }
    if (!rec_session_active(session)) {
        ESP_LOGI(TAG, "gravação cancelada após beep");
        return;
    }
    if (wav_writer_session_start_with_path(s_cap_wav_path) != ESP_OK) {
        ESP_LOGE(TAG, "wav_writer_session_start falhou");
        state_machine_post_event(TASKHOG_EVENT_ERROR, NULL);
        return;
    }
    if (!rec_session_active(session)) {
        ESP_LOGI(TAG, "gravação cancelada após abrir WAV");
        wav_writer_session_abort();
        audio_capture_stop();
        return;
    }
    if (audio_capture_start() != ESP_OK) {
        ESP_LOGE(TAG, "audio_capture_start falhou");
        wav_writer_session_abort();
        state_machine_post_event(TASKHOG_EVENT_ERROR, NULL);
        return;
    }
    if (!rec_session_active(session)) {
        ESP_LOGI(TAG, "gravação cancelada após iniciar captura");
        audio_capture_stop();
        wav_writer_session_abort();
        return;
    }
}

static void handle_finalizing(void)
{
    audio_capture_stop();
    size_t pcm_bytes = 0;
    esp_err_t err = wav_writer_session_finalize(&pcm_bytes);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wav_writer_session_finalize falhou");
        wav_writer_session_abort();
        state_machine_post_event(TASKHOG_EVENT_ERROR, NULL);
        return;
    }

    const char *wav_path = wav_writer_session_path();
    ESP_LOGI(TAG, "gravado %s (%u bytes PCM)", wav_path ? wav_path : "?", (unsigned)pcm_bytes);

    if (s_cap_job_id[0] != '\0' && wav_path != NULL) {
        recording_meta_t meta = {0};
        strncpy(meta.client_job_id, s_cap_job_id, sizeof(meta.client_job_id) - 1);
        format_iso8601_brt(meta.created_at_rtc, sizeof(meta.created_at_rtc));
        meta.rtc_valid = rtc_is_valid();
        meta.duration_s = pcm_duration_s(pcm_bytes);
        strncpy(meta.device_id, config_device_id(), sizeof(meta.device_id) - 1);
        strncpy(meta.fw_version, config_fw_version(), sizeof(meta.fw_version) - 1);

        battery_reading_t bat = {0};
        if (battery_read(&bat) == ESP_OK && bat.valid) {
            meta.battery_pct = bat.percent;
        }

        if (queue_enqueue(&meta, wav_path) != ESP_OK) {
            ESP_LOGE(TAG, "queue_enqueue falhou — WAV sem .job");
            state_machine_post_event(TASKHOG_EVENT_ERROR, NULL);
            return;
        }
        screens_set_last_duration(meta.duration_s);
    }

    s_cap_job_id[0] = '\0';
    s_cap_wav_path[0] = '\0';
    state_machine_post_event(TASKHOG_EVENT_FINALIZE_DONE, NULL);
}

static void handle_confirm(void)
{
    if (audio_beep_saved() != ESP_OK) {
        ESP_LOGW(TAG, "beep salvo falhou");
    }
}

static void rec_worker_task(void *arg)
{
    (void)arg;
    work_cmd_t cmd;

    while (true) {
        if (xQueueReceive(s_work_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (cmd) {
        case WORK_RECORDING:
            handle_recording();
            break;
        case WORK_FINALIZING:
            handle_finalizing();
            break;
        case WORK_CONFIRM:
            handle_confirm();
            break;
        default:
            break;
        }
    }
}

esp_err_t rec_worker_init(void)
{
    if (s_work_queue != NULL) {
        return ESP_OK;
    }

    s_work_queue = xQueueCreate(WORKER_QUEUE_LEN, sizeof(work_cmd_t));
    if (s_work_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(rec_worker_task, "rec_worker", WORKER_STACK_WORDS, NULL, 5, NULL);
    if (ok != pdPASS) {
        vQueueDelete(s_work_queue);
        s_work_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void rec_worker_on_state_changed(taskhog_state_t state)
{
    if (s_work_queue == NULL) {
        return;
    }

    work_cmd_t cmd;
    switch (state) {
    case TASKHOG_STATE_RECORDING:
        cmd = WORK_RECORDING;
        break;
    case TASKHOG_STATE_FINALIZING:
        cmd = WORK_FINALIZING;
        break;
    case TASKHOG_STATE_CONFIRM:
        cmd = WORK_CONFIRM;
        break;
    default:
        return;
    }

    if (xQueueSend(s_work_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "fila cheia — descartando work %d", (int)cmd);
    }
}
