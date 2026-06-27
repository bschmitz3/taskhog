#include "queue.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "board_pins.h"
#include "config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdcard.h"

static const char *TAG = "queue";

#define QUEUE_DIR  BOARD_SD_MOUNT_POINT "/queue"
#define SENT_DIR   BOARD_SD_MOUNT_POINT "/sent"
#define JOB_EXT    ".job"
#define WAV_EXT    ".wav"
#define PATH_MAX_LOCAL 80

static uint16_t s_seq;
static int s_pending_hint;
static SemaphoreHandle_t s_mutex;

#define RAM_PENDING_MAX 16

typedef struct {
    char id[24];
    char job_path[48];
    uint16_t seq;
} ram_pending_t;

static ram_pending_t s_ram[RAM_PENDING_MAX];
static int s_ram_count;

static void queue_lock(void)
{
    if (s_mutex != NULL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void queue_unlock(void)
{
    if (s_mutex != NULL) {
        xSemaphoreGive(s_mutex);
    }
}

static bool queue_join_path(char *out, size_t out_len, const char *dir, const char *name)
{
    if (name == NULL || strlen(name) > 12) {
        return false;
    }
    int n = snprintf(out, out_len, "%s/%s", dir, name);
    return n > 0 && (size_t)n < out_len;
}

static bool has_job_ext(const char *name)
{
    size_t len = strlen(name);
    return len >= 5 && strcasecmp(name + len - 4, ".job") == 0;
}

static bool parse_queue_seq(const char *name, uint16_t *out_seq)
{
    unsigned int n = 0;
    if (sscanf(name, "q%04u.job", &n) == 1 ||
        sscanf(name, "Q%04u.JOB", &n) == 1 ||
        sscanf(name, "q%04u.JOB", &n) == 1) {
        if (out_seq != NULL) {
            *out_seq = (uint16_t)n;
        }
        return true;
    }
    return false;
}

static void ram_pending_clear(void)
{
    s_ram_count = 0;
}

static void ram_pending_add(const job_t *job, uint16_t seq)
{
    if (job == NULL || s_ram_count >= RAM_PENDING_MAX) {
        return;
    }
    for (int i = 0; i < s_ram_count; i++) {
        if (strcmp(s_ram[i].id, job->id) == 0) {
            strncpy(s_ram[i].job_path, job->job_path, sizeof(s_ram[i].job_path) - 1);
            s_ram[i].seq = seq;
            return;
        }
    }
    strncpy(s_ram[s_ram_count].id, job->id, sizeof(s_ram[s_ram_count].id) - 1);
    strncpy(s_ram[s_ram_count].job_path, job->job_path, sizeof(s_ram[s_ram_count].job_path) - 1);
    s_ram[s_ram_count].seq = seq;
    s_ram_count++;
}

static void ram_pending_remove(const char *id)
{
    if (id == NULL) {
        return;
    }
    for (int i = 0; i < s_ram_count; i++) {
        if (strcmp(s_ram[i].id, id) == 0) {
            s_ram_count--;
            if (i < s_ram_count) {
                s_ram[i] = s_ram[s_ram_count];
            }
            return;
        }
    }
}

static esp_err_t read_job_file(const char *path, job_t *out);

static int ram_pending_list(char ids[][24], int max, uint16_t *seqs)
{
    int count = 0;
    for (int i = 0; i < s_ram_count && count < max; i++) {
        job_t job;
        if (read_job_file(s_ram[i].job_path, &job) != ESP_OK) {
            ESP_LOGW(TAG, "RAM idx: read falhou %s", s_ram[i].job_path);
            continue;
        }
        if (job.state != JOB_STATE_QUEUED && job.state != JOB_STATE_ERROR) {
            continue;
        }
        strncpy(ids[count], job.id, 23);
        ids[count][23] = '\0';
        if (seqs != NULL) {
            seqs[count] = s_ram[i].seq;
        }
        count++;
    }
    return count;
}

static esp_err_t ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_FAIL;
    }
    if (mkdir(path, 0755) != 0) {
        ESP_LOGE(TAG, "mkdir %s falhou", path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t generate_client_job_id(char *id, size_t len)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    uint8_t rnd = (uint8_t)(esp_random() & 0xFF);
    int n = snprintf(id, len, "%04d%02d%02d_%02d%02d%02d_%02x",
                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                       tm.tm_hour, tm.tm_min, tm.tm_sec, rnd);
    return (n > 0 && (size_t)n < len) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static const char *state_to_str(job_state_t state)
{
    switch (state) {
    case JOB_STATE_QUEUED: return "queued";
    case JOB_STATE_UPLOADING: return "uploading";
    case JOB_STATE_UPLOADED: return "uploaded";
    case JOB_STATE_PROCESSING: return "processing";
    case JOB_STATE_DONE: return "done";
    case JOB_STATE_ERROR: return "error";
    default: return "queued";
    }
}

static job_state_t str_to_state(const char *s)
{
    if (s == NULL) {
        return JOB_STATE_QUEUED;
    }
    if (strcmp(s, "uploading") == 0) {
        return JOB_STATE_UPLOADING;
    }
    if (strcmp(s, "uploaded") == 0) {
        return JOB_STATE_UPLOADED;
    }
    if (strcmp(s, "processing") == 0) {
        return JOB_STATE_PROCESSING;
    }
    if (strcmp(s, "done") == 0) {
        return JOB_STATE_DONE;
    }
    if (strcmp(s, "error") == 0) {
        return JOB_STATE_ERROR;
    }
    return JOB_STATE_QUEUED;
}

const char *queue_state_name(job_state_t state)
{
    return state_to_str(state);
}

static esp_err_t scan_max_seq(uint16_t *out_max)
{
    DIR *dir = opendir(QUEUE_DIR);
    if (dir == NULL) {
        *out_max = 0;
        return ESP_OK;
    }

    uint16_t max_seq = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        uint16_t n = 0;
        if (parse_queue_seq(ent->d_name, &n) && n > max_seq) {
            max_seq = n;
        }
        unsigned int w = 0;
        if (sscanf(ent->d_name, "q%04u.wav", &w) == 1 || sscanf(ent->d_name, "Q%04u.WAV", &w) == 1) {
            if ((uint16_t)w > max_seq) {
                max_seq = (uint16_t)w;
            }
        }
    }
    closedir(dir);
    *out_max = max_seq;
    return ESP_OK;
}

static esp_err_t write_job_file(const job_t *job)
{
    FILE *f = fopen(job->job_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "fopen %s falhou", job->job_path);
        return ESP_FAIL;
    }

    const char *err_json = "null";
    char err_quoted[64];
    if (job->last_error[0] != '\0') {
        snprintf(err_quoted, sizeof(err_quoted), "\"%s\"", job->last_error);
        err_json = err_quoted;
    }

    const char *hub_json = "null";
    char hub_quoted[32];
    if (job->hub_recording_id[0] != '\0') {
        snprintf(hub_quoted, sizeof(hub_quoted), "\"%s\"", job->hub_recording_id);
        hub_json = hub_quoted;
    }

    fprintf(f,
            "{\n"
            "  \"id\": \"%s\",\n"
            "  \"created_at_rtc\": \"%s\",\n"
            "  \"rtc_valid\": %s,\n"
            "  \"duration_s\": %.1f,\n"
            "  \"wav_path\": \"%s\",\n"
            "  \"device_id\": \"%s\",\n"
            "  \"state\": \"%s\",\n"
            "  \"attempts\": %d,\n"
            "  \"last_error\": %s,\n"
            "  \"hub_recording_id\": %s\n"
            "}\n",
            job->id,
            job->created_at_rtc,
            job->rtc_valid ? "true" : "false",
            (double)job->duration_s,
            job->wav_path,
            job->device_id,
            state_to_str(job->state),
            job->attempts,
            err_json,
            hub_json);

    fflush(f);
    fsync(fileno(f));
    fclose(f);
    return ESP_OK;
}

static bool parse_json_field(const char *json, const char *key, char *out, size_t out_len)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\": \"", key);
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
    if (n >= out_len) {
        n = out_len - 1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

static bool parse_json_float(const char *json, const char *key, float *out)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\": ", key);
    const char *p = strstr(json, pattern);
    if (p == NULL) {
        return false;
    }
    p += strlen(pattern);
    *out = strtof(p, NULL);
    return true;
}

