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

#ifndef CONFIG_TASKHOG_WIFI_SSID
#define CONFIG_TASKHOG_WIFI_SSID ""
#endif
#ifndef CONFIG_TASKHOG_WIFI_PASSWORD
#define CONFIG_TASKHOG_WIFI_PASSWORD ""
#endif
#ifndef CONFIG_TASKHOG_HUB_URL
#define CONFIG_TASKHOG_HUB_URL "https://hub.taskhog.win"
#endif
#ifndef CONFIG_TASKHOG_DEVICE_TOKEN
#define CONFIG_TASKHOG_DEVICE_TOKEN ""
#endif
#ifndef CONFIG_TASKHOG_SYNC_MAX_ATTEMPTS
#define CONFIG_TASKHOG_SYNC_MAX_ATTEMPTS 5
#endif

const char *config_wifi_ssid(void)
{
    return CONFIG_TASKHOG_WIFI_SSID;
}

const char *config_wifi_password(void)
{
    return CONFIG_TASKHOG_WIFI_PASSWORD;
}

const char *config_hub_url(void)
{
    return CONFIG_TASKHOG_HUB_URL;
}

const char *config_device_token(void)
{
    return CONFIG_TASKHOG_DEVICE_TOKEN;
}

int config_sync_max_attempts(void)
{
    return CONFIG_TASKHOG_SYNC_MAX_ATTEMPTS;
}
