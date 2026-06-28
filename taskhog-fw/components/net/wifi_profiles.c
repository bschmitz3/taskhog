#include "wifi_profiles.h"

#include <stdlib.h>
#include <string.h>

#include "board_pins.h"
#include "config.h"
#include "esp_log.h"
#include "sdcard.h"

static const char *TAG = "wifi_prof";

#define WIFI_CFG_PATH BOARD_SD_MOUNT_POINT "/wifi.cfg"
#define WIFI_CFG_MAX  2048

static char s_last_ssid[33];

static bool parse_quoted_field(const char *json, const char *key, char *out, size_t out_len)
{
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *p = strstr(json, pattern);
    if (p == NULL) {
        snprintf(pattern, sizeof(pattern), "\"%s\": \"", key);
        p = strstr(json, pattern);
    }
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

static int parse_networks(const char *json, wifi_profile_t *out, size_t max_out)
{
    const char *p = strstr(json, "\"networks\"");
    if (p == NULL) {
        return 0;
    }
    p = strchr(p, '[');
    if (p == NULL) {
        return 0;
    }

    int count = 0;
    while (count < (int)max_out) {
        const char *ssid_key = strstr(p, "\"ssid\"");
        if (ssid_key == NULL) {
            break;
        }
        const char *obj_end = strchr(ssid_key, '}');
        if (obj_end == NULL) {
            break;
        }

        char block[192];
        size_t blen = (size_t)(obj_end - ssid_key + 1);
        if (blen >= sizeof(block)) {
            p = obj_end + 1;
            continue;
        }
        memcpy(block, ssid_key, blen);
        block[blen] = '\0';

        if (!parse_quoted_field(block, "ssid", out[count].ssid, sizeof(out[count].ssid))) {
            p = obj_end + 1;
            continue;
        }
        if (out[count].ssid[0] == '\0') {
            p = obj_end + 1;
            continue;
        }
        parse_quoted_field(block, "psk", out[count].psk, sizeof(out[count].psk));
        count++;
        p = obj_end + 1;
    }
    return count;
}

static int load_from_sd(wifi_profile_t *out, size_t max_out)
{
    if (!sdcard_is_mounted()) {
        return 0;
    }

    FILE *f = fopen(WIFI_CFG_PATH, "r");
    if (f == NULL) {
        return 0;
    }

    char *buf = malloc(WIFI_CFG_MAX);
    if (buf == NULL) {
        fclose(f);
        return 0;
    }

    size_t n = fread(buf, 1, WIFI_CFG_MAX - 1, f);
    fclose(f);
    buf[n] = '\0';

    int count = parse_networks(buf, out, max_out);
    free(buf);
    if (count > 0) {
        ESP_LOGI(TAG, "%d rede(s) em %s", count, WIFI_CFG_PATH);
    }
    return count;
}

static int load_kconfig_fallback(wifi_profile_t *out)
{
    const char *ssid = config_wifi_ssid();
    if (ssid == NULL || ssid[0] == '\0') {
        return 0;
    }
    strncpy(out->ssid, ssid, sizeof(out->ssid) - 1);
    out->ssid[sizeof(out->ssid) - 1] = '\0';
    const char *psk = config_wifi_password();
    if (psk != NULL) {
        strncpy(out->psk, psk, sizeof(out->psk) - 1);
        out->psk[sizeof(out->psk) - 1] = '\0';
    } else {
        out->psk[0] = '\0';
    }
    ESP_LOGI(TAG, "fallback Kconfig: '%s'", out->ssid);
    return 1;
}

int wifi_profiles_load(wifi_profile_t *out, size_t max_out)
{
    if (out == NULL || max_out == 0) {
        return 0;
    }

    int count = load_from_sd(out, max_out);
    if (count == 0) {
        count = load_kconfig_fallback(out);
    }
    return count;
}

const char *wifi_profiles_last_ssid(void)
{
    return s_last_ssid;
}

void wifi_profiles_set_last_ssid(const char *ssid)
{
    if (ssid == NULL) {
        s_last_ssid[0] = '\0';
        return;
    }
    strncpy(s_last_ssid, ssid, sizeof(s_last_ssid) - 1);
    s_last_ssid[sizeof(s_last_ssid) - 1] = '\0';
}