static bool parse_json_int(const char *json, const char *key, int *out)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\": ", key);
    const char *p = strstr(json, pattern);
    if (p == NULL) {
        return false;
    }
    p += strlen(pattern);
    *out = (int)strtol(p, NULL, 10);
    return true;
}

static bool parse_json_bool(const char *json, const char *key, bool *out)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\": ", key);
    const char *p = strstr(json, pattern);
    if (p == NULL) {
        return false;
    }
    p += strlen(pattern);
    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static esp_err_t read_job_file(const char *path, job_t *out)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return ESP_FAIL;
    }

    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) {
        return ESP_FAIL;
    }
    buf[n] = '\0';

    memset(out, 0, sizeof(*out));
    strncpy(out->job_path, path, sizeof(out->job_path) - 1);

    parse_json_field(buf, "id", out->id, sizeof(out->id));
    parse_json_field(buf, "created_at_rtc", out->created_at_rtc, sizeof(out->created_at_rtc));
    parse_json_field(buf, "wav_path", out->wav_path, sizeof(out->wav_path));
    parse_json_field(buf, "device_id", out->device_id, sizeof(out->device_id));

    char state_buf[16];
    if (parse_json_field(buf, "state", state_buf, sizeof(state_buf))) {
        out->state = str_to_state(state_buf);
    }
    parse_json_bool(buf, "rtc_valid", &out->rtc_valid);
    parse_json_float(buf, "duration_s", &out->duration_s);
    parse_json_int(buf, "attempts", &out->attempts);
    parse_json_field(buf, "last_error", out->last_error, sizeof(out->last_error));
    if (strcmp(out->last_error, "null") == 0) {
        out->last_error[0] = '\0';
    }
    parse_json_field(buf, "hub_recording_id", out->hub_recording_id, sizeof(out->hub_recording_id));
    if (strcmp(out->hub_recording_id, "null") == 0) {
        out->hub_recording_id[0] = '\0';
    }

    return out->id[0] ? ESP_OK : ESP_FAIL;
}

