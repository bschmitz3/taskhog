#pragma once

#include "esp_err.h"
#include "queue.h"

esp_err_t journal_init(void);
esp_err_t journal_deinit(void);

/** Append-only: job enfileirado. */
esp_err_t journal_log_enqueue(const char *job_id);

/** Append-only: transição de estado. */
esp_err_t journal_log_mark(const char *job_id, job_state_t state, const char *err);

/** Append-only: upload aceito pelo Hub. */
esp_err_t journal_log_uploaded(const char *job_id, const char *hub_recording_id);

/** Append-only: job movido para sent/. */
esp_err_t journal_log_complete(const char *job_id);

/** fsync do journal (chamar antes de deep sleep / shutdown). */
esp_err_t journal_flush(void);
