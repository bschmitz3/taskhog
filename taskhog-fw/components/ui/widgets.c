#include "widgets.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "assets_fonts.h"
#include "assets_images.h"
#include "battery.h"
#include "framebuffer.h"
#include "queue.h"
#include "sdcard.h"

#define STATUS_Y 6
#define FOOTER_Y 178

static const char *WEEKDAY_PT[] = {"dom", "seg", "ter", "qua", "qui", "sex", "sab"};
static const char *MONTH_PT[] = {"jan", "fev", "mar", "abr", "mai", "jun",
                                 "jul", "ago", "set", "out", "nov", "dez"};

static volatile wifi_state_t s_wifi_state = WIFI_DOWN;

void widget_set_wifi_state(wifi_state_t state)
{
    s_wifi_state = state;
}

esp_err_t widgets_init(void)
{
    return ESP_OK;
}

esp_err_t widgets_deinit(void)
{
    return ESP_OK;
}

esp_err_t widget_read_status(widget_status_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->wifi = s_wifi_state;

    battery_reading_t bat = {0};
    if (battery_read(&bat) == ESP_OK && bat.valid) {
        out->battery_pct = bat.percent;
        out->battery_valid = true;
    }

    if (sdcard_is_mounted()) {
        out->queue_count = queue_pending_count();
    }
    return ESP_OK;
}

static const gfx_image_t *battery_icon(const widget_status_t *st)
{
    if (st->charging) {
        return &g_img_batt_charging;
    }
    if (!st->battery_valid) {
        return &g_img_batt_empty;
    }
    if (st->battery_pct > 70) {
        return &g_img_batt_full;
    }
    if (st->battery_pct > 35) {
        return &g_img_batt_mid;
    }
    if (st->battery_pct > 10) {
        return &g_img_batt_low;
    }
    return &g_img_batt_empty;
}

static const gfx_image_t *wifi_icon(const widget_status_t *st)
{
    switch (st->wifi) {
    case WIFI_UP: return &g_img_wifi;
    case WIFI_ERROR: return &g_img_wifi_error;
    default: return &g_img_no_wifi;
    }
}

void widget_status_bar(const widget_status_t *st)
{
    char buf[8];
    widget_format_clock(buf, sizeof(buf));
    fb_text(&g_font_sm, 8, STATUS_Y, buf, true);

    int x_right = 192;
    if (st != NULL && st->battery_valid) {
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", st->battery_pct);
        fb_text_right(&g_font_sm, x_right, STATUS_Y, pct, true);
        x_right -= fb_text_width(&g_font_sm, pct) + 4;
    }

    if (st != NULL) {
        const gfx_image_t *bat = battery_icon(st);
        fb_image(bat, x_right - bat->width, STATUS_Y, true);
        x_right -= bat->width + 5;

        const gfx_image_t *wf = wifi_icon(st);
        fb_image(wf, x_right - wf->width, STATUS_Y, true);
    }
}

void widget_footer(const char *text)
{
    if (text != NULL) {
        fb_text_center(&g_font_sm, FOOTER_Y, text, true);
    }
}

void widget_format_clock(char *buf, size_t len)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    snprintf(buf, len, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

void widget_format_date_subtitle(char *buf, size_t len)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    const char *wd = (tm.tm_wday >= 0 && tm.tm_wday <= 6) ? WEEKDAY_PT[tm.tm_wday] : "---";
    const char *mo = (tm.tm_mon >= 0 && tm.tm_mon <= 11) ? MONTH_PT[tm.tm_mon] : "---";
    snprintf(buf, len, "%s, %d %s", wd, tm.tm_mday, mo);
}

void widget_format_duration(int seconds, char *buf, size_t len)
{
    if (seconds < 0) {
        seconds = 0;
    }
    snprintf(buf, len, "%d:%02d", seconds / 60, seconds % 60);
}
