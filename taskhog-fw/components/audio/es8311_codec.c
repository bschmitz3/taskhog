#include "es8311_codec.h"

esp_err_t es8311_codec_init(void)
{
    /* Init real do codec ocorre em audio_capture_init() (M0-T4 / M1). */
    return ESP_OK;
}

esp_err_t es8311_codec_deinit(void)
{
    return ESP_OK;
}
