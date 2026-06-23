#include "wifi_sta.h"

#include <string.h>

#include "config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "widgets.h"

static const char *TAG = "wifi_sta";

#define BIT_CONNECTED BIT0
#define BIT_FAIL      BIT1
#define MAX_RETRY     5

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
        esp_wifi_connect();
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
    const char *ssid = config_wifi_ssid();
    if (ssid == NULL || ssid[0] == '\0') {
        ESP_LOGW(TAG, "sem SSID configurado (Kconfig) — sync desligado");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_connected) {
        return ESP_OK;
    }

    const char *pass = config_wifi_password();
    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = (pass[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wc), TAG, "set config");

    s_retry = 0;
    xEventGroupClearBits(s_events, BIT_CONNECTED | BIT_FAIL);

    if (!s_started) {
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
        s_started = true;
    } else {
        esp_wifi_connect();
    }

    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_CONNECTED | BIT_FAIL,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if (bits & BIT_CONNECTED) {
        ESP_LOGI(TAG, "conectado a '%s'", ssid);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "falha ao conectar a '%s'", ssid);
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
