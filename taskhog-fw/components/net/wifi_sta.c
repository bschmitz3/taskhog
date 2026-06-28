#include "wifi_sta.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "widgets.h"
#include "wifi_profiles.h"

static const char *TAG = "wifi_sta";

#define BIT_CONNECTED BIT0
#define BIT_FAIL      BIT1
#define MAX_RETRY     5
#define SCAN_MAX_AP   32

static EventGroupHandle_t s_events;
static esp_netif_t *s_netif;
static int s_retry;
static bool s_started;
static volatile bool s_connected;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        /* connect disparado explicitamente após scan/set_config */
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        widget_set_wifi_state(WIFI_DOWN);
        if (s_retry < MAX_RETRY) {
            s_retry++;
            esp_wifi_connect();
        } else {
            widget_set_wifi_state(WIFI_ERROR);
            xEventGroupSetBits(s_events, BIT_FAIL);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry = 0;
        s_connected = true;
        widget_set_wifi_state(WIFI_UP);
        xEventGroupSetBits(s_events, BIT_CONNECTED);
    }
}

static bool profile_matches(const wifi_profile_t *profiles, int count, const char *ssid, int *out_idx)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(profiles[i].ssid, ssid) == 0) {
            *out_idx = i;
            return true;
        }
    }
    return false;
}

static esp_err_t pick_best_network(
    const wifi_profile_t *profiles,
    int profile_count,
    wifi_profile_t *chosen)
{
    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
    };
    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan_cfg, true), TAG, "scan start");

    uint16_t ap_count = SCAN_MAX_AP;
    wifi_ap_record_t *aps = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (aps == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_wifi_scan_get_ap_records(&ap_count, aps);
    if (err != ESP_OK) {
        free(aps);
        return err;
    }

    int best_idx = -1;
    int best_rssi = -127;
    for (int i = 0; i < ap_count; i++) {
        int pidx = -1;
        if (!profile_matches(profiles, profile_count, (const char *)aps[i].ssid, &pidx)) {
            continue;
        }
        if (aps[i].rssi > best_rssi) {
            best_rssi = aps[i].rssi;
            best_idx = pidx;
        }
    }
    free(aps);

    if (best_idx < 0) {
        ESP_LOGW(TAG, "nenhuma rede conhecida no scan (%d APs visíveis)", ap_count);
        return ESP_ERR_NOT_FOUND;
    }

    *chosen = profiles[best_idx];
    ESP_LOGI(TAG, "scan: escolhida '%s' (RSSI %d)", chosen->ssid, best_rssi);
    return ESP_OK;
}

esp_err_t wifi_sta_init(void)
{
    if (s_events != NULL) {
        return ESP_OK;
    }

    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL),
        TAG, "wifi evt");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL),
        TAG, "ip evt");

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");
    return ESP_OK;
}

esp_err_t wifi_sta_connect(uint32_t timeout_ms)
{
    wifi_profile_t profiles[WIFI_PROFILE_MAX];
    int profile_count = wifi_profiles_load(profiles, WIFI_PROFILE_MAX);
    if (profile_count <= 0) {
        ESP_LOGW(TAG, "sem redes configuradas (wifi.cfg ou Kconfig)");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_connected) {
        return ESP_OK;
    }

    if (!s_started) {
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
        s_started = true;
    } else {
        esp_wifi_disconnect();
        s_connected = false;
    }

    wifi_profile_t chosen;
    esp_err_t err = pick_best_network(profiles, profile_count, &chosen);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, chosen.ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, chosen.psk, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = (chosen.psk[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wc), TAG, "set config");

    s_retry = 0;
    xEventGroupClearBits(s_events, BIT_CONNECTED | BIT_FAIL);
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "connect");

    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_CONNECTED | BIT_FAIL,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if (bits & BIT_CONNECTED) {
        wifi_profiles_set_last_ssid(chosen.ssid);
        ESP_LOGI(TAG, "conectado a '%s'", chosen.ssid);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "falha ao conectar a '%s'", chosen.ssid);
    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_sta_drop(void)
{
    s_connected = false;
    widget_set_wifi_state(WIFI_DOWN);
    if (s_started) {
        esp_wifi_disconnect();
    }
    return ESP_OK;
}

bool wifi_sta_is_connected(void)
{
    return s_connected;
}

esp_err_t wifi_sta_deinit(void)
{
    if (s_started) {
        esp_wifi_stop();
        s_started = false;
    }
    s_connected = false;
    return ESP_OK;
}
