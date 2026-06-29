#include "board_power.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"

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

    /* Após deep sleep o hold pode persistir — liberar para uso normal dos GPIOs. */
    gpio_hold_dis(BOARD_VBAT_PWR_GPIO);

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

esp_err_t board_power_epd_off(void)
{
    gpio_set_level(BOARD_EPD_PWR_GPIO, 1);
    return ESP_OK;
}

esp_err_t board_power_audio_on(void)
{
    gpio_set_level(BOARD_AUDIO_PWR_GPIO, 0);
    ESP_LOGI(TAG, "Audio rail ON (GPIO%d LOW)", BOARD_AUDIO_PWR_GPIO);
    return ESP_OK;
}

esp_err_t board_power_audio_off(void)
{
    gpio_set_level(BOARD_AUDIO_PWR_GPIO, 1);
    return ESP_OK;
}

esp_err_t board_power_vbat_on(void)
{
    return board_power_hold_on();
}

esp_err_t board_power_sleep_latch(void)
{
    if (!s_inited) {
        ESP_RETURN_ON_ERROR(board_power_init(), TAG, "init");
    }
    gpio_set_level(BOARD_VBAT_PWR_GPIO, 1);
    esp_err_t err = gpio_hold_en(BOARD_VBAT_PWR_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_hold_en GPIO%d falhou: %s", BOARD_VBAT_PWR_GPIO, esp_err_to_name(err));
        return err;
    }
    gpio_deep_sleep_hold_en();
    ESP_LOGI(TAG, "BAT_Control GPIO%d latched HIGH (deep sleep)", BOARD_VBAT_PWR_GPIO);
    return ESP_OK;
}