static int count_jobs_in_state_nolock(job_state_t target)
{
    DIR *dir = opendir(QUEUE_DIR);
    if (dir == NULL) {
        return 0;
    }

    int count = 0;
    char path[PATH_MAX_LOCAL];
    job_t job;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!has_job_ext(ent->d_name)) {
            continue;
        }
        if (!queue_join_path(path, sizeof(path), QUEUE_DIR, ent->d_name)) {
            continue;
        }
        if (read_job_file(path, &job) == ESP_OK && job.state == target) {
            count++;
        }
    }
    closedir(dir);
    return count;
}

static esp_err_t find_job_by_id(const char *id, job_t *out)
{
    DIR *dir = opendir(QUEUE_DIR);
    if (dir == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    struct dirent *ent;
    char path[PATH_MAX_LOCAL];
    while ((ent = readdir(dir)) != NULL) {
        if (!has_job_ext(ent->d_name)) {
            continue;
        }
        if (!queue_join_path(path, sizeof(path), QUEUE_DIR, ent->d_name)) {
            continue;
        }
        if (read_job_file(path, out) == ESP_OK && strcmp(out->id, id) == 0) {
            closedir(dir);
            return ESP_OK;
        }
    }
    closedir(dir);

    for (int i = 0; i < s_ram_count; i++) {
        if (strcmp(s_ram[i].id, id) == 0) {
            return read_job_file(s_ram[i].job_path, out);
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t queue_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!sdcard_is_mounted()) {
        ESP_LOGW(TAG, "SD não montado — fila adiada");
        return ESP_OK;
    }

    queue_lock();
    esp_err_t err = ensure_dir(QUEUE_DIR);
    if (err != ESP_OK) {
        queue_unlock();
        return err;
    }
    err = ensure_dir(SENT_DIR);
    if (err != ESP_OK) {
        queue_unlock();
        return err;
    }

    uint16_t max_seq = 0;
    scan_max_seq(&max_seq);
    s_seq = max_seq;
    s_pending_hint = count_jobs_in_state_nolock(JOB_STATE_QUEUED)
                   + count_jobs_in_state_nolock(JOB_STATE_ERROR);
    ram_pending_clear();
    if (s_pending_hint > 0) {
        int rebuilt = 0;
        DIR *dir = opendir(QUEUE_DIR);
        if (dir != NULL) {
            struct dirent *ent;
            char path[PATH_MAX_LOCAL];
            job_t job;
            while ((ent = readdir(dir)) != NULL && rebuilt < RAM_PENDING_MAX) {
                uint16_t seq = 0;
                if (!parse_queue_seq(ent->d_name, &seq)) {
                    continue;
                }
                if (!queue_join_path(path, sizeof(path), QUEUE_DIR, ent->d_name)) {
                    continue;
                }
                if (read_job_file(path, &job) != ESP_OK) {
                    continue;
                }
                if (job.state == JOB_STATE_QUEUED || job.state == JOB_STATE_ERROR) {
                    ram_pending_add(&job, seq);
                    rebuilt++;
                }
            }
            closedir(dir);
        }
        if (rebuilt == 0 && s_pending_hint > 0) {
            ESP_LOGW(TAG, "init: hint=%d mas SD scan não listou .job — índice RAM vazio",
                     s_pending_hint);
            s_pending_hint = 0;
        }
    }
    queue_unlock();

    ESP_LOGI(TAG, "fila pronta (seq=%u, hint=%d)", (unsigned)s_seq, s_pending_hint);
    return ESP_OK;
}

esp_err_t queue_deinit(void)
{
    return ESP_OK;
}

esp_err_t queue_prepare_capture(char *out_job_id,
                                size_t job_id_len,
                                char *out_wav_path,
                                size_t wav_path_len)
{
    if (out_job_id == NULL || out_wav_path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sdcard_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    queue_lock();
    esp_err_t err = ensure_dir(QUEUE_DIR);
    if (err != ESP_OK) {
        queue_unlock();
        return err;
    }

    if (s_seq == 0) {
        uint16_t max_seq = 0;
        scan_max_seq(&max_seq);
        s_seq = max_seq;
    }

    s_seq++;
    err = generate_client_job_id(out_job_id, job_id_len);
    if (err != ESP_OK) {
        queue_unlock();
        return err;
    }

    int n = snprintf(out_wav_path, wav_path_len, "%s/q%04u%s", QUEUE_DIR, (unsigned)s_seq, WAV_EXT);
    queue_unlock();

    if (n <= 0 || (size_t)n >= wav_path_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "captura %s → %s", out_job_id, out_wav_path);
    return ESP_OK;
}

esp_err_t queue_enqueue(const recording_meta_t *meta, const char *wav_path)
{
    if (meta == NULL || wav_path == NULL || meta->client_job_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sdcard_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    queue_lock();

    struct stat st;
    if (stat(wav_path, &st) != 0 || st.st_size == 0) {
        ESP_LOGE(TAG, "WAV ausente: %s", wav_path);
        queue_unlock();
        return ESP_ERR_NOT_FOUND;
    }

    const char *base = strrchr(wav_path, '/');
    base = (base != NULL) ? base + 1 : wav_path;
    char job_path[64];
    snprintf(job_path, sizeof(job_path), "%s/%.*s%s",
             QUEUE_DIR, (int)(strlen(base) - strlen(WAV_EXT)), base, JOB_EXT);

    job_t job = {0};
    strncpy(job.id, meta->client_job_id, sizeof(job.id) - 1);
    strncpy(job.job_path, job_path, sizeof(job.job_path) - 1);
    strncpy(job.wav_path, wav_path, sizeof(job.wav_path) - 1);
    strncpy(job.created_at_rtc, meta->created_at_rtc, sizeof(job.created_at_rtc) - 1);
    strncpy(job.device_id, meta->device_id, sizeof(job.device_id) - 1);
    job.rtc_valid = meta->rtc_valid;
    job.duration_s = meta->duration_s;
    job.state = JOB_STATE_QUEUED;
    job.attempts = 0;

    esp_err_t err = write_job_file(&job);
    if (err == ESP_OK) {
        unsigned int seq_n = 0;
        if (sscanf(base, "q%04u.wav", &seq_n) != 1) {
            sscanf(base, "Q%04u.WAV", &seq_n);
        }
        ram_pending_add(&job, (uint16_t)seq_n);
        s_pending_hint++;
        ESP_LOGI(TAG, "enfileirado %s (%s) hint=%d ram=%d",
                 job.id, job.job_path, s_pending_hint, s_ram_count);
    }
    queue_unlock();
    return err;
}

esp_err_t queue_peek_next(job_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    queue_lock();

    DIR *dir = opendir(QUEUE_DIR);
    if (dir == NULL) {
        queue_unlock();
        return ESP_ERR_NOT_FOUND;
    }

    uint16_t best_seq = UINT16_MAX;
    char best_path[PATH_MAX_LOCAL] = {0};

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        uint16_t n = 0;
        if (!parse_queue_seq(ent->d_name, &n)) {
            continue;
        }
        if (n < best_seq) {
            best_seq = n;
            if (!queue_join_path(best_path, sizeof(best_path), QUEUE_DIR, ent->d_name)) {
                best_path[0] = '\0';
            }
        }
    }
    closedir(dir);

    if (best_path[0] == '\0') {
        queue_unlock();
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = ESP_OK;
    if (read_job_file(best_path, out) != ESP_OK) {
        err = ESP_FAIL;
    } else if (out->state != JOB_STATE_QUEUED && out->state != JOB_STATE_ERROR) {
        err = ESP_ERR_NOT_FOUND;
    }
    queue_unlock();
    return err;
}

esp_err_t queue_get(const char *id, job_t *out)
{
    if (id == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    queue_lock();
    esp_err_t err = find_job_by_id(id, out);
    queue_unlock();
    return err;
}

esp_err_t queue_mark_uploaded(const char *id, const char *hub_recording_id)
{
    queue_lock();
    job_t job;
    esp_err_t err = find_job_by_id(id, &job);
    if (err != ESP_OK) {
        queue_unlock();
        return err;
    }

    bool was_pending = (job.state == JOB_STATE_QUEUED || job.state == JOB_STATE_ERROR);
    job.state = JOB_STATE_UPLOADED;
    job.last_error[0] = '\0';
    if (hub_recording_id != NULL) {
        strncpy(job.hub_recording_id, hub_recording_id, sizeof(job.hub_recording_id) - 1);
        job.hub_recording_id[sizeof(job.hub_recording_id) - 1] = '\0';
    }
    err = write_job_file(&job);
    if (err == ESP_OK && was_pending && s_pending_hint > 0) {
        s_pending_hint--;
    }
    if (err == ESP_OK) {
        ram_pending_remove(id);
    }
    queue_unlock();
    return err;
}

int queue_list_pending(char ids[][24], int max)
{
    if (ids == NULL || max <= 0) {
        return 0;
    }

    queue_lock();

    DIR *dir = opendir(QUEUE_DIR);
    if (dir == NULL) {
        ESP_LOGW(TAG, "opendir %s falhou", QUEUE_DIR);
        queue_unlock();
        return 0;
    }

    uint16_t seqs[64];
    char tmp_ids[64][24];
    int cap = max < 64 ? max : 64;
    int count = 0;
    int jobs_seen = 0;
    int read_fail = 0;
    int wrong_state = 0;

    struct dirent *ent;
    char path[PATH_MAX_LOCAL];
    job_t job;
    while ((ent = readdir(dir)) != NULL && count < cap) {
        uint16_t n = 0;
        if (!parse_queue_seq(ent->d_name, &n)) {
            continue;
        }
        jobs_seen++;
        if (!queue_join_path(path, sizeof(path), QUEUE_DIR, ent->d_name)) {
            continue;
        }
        if (read_job_file(path, &job) != ESP_OK) {
            read_fail++;
            continue;
        }
        if (job.state != JOB_STATE_QUEUED && job.state != JOB_STATE_ERROR) {
            wrong_state++;
            continue;
        }
        seqs[count] = (uint16_t)n;
        strncpy(tmp_ids[count], job.id, sizeof(tmp_ids[count]) - 1);
        tmp_ids[count][sizeof(tmp_ids[count]) - 1] = '\0';
        count++;
    }
    closedir(dir);

    if (count == 0 && s_ram_count > 0) {
        count = ram_pending_list(tmp_ids, cap, seqs);
        if (count > 0) {
            ESP_LOGW(TAG, "list_pending: %d via índice RAM (SD scan=%d .job, hint=%d)",
                     count, jobs_seen, s_pending_hint);
        }
    } else if (count == 0 && s_pending_hint > 0) {
        ESP_LOGW(TAG, "list_pending: vazio (hint=%d ram=%d jobs_seen=%d read_fail=%d)",
                 s_pending_hint, s_ram_count, jobs_seen, read_fail);
    }

    /* Insertion sort por seq (FIFO). */
    for (int i = 1; i < count; i++) {
        uint16_t s = seqs[i];
        char id[24];
        strncpy(id, tmp_ids[i], sizeof(id));
        int j = i - 1;
        while (j >= 0 && seqs[j] > s) {
            seqs[j + 1] = seqs[j];
            strncpy(tmp_ids[j + 1], tmp_ids[j], sizeof(tmp_ids[j + 1]));
            j--;
        }
        seqs[j + 1] = s;
        strncpy(tmp_ids[j + 1], id, sizeof(tmp_ids[j + 1]));
    }

    for (int i = 0; i < count; i++) {
        strncpy(ids[i], tmp_ids[i], 24 - 1);
        ids[i][24 - 1] = '\0';
    }

    queue_unlock();

    if (count == 0 && jobs_seen > 0) {
        ESP_LOGW(TAG, "list_pending: 0 ok (%d .job, %d read_fail, %d wrong_state)",
                 jobs_seen, read_fail, wrong_state);
    } else if (count > 0) {
        ESP_LOGI(TAG, "list_pending: %d job(s) pendente(s)", count);
    }
    return count;
}

esp_err_t queue_mark(const char *id, job_state_t state, const char *err)
{
    queue_lock();
    job_t job;
    esp_err_t ret = find_job_by_id(id, &job);
    if (ret != ESP_OK) {
        queue_unlock();
        return ret;
    }

    job.state = state;
    if (err != NULL && err[0] != '\0') {
        strncpy(job.last_error, err, sizeof(job.last_error) - 1);
    } else if (state != JOB_STATE_ERROR) {
        job.last_error[0] = '\0';
    }
    if (state == JOB_STATE_UPLOADING) {
        job.attempts++;
    }

    ret = write_job_file(&job);
    queue_unlock();
    return ret;
}

esp_err_t queue_complete(const char *id)
{
    queue_lock();
    job_t job;
    esp_err_t ret = find_job_by_id(id, &job);
    if (ret != ESP_OK) {
        queue_unlock();
        return ret;
    }

    char sent_job[64];
    char sent_wav[64];
    const char *job_base = strrchr(job.job_path, '/');
    const char *wav_base = strrchr(job.wav_path, '/');
    job_base = job_base ? job_base + 1 : job.job_path;
    wav_base = wav_base ? wav_base + 1 : job.wav_path;

    snprintf(sent_job, sizeof(sent_job), "%s/%s", SENT_DIR, job_base);
    snprintf(sent_wav, sizeof(sent_wav), "%s/%s", SENT_DIR, wav_base);

    ret = ensure_dir(SENT_DIR);
    if (ret != ESP_OK) {
        queue_unlock();
        return ret;
    }

    if (rename(job.wav_path, sent_wav) != 0) {
        ESP_LOGW(TAG, "rename wav falhou — mantendo em queue");
    }
    if (rename(job.job_path, sent_job) != 0) {
        ESP_LOGW(TAG, "rename job falhou");
        queue_unlock();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "completo %s → sent/", id);
    queue_unlock();
    return ESP_OK;
}

int queue_pending_count(void)
{
    queue_lock();
    int n = count_jobs_in_state_nolock(JOB_STATE_QUEUED);
    queue_unlock();
    return n;
}

int queue_error_count(void)
{
    queue_lock();
    int n = count_jobs_in_state_nolock(JOB_STATE_ERROR);
    queue_unlock();
    return n;
}

int queue_pending_hint(void)
{
    return s_pending_hint;
}
