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

#define SYNC_STACK_WORDS       8192
#define WIFI_CONNECT_MS        15000
#define MAX_PENDING_SNAPSHOT   16
#define BACKOFF_BASE_MS        1000
#define BACKOFF_CAP_MS         60000
#define MAX_RETRY_SLOTS        16

typedef struct {
    char id[24];
    TickType_t not_before;
    bool used;
} retry_slot_t;

static TaskHandle_t s_task;
static sync_done_cb_t s_done_cb;
static retry_slot_t s_retry[MAX_RETRY_SLOTS];

static uint32_t backoff_delay_ms(int attempts)
{
    if (attempts <= 0) {
        return 0;
    }
    int shift = attempts - 1;
    if (shift > 10) {
        shift = 10;
    }
    uint32_t ms = (uint32_t)BACKOFF_BASE_MS << (unsigned)shift;
    return ms > BACKOFF_CAP_MS ? BACKOFF_CAP_MS : ms;
}

static retry_slot_t *retry_find(const char *id)
{
    for (int i = 0; i < MAX_RETRY_SLOTS; i++) {
        if (s_retry[i].used && strcmp(s_retry[i].id, id) == 0) {
            return &s_retry[i];
        }
    }
    return NULL;
}

static void retry_set(const char *id, int attempts)
{
    if (id == NULL || id[0] == '\0') {
        return;
    }

    retry_slot_t *slot = retry_find(id);
    if (slot == NULL) {
        for (int i = 0; i < MAX_RETRY_SLOTS; i++) {
            if (!s_retry[i].used) {
                slot = &s_retry[i];
                break;
            }
        }
    }
    if (slot == NULL) {
        ESP_LOGW(TAG, "tabela de backoff cheia — %s sem delay RAM", id);
        return;
    }

    uint32_t delay_ms = backoff_delay_ms(attempts);
    slot->used = true;
    strncpy(slot->id, id, sizeof(slot->id) - 1);
    slot->id[sizeof(slot->id) - 1] = '\0';
    slot->not_before = xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);
    ESP_LOGI(TAG, "%s backoff %lu ms (tentativa %d)", id, (unsigned long)delay_ms, attempts);
}

static void retry_clear(const char *id)
{
    retry_slot_t *slot = retry_find(id);
    if (slot != NULL) {
        slot->used = false;
    }
}

static bool retry_ready(const char *id, uint32_t *remaining_ms)
{
    retry_slot_t *slot = retry_find(id);
    if (slot == NULL) {
        return true;
    }

    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(slot->not_before - now) <= 0) {
        return true;
    }

    if (remaining_ms != NULL) {
        uint32_t ticks_left = (uint32_t)(slot->not_before - now);
        *remaining_ms = ticks_left * portTICK_PERIOD_MS;
    }
    return false;
}

static uint32_t next_retry_delay_ms(void)
{
    TickType_t now = xTaskGetTickCount();
    uint32_t best = 0;

    for (int i = 0; i < MAX_RETRY_SLOTS; i++) {
        if (!s_retry[i].used) {
            continue;
        }
        if ((int32_t)(s_retry[i].not_before - now) <= 0) {
            continue;
        }
        uint32_t ms = (uint32_t)(s_retry[i].not_before - now) * portTICK_PERIOD_MS;
        if (best == 0 || ms < best) {
            best = ms;
        }
    }
    return best;
}

static bool should_skip_job(const job_t *job, int max_attempts)
{
    if (job->state == JOB_STATE_ERROR && job->attempts >= max_attempts) {
        return true;
    }
    return false;
}

void sync_engine_set_done_cb(sync_done_cb_t cb)
{
    s_done_cb = cb;
}

int sync_engine_drain(void)
{
    /* Snapshot da fila ANTES de Wi-Fi/TLS — SDIO + RF concorrente falha leitura FAT. */
    char ids[MAX_PENDING_SNAPSHOT][24];
    int count = queue_list_pending(ids, MAX_PENDING_SNAPSHOT);
    if (count == 0) {
        ESP_LOGI(TAG, "nada pendente");
        return 0;
    }

    const int max_attempts = config_sync_max_attempts();
    int eligible = 0;
    for (int i = 0; i < count; i++) {
        job_t job;
        if (queue_get(ids[i], &job) != ESP_OK) {
            continue;
        }
        if (job.state != JOB_STATE_QUEUED && job.state != JOB_STATE_ERROR) {
            continue;
        }
        if (should_skip_job(&job, max_attempts)) {
            continue;
        }
        uint32_t remaining = 0;
        if (!retry_ready(ids[i], &remaining)) {
            ESP_LOGI(TAG, "%s em backoff (%lu ms)", ids[i], (unsigned long)remaining);
            continue;
        }
        eligible++;
    }

    if (eligible == 0) {
        ESP_LOGI(TAG, "%d pendente(s), nenhum pronto (backoff)", count);
        return 0;
    }

    if (wifi_sta_connect(WIFI_CONNECT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "sem rede — fila mantida intacta");
        return 0;
    }
    if (http_uploader_health() != ESP_OK) {
        ESP_LOGW(TAG, "Hub inalcançável — fila mantida intacta");
        wifi_sta_drop();
        return 0;
    }

    int sent = 0;

    for (int i = 0; i < count; i++) {
        job_t job;
        if (queue_get(ids[i], &job) != ESP_OK) {
            continue;
        }
        if (job.state != JOB_STATE_QUEUED && job.state != JOB_STATE_ERROR) {
            continue;
        }
        if (should_skip_job(&job, max_attempts)) {
            continue;
        }
        if (!retry_ready(ids[i], NULL)) {
            continue;
        }

        queue_mark(ids[i], JOB_STATE_UPLOADING, NULL);  /* incrementa attempts */
        if (queue_get(ids[i], &job) != ESP_OK) {
            continue;
        }

        char recid[24] = {0};
        esp_err_t err = http_uploader_upload(&job, recid, sizeof(recid));
        if (err == ESP_OK) {
            retry_clear(ids[i]);
            queue_mark_uploaded(ids[i], recid);
            sent++;
            ESP_LOGI(TAG, "enviado %s → %s", ids[i], recid[0] ? recid : "?");
        } else if (job.attempts >= max_attempts) {
            retry_clear(ids[i]);
            queue_mark(ids[i], JOB_STATE_ERROR, "upload falhou");
            ESP_LOGW(TAG, "%s marcado ERROR após %d tentativas", ids[i], job.attempts);
        } else {
            queue_mark(ids[i], JOB_STATE_QUEUED, NULL);
            retry_set(ids[i], job.attempts);
            ESP_LOGW(TAG, "%s falhou (tentativa %d/%d)", ids[i], job.attempts, max_attempts);
        }
    }

    ESP_LOGI(TAG, "drenagem concluída: %d/%d enviados", sent, eligible);
    wifi_sta_drop();
    return sent;
}

static void sync_task(void *arg)
{
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        for (;;) {
            sync_engine_drain();
            uint32_t delay_ms = next_retry_delay_ms();
            if (delay_ms == 0 || queue_pending_hint() <= 0) {
                break;
            }
            ESP_LOGI(TAG, "aguardando backoff %lu ms antes de re-tentar", (unsigned long)delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }

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
    memset(s_retry, 0, sizeof(s_retry));
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
