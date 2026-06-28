#include "http_uploader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "battery.h"
#include "config.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "http_uploader";

#define BOUNDARY     "----taskhogboundaryK7Qf3z"
#define FILE_CHUNK   1024
#define HTTP_TIMEOUT 30000

esp_err_t http_uploader_init(void)
{
    return ESP_OK;
}

esp_err_t http_uploader_deinit(void)
{
    return ESP_OK;
}

static bool parse_str_field(const char *json, const char *key, char *out, size_t len)
{
    char pattern[40];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *p = strstr(json, pattern);
    if (p == NULL) {
        return false;
    }
    p += strlen(pattern);
    const char *end = strchr(p, '"');
    if (end == NULL) {
        return false;
    }
    size_t n = (size_t)(end - p);
    if (n >= len) {
        n = len - 1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

static esp_err_t build_metadata(const job_t *job, char *out, size_t len)
{
    char batt[12];
    battery_reading_t bat = {0};
    if (battery_read(&bat) == ESP_OK && bat.valid) {
        snprintf(batt, sizeof(batt), "%d", bat.percent);
    } else {
        strcpy(batt, "null");
    }

    int n = snprintf(out, len,
                     "{\"client_job_id\":\"%s\",\"device_id\":\"%s\","
                     "\"rtc_timestamp\":\"%s\",\"rtc_valid\":%s,"
                     "\"duration_s\":%.1f,\"battery_pct\":%s,\"fw_version\":\"%s\"}",
                     job->id, job->device_id, job->created_at_rtc,
                     job->rtc_valid ? "true" : "false",
                     (double)job->duration_s, batt, config_fw_version());
    return (n > 0 && (size_t)n < len) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t http_uploader_health(void)
{
    char url[160];
    snprintf(url, sizeof(url), "%s/v1/health", config_hub_url());

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "health falhou (err=%s status=%d)", esp_err_to_name(err), status);
    return ESP_FAIL;
}

esp_err_t http_uploader_upload(const job_t *job, char *out_recording_id, size_t recid_len)
{
    if (job == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(job->wav_path, &st) != 0 || st.st_size <= 0) {
        ESP_LOGE(TAG, "WAV ausente/vazio: %s", job->wav_path);
        return ESP_ERR_NOT_FOUND;
    }
    long fsize = (long)st.st_size;

    char meta[320];
    ESP_RETURN_ON_ERROR(build_metadata(job, meta, sizeof(meta)), TAG, "metadata");

    char prefix[640];
    int plen = snprintf(prefix, sizeof(prefix),
                        "--" BOUNDARY "\r\n"
                        "Content-Disposition: form-data; name=\"metadata\"\r\n"
                        "Content-Type: application/json\r\n\r\n"
                        "%s\r\n"
                        "--" BOUNDARY "\r\n"
                        "Content-Disposition: form-data; name=\"audio\"; filename=\"rec.wav\"\r\n"
                        "Content-Type: audio/wav\r\n\r\n",
                        meta);
    if (plen <= 0 || (size_t)plen >= sizeof(prefix)) {
        return ESP_ERR_INVALID_SIZE;
    }
    static const char suffix[] = "\r\n--" BOUNDARY "--\r\n";
    int slen = (int)(sizeof(suffix) - 1);
    int total = plen + (int)fsize + slen;

    char url[160];
    snprintf(url, sizeof(url), "%s/v1/recordings", config_hub_url());

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_FAIL;
    }

    char auth[160];
    snprintf(auth, sizeof(auth), "Bearer %s", config_device_token());
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type",
                               "multipart/form-data; boundary=" BOUNDARY);

    esp_err_t err = esp_http_client_open(client, total);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open falhou: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    FILE *f = NULL;
    char *buf = NULL;

    if (esp_http_client_write(client, prefix, plen) != plen) {
        err = ESP_FAIL;
        goto done;
    }

    f = fopen(job->wav_path, "rb");
    if (f == NULL) {
        err = ESP_ERR_NOT_FOUND;
        goto done;
    }
    buf = malloc(FILE_CHUNK);
    if (buf == NULL) {
        err = ESP_ERR_NO_MEM;
        goto done;
    }
    size_t rd;
    while ((rd = fread(buf, 1, FILE_CHUNK, f)) > 0) {
        if (esp_http_client_write(client, buf, rd) != (int)rd) {
            err = ESP_FAIL;
            goto done;
        }
    }

    if (esp_http_client_write(client, suffix, slen) != slen) {
        err = ESP_FAIL;
        goto done;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    char body[256];
    int bl = esp_http_client_read_response(client, body, sizeof(body) - 1);
    if (bl < 0) {
        bl = 0;
    }
    body[bl] = '\0';
    ESP_LOGI(TAG, "upload %s → status=%d", job->id, status);

    if (status == 200 || status == 201 || status == 202) {
        if (out_recording_id != NULL && recid_len > 0) {
            out_recording_id[0] = '\0';
            parse_str_field(body, "recording_id", out_recording_id, recid_len);
        }
        err = ESP_OK;
    } else {
        ESP_LOGW(TAG, "resposta inesperada: %s", body);
        err = ESP_FAIL;
    }

done:
    if (buf != NULL) {
        free(buf);
    }
    if (f != NULL) {
        fclose(f);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

esp_err_t http_uploader_poll_status(const char *recording_id,
                                    char *hub_status,
                                    size_t hub_status_len,
                                    char *hub_error,
                                    size_t hub_error_len)
{
    if (recording_id == NULL || recording_id[0] == '\0' || hub_status == NULL || hub_status_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    hub_status[0] = '\0';
    if (hub_error != NULL && hub_error_len > 0) {
        hub_error[0] = '\0';
    }

    char url[192];
    snprintf(url, sizeof(url), "%s/v1/recordings/%s", config_hub_url(), recording_id);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 12000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_FAIL;
    }

    char auth[160];
    snprintf(auth, sizeof(auth), "Bearer %s", config_device_token());
    esp_http_client_set_header(client, "Authorization", auth);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }
    esp_http_client_fetch_headers(client);
    int code = esp_http_client_get_status_code(client);
    char body[512];
    int bl = esp_http_client_read_response(client, body, sizeof(body) - 1);
    if (bl < 0) {
        bl = 0;
    }
    body[bl] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || code != 200) {
        ESP_LOGW(TAG, "poll %s falhou (err=%s http=%d)", recording_id,
                 esp_err_to_name(err), code);
        return ESP_FAIL;
    }

    if (!parse_str_field(body, "status", hub_status, hub_status_len)) {
        ESP_LOGW(TAG, "poll %s sem campo status", recording_id);
        return ESP_FAIL;
    }
    if (hub_error != NULL && hub_error_len > 0) {
        if (!parse_str_field(body, "error", hub_error, hub_error_len)) {
            hub_error[0] = '\0';
        } else if (strcmp(hub_error, "null") == 0) {
            hub_error[0] = '\0';
        }
    }
    ESP_LOGI(TAG, "poll %s → status=%s", recording_id, hub_status);
    return ESP_OK;
}
