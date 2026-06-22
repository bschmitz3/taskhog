#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board_pins.h"
#include "esp_err.h"

#define EPAPER_WIDTH      BOARD_EPD_WIDTH
#define EPAPER_HEIGHT     BOARD_EPD_HEIGHT
#define EPAPER_BUF_BYTES  BOARD_EPD_BUF_BYTES

/** true = preto, false = branco */
esp_err_t epaper_drv_init(void);
esp_err_t epaper_drv_deinit(void);
bool epaper_drv_is_ready(void);

uint8_t *epaper_drv_buffer(void);
esp_err_t epaper_drv_fill(bool white);
esp_err_t epaper_drv_set_pixel(uint16_t x, uint16_t y, bool black);
esp_err_t epaper_drv_refresh_full(void);

/** Padrão de calibração de orientação (canto sólido topo-esq + "F" + seta). */
esp_err_t epaper_drv_draw_calibration(void);
