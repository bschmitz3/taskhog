#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "gfx.h"

#define FB_W 200
#define FB_H 200

esp_err_t framebuffer_init(void);
esp_err_t framebuffer_deinit(void);

void fb_clear(bool white);
void fb_pixel(int x, int y, bool black);
void fb_hline(int x, int y, int w, bool black);
void fb_vline(int x, int y, int h, bool black);
void fb_rect(int x, int y, int w, int h, bool black);
void fb_fill_rect(int x, int y, int w, int h, bool black);
void fb_round_rect(int x, int y, int w, int h, int r, bool black);

/* Texto. y = topo da célula. Retorna largura em px. */
int fb_text_width(const gfx_font_t *f, const char *s);
int fb_text(const gfx_font_t *f, int x, int y, const char *s, bool black);
void fb_text_center(const gfx_font_t *f, int y, const char *s, bool black);
void fb_text_right(const gfx_font_t *f, int x_right, int y, const char *s, bool black);

/* Imagens 1-bit. black=true desenha tinta como preto. */
void fb_image(const gfx_image_t *img, int x, int y, bool black);
void fb_image_center(const gfx_image_t *img, int y, bool black);
