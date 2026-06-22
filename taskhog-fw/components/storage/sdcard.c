#include "sdcard.h"

#include <stdio.h>

#include "board_pins.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sdcard";

static sdmmc_card_t *s_card;
static bool s_mounted;

esp_err_t sdcard_init(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 16,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = BOARD_SD_CLK_GPIO;
    slot.cmd = BOARD_SD_CMD_GPIO;
    slot.d0 = BOARD_SD_D0_GPIO;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(SDCARD_MOUNT_POINT, &host, &slot, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mount falhou (%s) — inserir cartão FAT32?", esp_err_to_name(err));
        return err;
    }

    s_mounted = true;
    fflush(stdout);
    sdmmc_card_print_info(stdout, s_card);

    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    if (esp_vfs_fat_info(SDCARD_MOUNT_POINT, &total_bytes, &free_bytes) == ESP_OK) {
        ESP_LOGI(TAG, "SD espaço: livre=%llu MB / total=%llu MB",
                 (unsigned long long)(free_bytes / (1024 * 1024)),
                 (unsigned long long)(total_bytes / (1024 * 1024)));
    }

    ESP_LOGI(TAG, "SD montado em %s (CLK=%d CMD=%d D0=%d)",
             SDCARD_MOUNT_POINT, BOARD_SD_CLK_GPIO, BOARD_SD_CMD_GPIO, BOARD_SD_D0_GPIO);
    ESP_LOGI(TAG, "Filesystem: FAT32 único recomendado (evitar exFAT / multi-partição)");
    return ESP_OK;
}

bool sdcard_is_mounted(void)
{
    return s_mounted && s_card != NULL;
}

esp_err_t sdcard_deinit(void)
{
    if (!s_mounted) {
        return ESP_OK;
    }
    esp_err_t err = esp_vfs_fat_sdcard_unmount(SDCARD_MOUNT_POINT, s_card);
    s_card = NULL;
    s_mounted = false;
    return err;
}
