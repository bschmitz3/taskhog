#include "m0_buttons_gate.h"

#include "board_buttons.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "m0_buttons";

#define M0_BTN_POLL_MS       20
#define M0_BTN_WINDOW_SEC    15

esp_err_t m0_buttons_gate_run(void)
{
    ESP_LOGI(TAG, "=== Gate M0-T9: mapeamento de botões ===");

    ESP_RETURN_ON_ERROR(board_buttons_init(), TAG, "buttons init");

    bool prev[BOARD_BTN_COUNT] = {0};
    for (int i = 0; i < BOARD_BTN_COUNT; i++) {
        prev[i] = board_button_is_pressed((board_button_id_t)i);
    }

    ESP_LOGI(TAG, "Pressione BOOT (GPIO%d) e PWR (GPIO%d) nos próximos %d s...",
             board_button_gpio(BOARD_BTN_BOOT),
             board_button_gpio(BOARD_BTN_PWR),
             M0_BTN_WINDOW_SEC);

    const int64_t end_us = esp_timer_get_time() + (int64_t)M0_BTN_WINDOW_SEC * 1000000LL;
    int press_count[BOARD_BTN_COUNT] = {0};

    while (esp_timer_get_time() < end_us) {
        for (int i = 0; i < BOARD_BTN_COUNT; i++) {
            bool now = board_button_is_pressed((board_button_id_t)i);
            if (now != prev[i]) {
                ESP_LOGI(TAG, "Gate M0-T9: %s GPIO%d %s",
                         board_button_label((board_button_id_t)i),
                         board_button_gpio((board_button_id_t)i),
                         now ? "PRESSED" : "RELEASED");
                if (now) {
                    press_count[i]++;
                }
                prev[i] = now;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(M0_BTN_POLL_MS));
    }

    ESP_LOGI(TAG, "Resumo M0-T9:");
    for (int i = 0; i < BOARD_BTN_COUNT; i++) {
        ESP_LOGI(TAG, "  %s GPIO%d: %d pressionamento(s) detectado(s)",
                 board_button_label((board_button_id_t)i),
                 board_button_gpio((board_button_id_t)i),
                 press_count[i]);
    }

    ESP_LOGI(TAG, "Mapeamento proposto (Waveshare V2 + demo):");
    ESP_LOGI(TAG, "  REC (push-to-talk) -> BOOT GPIO%d", board_button_gpio(BOARD_BTN_BOOT));
    ESP_LOGI(TAG, "  NAV / secundário   -> PWR  GPIO%d", board_button_gpio(BOARD_BTN_PWR));
    ESP_LOGI(TAG, "  Provisioning boot  -> BOOT segurado no power-on");
    ESP_LOGI(TAG, "Gate M0-T9: confirme no log que cada botão gerou PRESSED/RELEASED");

    return ESP_OK;
}
