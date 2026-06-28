#include "journal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "board_pins.h"
#include "esp_log.h"
#include "queue.h"
#include "sdcard.h"

static const char *TAG = "journal";

#define JOURNAL_DIR  BOARD_SD_MOUNT_POINT "/journal"
/* FAT sem LFN (M0): extensão ≤3 chars — "queue.journal" falha no fopen. */
#define JOURNAL_PATH BOARD_SD_MOUNT_POINT "/journal/queue.jnl"

static FILE *s_journal;

static void log_fs_err(const char *op, const char *path)
{
    ESP_LOGE(TAG, "%s %s falhou — errno=%d (%s)", op, path, errno, strerror(errno));
}

static esp_err_t ensure_journal_dir(void)
{
    struct stat st;
    if (stat(JOURNAL_DIR, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            ESP_LOGE(TAG, "%s existe mas não é diretório", JOURNAL_DIR);
            return ESP_FAIL;
        }
        return ESP_OK;
    }
    if (mkdir(JOURNAL_DIR, 0755) != 0) {
        log_fs_err("mkdir", JOURNAL_DIR);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t open_journal_append(void)
{
    if (s_journal != NULL) {
        return ESP_OK;
    }
    s_journal = fopen(JOURNAL_PATH, "a");
    if (s_journal == NULL) {
        log_fs_err("fopen", JOURNAL_PATH);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t journal_append_line(const char *line)
{
    if (!sdcard_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ensure_journal_dir();
    if (err != ESP_OK) {
        return err;
    }
    err = open_journal_append();
    if (err != ESP_OK) {
        return err;
    }

    if (fputs(line, s_journal) == EOF || fputs("\n", s_journal) == EOF) {
        ESP_LOGE(TAG, "escrita journal falhou");
        return ESP_FAIL;
    }
    fflush(s_journal);
    fsync(fileno(s_journal));
    return ESP_OK;
}

static esp_err_t journal_recover(void)
{
    if (!sdcard_is_mounted()) {
        ESP_LOGW(TAG, "SD ausente — recovery adiada");
        return ESP_OK;
    }

    int stuck = queue_recover_stuck_jobs();
    int orphans = queue_scan_orphan_wavs();
    if (stuck > 0 || orphans > 0) {
        ESP_LOGW(TAG, "recovery boot: %d stuck, %d órfão(s) WAV", stuck, orphans);
    }
    return ESP_OK;
}

esp_err_t journal_init(void)
{
    if (!sdcard_is_mounted()) {
        ESP_LOGW(TAG, "SD não montado — journal adiado");
        return ESP_OK;
    }

    esp_err_t err = ensure_journal_dir();
    if (err != ESP_OK) {
        return err;
    }

    err = journal_recover();
    if (err != ESP_OK) {
        return err;
    }

    if (open_journal_append() != ESP_OK) {
        ESP_LOGW(TAG, "journal indisponível — recovery via .job continua");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "journal pronto (%s)", JOURNAL_PATH);
    return ESP_OK;
}

esp_err_t journal_deinit(void)
{
    if (s_journal != NULL) {
        fclose(s_journal);
        s_journal = NULL;
    }
    return ESP_OK;
}

esp_err_t journal_log_enqueue(const char *job_id)
{
    if (job_id == NULL || job_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    char line[40];
    snprintf(line, sizeof(line), "E %s", job_id);
    return journal_append_line(line);
}

esp_err_t journal_log_mark(const char *job_id, job_state_t state, const char *err)
{
    if (job_id == NULL || job_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    char line[80];
    if (state == JOB_STATE_ERROR && err != NULL && err[0] != '\0') {
        snprintf(line, sizeof(line), "M %s error", job_id);
    } else {
        snprintf(line, sizeof(line), "M %s %s", job_id, queue_state_name(state));
    }
    return journal_append_line(line);
}

esp_err_t journal_log_uploaded(const char *job_id, const char *hub_recording_id)
{
    if (job_id == NULL || job_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    char line[56];
    if (hub_recording_id != NULL && hub_recording_id[0] != '\0') {
        snprintf(line, sizeof(line), "U %s %s", job_id, hub_recording_id);
    } else {
        snprintf(line, sizeof(line), "U %s", job_id);
    }
    return journal_append_line(line);
}

esp_err_t journal_log_complete(const char *job_id)
{
    if (job_id == NULL || job_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    char line[40];
    snprintf(line, sizeof(line), "C %s", job_id);
    return journal_append_line(line);
}

esp_err_t journal_flush(void)
{
    if (s_journal == NULL) {
        return ESP_OK;
    }
    fflush(s_journal);
    fsync(fileno(s_journal));
    return ESP_OK;
}
