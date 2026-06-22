#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

typedef enum {
    BOARD_BTN_BOOT = 0,
    BOARD_BTN_PWR,
    BOARD_BTN_COUNT,
} board_button_id_t;

esp_err_t board_buttons_init(void);
gpio_num_t board_button_gpio(board_button_id_t id);
const char *board_button_label(board_button_id_t id);
/** true = botão pressionado (active LOW). */
bool board_button_is_pressed(board_button_id_t id);
int board_button_level(board_button_id_t id);
