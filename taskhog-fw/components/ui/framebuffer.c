#include "framebuffer.h"

#include <string.h>

#include "epaper_drv.h"
#include "esp_log.h"

static const char *TAG = "framebuffer";

esp_err_t framebuffer_init(void)
{
    ESP_LOGI(TAG, "canvas %dx%d", FB_W, FB_H);
    return ESP_OK;
}

esp_err_t framebuffer_deinit(void)
{
    return ESP_OK;
}

void fb_clear(bool white)
{
    epaper_drv_fill(white);
}

void fb_pixel(int x, int y, bool black)
{
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) {
        return;
    }
    epaper_drv_set_pixel((uint16_t)x, (uint16_t)y, black);
}

void fb_hline(int x, int y, int w, bool black)
{
    for (int i = 0; i < w; i++) {
        fb_pixel(x + i, y, black);
    }
}

void fb_vline(int x, int y, int h, bool black)
{
    for (int j = 0; j < h; j++) {
        fb_pixel(x, y + j, black);
    }
}

void fb_rect(int x, int y, int w, int h, bool black)
{
    fb_hline(x, y, w, black);
    fb_hline(x, y + h - 1, w, black);
    fb_vline(x, y, h, black);
    fb_vline(x + w - 1, y, h, black);
}

void fb_fill_rect(int x, int y, int w, int h, bool black)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            fb_pixel(x + i, y + j, black);
        }
    }
}

void fb_round_rect(int x, int y, int w, int h, int r, bool black)
{
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    fb_hline(x + r, y, w - 2 * r, black);
    fb_hline(x + r, y + h - 1, w - 2 * r, black);
    fb_vline(x, y + r, h - 2 * r, black);
    fb_vline(x + w - 1, y + r, h - 2 * r, black);

    int rr = r * r;
    for (int dy = 0; dy <= r; dy++) {
        for (int dx = 0; dx <= r; dx++) {
            int d = dx * dx + dy * dy;
            if (d <= rr && d > (r - 1) * (r - 1)) {
                fb_pixel(x + r - dx, y + r - dy, black);
                fb_pixel(x + w - 1 - r + dx, y + r - dy, black);
                fb_pixel(x + r - dx, y + h - 1 - r + dy, black);
                fb_pixel(x + w - 1 - r + dx, y + h - 1 - r + dy, black);
            }
        }
    }
}

static void draw_glyph(const gfx_font_t *f, int x, int y, char c, bool black)
{
    if ((uint8_t)c < f->first || (uint8_t)c >= f->first + f->count) {
        c = ' ';
    }
    int idx = (uint8_t)c - f->first;
    const uint8_t *g = &f->data[idx * f->height * f->bytes_per_row];
    for (int row = 0; row < f->height; row++) {
        const uint8_t *line = &g[row * f->bytes_per_row];
        for (int col = 0; col < f->width; col++) {
            if (line[col >> 3] & (1u << (7 - (col & 7)))) {
                fb_pixel(x + col, y + row, black);
            }
        }
    }
}

int fb_text_width(const gfx_font_t *f, const char *s)
{
    if (s == NULL) {
        return 0;
    }
    return (int)strlen(s) * f->advance;
}

int fb_text(const gfx_font_t *f, int x, int y, const char *s, bool black)
{
    if (s == NULL) {
        return 0;
    }
    int cx = x;
    for (const char *p = s; *p; p++) {
        draw_glyph(f, cx, y, *p, black);
        cx += f->advance;
    }
    return cx - x;
}

void fb_text_center(const gfx_font_t *f, int y, const char *s, bool black)
{
    int w = fb_text_width(f, s);
    int x = (FB_W - w) / 2;
    if (x < 0) {
        x = 0;
    }
    fb_text(f, x, y, s, black);
}

void fb_text_right(const gfx_font_t *f, int x_right, int y, const char *s, bool black)
{
    int w = fb_text_width(f, s);
    fb_text(f, x_right - w, y, s, black);
}

void fb_image(const gfx_image_t *img, int x, int y, bool black)
{
    for (int row = 0; row < img->height; row++) {
        const uint8_t *line = &img->data[row * img->bytes_per_row];
        for (int col = 0; col < img->width; col++) {
            if (line[col >> 3] & (1u << (7 - (col & 7)))) {
                fb_pixel(x + col, y + row, black);
            }
        }
    }
}

void fb_image_center(const gfx_image_t *img, int y, bool black)
{
    fb_image(img, (FB_W - img->width) / 2, y, black);
}
