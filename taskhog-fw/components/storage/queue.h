#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    JOB_STATE_QUEUED = 0,
    JOB_STATE_UPLOADING,
    JOB_STATE_UPLOADED,
    JOB_STATE_PROCESSING,
    JOB_STATE_DONE,
    JOB_STATE_ERROR,
} job_state_t;

typedef struct {
    char client_job_id[24];
    char created_at_rtc[28];
    bool rtc_valid;
    float duration_s;
    int battery_pct;
    char device_id[20];
    char fw_version[16];
} recording_meta_t;

typedef struct {
    char id[24];
    char job_path[48];
    char wav_path[48];
    char created_at_rtc[28];
    bool rtc_valid;
    float duration_s;
    char device_id[20];
    job_state_t state;
    int attempts;
    char last_error[48];
    char hub_recording_id[24];
} job_t;

esp_err_t queue_init(void);
esp_err_t queue_deinit(void);

/** Gera client_job_id + paths 8.3 em /sdcard/queue/ (chamar ao iniciar REC). */
esp_err_t queue_prepare_capture(char *out_job_id,
                                size_t job_id_len,
                                char *out_wav_path,
                                size_t wav_path_len);

/** Cria .job após WAV finalizado (Spec 03 §3). */
esp_err_t queue_enqueue(const recording_meta_t *meta, const char *wav_path);

esp_err_t queue_peek_next(job_t *out);
esp_err_t queue_mark(const char *id, job_state_t state, const char *err);
esp_err_t queue_complete(const char *id);

/** Lê um job pelo id. */
esp_err_t queue_get(const char *id, job_t *out);

/** Marca UPLOADED e grava o hub_recording_id. */
esp_err_t queue_mark_uploaded(const char *id, const char *hub_recording_id);

/** Snapshot dos ids pendentes (QUEUED/ERROR) por ordem FIFO; retorna a contagem. */
int queue_list_pending(char ids[][24], int max);

int queue_pending_count(void);
int queue_error_count(void);

/** Contador em RAM (atualizado em enqueue/upload); evita I/O SD na UI. */
int queue_pending_hint(void);

/** Boot recovery (M5-T1): uploading/processing → queued. */
int queue_recover_stuck_jobs(void);

/** Boot recovery: WAV em queue/ sem .job correspondente. */
int queue_scan_orphan_wavs(void);

const char *queue_state_name(job_state_t state);
