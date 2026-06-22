#include "board_buttons.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "board_buttons";

typedef struct {
    gpio_num_t gpio;
    const char *label;
    bool active_low;
} button_desc_t;

static const button_desc_t s_buttons[BOARD_BTN_COUNT] = {
    [BOARD_BTN_BOOT] = { BOARD_BOOT_BTN_GPIO, "BOOT", true },
    [BOARD_BTN_PWR] = { BOARD_PWR_BTN_GPIO, "PWR", true },
};

static bool s_inited;

esp_err_t board_buttons_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    uint64_t mask = 0;
    for (int i = 0; i < BOARD_BTN_COUNT; i++) {
        mask |= (1ULL << s_buttons[i].gpio);
    }

    gpio_config_t cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = mask,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    s_inited = true;
    for (int i = 0; i < BOARD_BTN_COUNT; i++) {
        ESP_LOGI(TAG, "%s GPIO%d nível=%d (%s)",
                 s_buttons[i].label,
                 s_buttons[i].gpio,
                 gpio_get_level(s_buttons[i].gpio),
                 board_button_is_pressed((board_button_id_t)i) ? "PRESSED" : "released");
    }
    return ESP_OK;
}

gpio_num_t board_button_gpio(board_button_id_t id)
{
    if (id >= BOARD_BTN_COUNT) {
        return GPIO_NUM_NC;
    }
    return s_buttons[id].gpio;
}

const char *board_button_label(board_button_id_t id)
{
    if (id >= BOARD_BTN_COUNT) {
        return "?";
    }
    return s_buttons[id].label;
}

int board_button_level(board_button_id_t id)
{
    if (id >= BOARD_BTN_COUNT || !s_inited) {
        return 1;
    }
    return gpio_get_level(s_buttons[id].gpio);
}

bool board_button_is_pressed(board_button_id_t id)
{
    if (id >= BOARD_BTN_COUNT || !s_inited) {
        return false;
    }
    const button_desc_t *b = &s_buttons[id];
    int level = gpio_get_level(b->gpio);
    return b->active_low ? (level == 0) : (level != 0);
}
