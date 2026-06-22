#include "config.h"

#include "esp_app_desc.h"
#include "esp_mac.h"
#include <stdio.h>

static char s_device_id[20];

esp_err_t config_init(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_id, sizeof(s_device_id), "taskhog-%02x%02x", mac[4], mac[5]);
    return ESP_OK;
}

esp_err_t config_deinit(void)
{
    return ESP_OK;
}

const char *config_device_id(void)
{
    return s_device_id[0] ? s_device_id : "taskhog-00";
}

const char *config_fw_version(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    if (app != NULL && app->version[0] != '\0') {
        return app->version;
    }
    return "0.1.0";
}
