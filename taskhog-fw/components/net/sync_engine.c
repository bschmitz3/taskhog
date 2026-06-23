#include "sync_engine.h"

#include <string.h>

#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_uploader.h"
#include "queue.h"
#include "wifi_sta.h"

static const char *TAG = "sync_engine";

#define SYNC_STACK_WORDS    8192
#define WIFI_CONNECT_MS     15000
#define MAX_PENDING_SNAPSHOT 16

static TaskHandle_t s_task;
static sync_done_cb_t s_done_cb;

void sync_engine_set_done_cb(sync_done_cb_t cb)
{
    s_done_cb = cb;
}

int sync_engine_drain(void)
{
    if (wifi_sta_connect(WIFI_CONNECT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "sem rede — fila mantida intacta");
        return 0;
    }
    if (http_uploader_health() != ESP_OK) {
        ESP_LOGW(TAG, "Hub inalcançável — fila mantida intacta");
        return 0;
    }

    char ids[MAX_PENDING_SNAPSHOT][24];
    int count = queue_list_pending(ids, MAX_PENDING_SNAPSHOT);
    if (count == 0) {
        ESP_LOGI(TAG, "nada pendente");
        return 0;
    }

    const int max_attempts = config_sync_max_attempts();
    int sent = 0;

    for (int i = 0; i < count; i++) {
        job_t job;
        if (queue_get(ids[i], &job) != ESP_OK) {
            continue;
        }
        if (job.state != JOB_STATE_QUEUED && job.state != JOB_STATE_ERROR) {
            continue;
        }

        int attempts_after = job.attempts + 1;
        queue_mark(ids[i], JOB_STATE_UPLOADING, NULL);  /* incrementa attempts */

        char recid[24] = {0};
        esp_err_t err = http_uploader_upload(&job, recid, sizeof(recid));
        if (err == ESP_OK) {
            queue_mark_uploaded(ids[i], recid);
            sent++;
            ESP_LOGI(TAG, "enviado %s → %s", ids[i], recid[0] ? recid : "?");
        } else if (attempts_after >= max_attempts) {
            queue_mark(ids[i], JOB_STATE_ERROR, "upload falhou");
            ESP_LOGW(TAG, "%s marcado ERROR após %d tentativas", ids[i], attempts_after);
        } else {
            queue_mark(ids[i], JOB_STATE_QUEUED, NULL);
            ESP_LOGW(TAG, "%s falhou (tentativa %d) — volta p/ fila", ids[i], attempts_after);
        }
    }

    ESP_LOGI(TAG, "drenagem concluída: %d/%d enviados", sent, count);
    return sent;
}

static void sync_task(void *arg)
{
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        sync_engine_drain();
        if (s_done_cb != NULL) {
            s_done_cb();
        }
    }
}

esp_err_t sync_engine_init(void)
{
    if (s_task != NULL) {
        return ESP_OK;
    }
    BaseType_t ok = xTaskCreate(sync_task, "tk_sync", SYNC_STACK_WORDS, NULL, 4, &s_task);
    if (ok != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t sync_engine_deinit(void)
{
    if (s_task != NULL) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    return ESP_OK;
}

void sync_engine_trigger(void)
{
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}
