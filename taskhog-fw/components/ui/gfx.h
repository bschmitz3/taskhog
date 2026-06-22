#pragma once

#include <stdint.h>

/* Fonte bitmap monoespaçada gerada de TTF (tools/gen_assets.py).
 * data: count * height * bytes_per_row bytes, MSB-first, 1 = tinta. */
typedef struct {
    uint8_t first;          /* primeiro código ASCII */
    uint8_t count;          /* nº de glifos */
    uint8_t width;          /* largura da célula (px) */
    uint8_t height;         /* altura da célula (px) */
    uint8_t advance;        /* avanço horizontal por caractere (px) */
    uint8_t bytes_per_row;  /* bytes por linha do glifo */
    const uint8_t *data;
} gfx_font_t;

/* Imagem 1-bit (mascote/ícones). data: height * bytes_per_row, MSB-first, 1 = tinta. */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t bytes_per_row;
    const uint8_t *data;
} gfx_image_t;
