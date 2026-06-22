#include "m0_sdcard_gate.h"

#include "esp_check.h"
#include "esp_log.h"
#include "sdcard.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "m0_sdcard";

#define M0_T6_PATH       SDCARD_MOUNT_POINT "/m0t6.bin"
#define M0_T6_PARTIAL    SDCARD_MOUNT_POINT "/m0t6prt.bin"
#define M0_T6_PAYLOAD    "TASKHOG-M0-T6-CRUD-OK"

static esp_err_t file_exists(const char *path, bool *exists)
{
    struct stat st;
    *exists = (stat(path, &st) == 0);
    return ESP_OK;
}

static void log_fopen_fail(const char *path, const char *mode)
{
    ESP_LOGE(TAG, "Gate M0-T6: fopen(%s, %s) falhou — errno=%d (%s)",
             path, mode, errno, strerror(errno));
}

esp_err_t m0_sdcard_gate_run(void)
{
    ESP_LOGI(TAG, "=== Gate M0-T6: microSD CRUD ===");

    if (!sdcard_is_mounted()) {
        ESP_LOGE(TAG, "Gate M0-T6: SD não montado");
        return ESP_ERR_INVALID_STATE;
    }

    struct stat root_st;
    if (stat(SDCARD_MOUNT_POINT, &root_st) != 0) {
        ESP_LOGE(TAG, "Gate M0-T6: stat(%s) falhou — errno=%d (%s)",
                 SDCARD_MOUNT_POINT, errno, strerror(errno));
        return ESP_FAIL;
    }

    unlink(M0_T6_PATH);
    unlink(M0_T6_PARTIAL);

    /* CREATE + WRITE */
    FILE *f = fopen(M0_T6_PATH, "wb");
    if (f == NULL) {
        log_fopen_fail(M0_T6_PATH, "wb");
        return ESP_FAIL;
    }
    size_t len = strlen(M0_T6_PAYLOAD);
    if (fwrite(M0_T6_PAYLOAD, 1, len, f) != len) {
        fclose(f);
        ESP_LOGE(TAG, "Gate M0-T6: fwrite falhou — errno=%d (%s)", errno, strerror(errno));
        return ESP_FAIL;
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    ESP_LOGI(TAG, "Gate M0-T6: CREATE+WRITE %u bytes", (unsigned)len);

    /* READ */
    f = fopen(M0_T6_PATH, "rb");
    if (f == NULL) {
        log_fopen_fail(M0_T6_PATH, "rb");
        return ESP_FAIL;
    }
    char buf[64] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n != len || memcmp(buf, M0_T6_PAYLOAD, len) != 0) {
        ESP_LOGE(TAG, "Gate M0-T6: READ diverge (n=%u)", (unsigned)n);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Gate M0-T6: READ OK");

    /* UPDATE (append) */
    f = fopen(M0_T6_PATH, "ab");
    if (f == NULL) {
        log_fopen_fail(M0_T6_PATH, "ab");
        return ESP_FAIL;
    }
    const char *suffix = "-v2";
    if (fwrite(suffix, 1, strlen(suffix), f) != strlen(suffix)) {
        fclose(f);
        return ESP_FAIL;
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    ESP_LOGI(TAG, "Gate M0-T6: UPDATE (append) OK");

    /* DELETE */
    if (unlink(M0_T6_PATH) != 0) {
        ESP_LOGE(TAG, "Gate M0-T6: unlink falhou — errno=%d (%s)", errno, strerror(errno));
        return ESP_FAIL;
    }
    bool exists = false;
    file_exists(M0_T6_PATH, &exists);
    if (exists) {
        ESP_LOGE(TAG, "Gate M0-T6: arquivo ainda existe após delete");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Gate M0-T6: DELETE OK");

    /* Escrita parcial + remoção (robustez FAT) */
    f = fopen(M0_T6_PARTIAL, "wb");
    if (f == NULL) {
        log_fopen_fail(M0_T6_PARTIAL, "wb");
        return ESP_FAIL;
    }
    uint8_t block[512];
    memset(block, 0xA5, sizeof(block));
    fwrite(block, 1, sizeof(block), f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    unlink(M0_T6_PARTIAL);
    file_exists(M0_T6_PARTIAL, &exists);
    ESP_LOGI(TAG, "Gate M0-T6: partial+unlink exists=%s", exists ? "sim" : "não");

    ESP_LOGI(TAG, "Gate M0-T6: CRUD completo — cartão pronto para fila/journal");
    return ESP_OK;
}
