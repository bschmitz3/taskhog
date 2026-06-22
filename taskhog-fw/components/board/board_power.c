#include "board_power.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "board_power";
static bool s_inited;

esp_err_t board_power_hold_on(void)
{
    if (!s_inited) {
        ESP_RETURN_ON_ERROR(board_power_init(), TAG, "init");
    }
    gpio_set_level(BOARD_VBAT_PWR_GPIO, 1);
    return ESP_OK;
}

esp_err_t board_power_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    gpio_config_t cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BOARD_EPD_PWR_GPIO) |
                        (1ULL << BOARD_AUDIO_PWR_GPIO) |
                        (1ULL << BOARD_VBAT_PWR_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    /*
     * Waveshare V2: GPIO17 = BAT_Control (power latch, active HIGH).
     * Manter HIGH desde o boot — senão a placa desliga ao soltar PWR ou sob carga.
     * GPIO6/42 active LOW = rail OFF até board_power_*_on().
     */
    gpio_set_level(BOARD_VBAT_PWR_GPIO, 1);
    gpio_set_level(BOARD_EPD_PWR_GPIO, 1);
    gpio_set_level(BOARD_AUDIO_PWR_GPIO, 1);

    s_inited = true;
    ESP_LOGI(TAG, "Power GPIOs (EPD=%d Audio=%d BAT_Control=%d HIGH)",
             BOARD_EPD_PWR_GPIO, BOARD_AUDIO_PWR_GPIO, BOARD_VBAT_PWR_GPIO);
    return ESP_OK;
}

esp_err_t board_power_epd_on(void)
{
    gpio_set_level(BOARD_EPD_PWR_GPIO, 0);
    return ESP_OK;
}

esp_err_t board_power_audio_on(void)
{
    gpio_set_level(BOARD_AUDIO_PWR_GPIO, 0);
    ESP_LOGI(TAG, "Audio rail ON (GPIO%d LOW)", BOARD_AUDIO_PWR_GPIO);
    return ESP_OK;
}

esp_err_t board_power_vbat_on(void)
{
    return board_power_hold_on();
}
