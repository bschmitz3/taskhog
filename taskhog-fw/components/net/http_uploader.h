#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "queue.h"

esp_err_t http_uploader_init(void);
esp_err_t http_uploader_deinit(void);

/** GET {hub}/v1/health → ESP_OK se 200. */
esp_err_t http_uploader_health(void);

/**
 * POST {hub}/v1/recordings (multipart: metadata + audio), streaming do WAV do SD.
 * Em sucesso (200/202), grava o recording_id retornado em out_recording_id.
 */
esp_err_t http_uploader_upload(const job_t *job, char *out_recording_id, size_t recid_len);

/**
 * GET {hub}/v1/recordings/{id} — preenche status Hub (queued…done|error).
 * hub_error recebe mensagem se status=error (pode ser NULL).
 */
esp_err_t http_uploader_poll_status(const char *recording_id,
                                    char *hub_status,
                                    size_t hub_status_len,
                                    char *hub_error,
                                    size_t hub_error_len);
