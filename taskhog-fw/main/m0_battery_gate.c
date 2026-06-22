#include "m0_battery_gate.h"

#include "battery.h"
#include "board_pins.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <limits.h>

static const char *TAG = "m0_battery";

#define M0_BAT_SAMPLES 5

esp_err_t m0_battery_gate_run(void)
{
    ESP_LOGI(TAG, "=== Gate M0-T7: bateria BAT_ADC GPIO%d ===", BOARD_BAT_ADC_GPIO);

    ESP_RETURN_ON_ERROR(battery_init(), TAG, "battery init");

    int min_mv = INT32_MAX;
    int max_mv = 0;
    int last_pct = -1;

    for (int i = 0; i < M0_BAT_SAMPLES; i++) {
        battery_reading_t r = {0};
        ESP_RETURN_ON_ERROR(battery_read(&r), TAG, "battery read");

        if (r.voltage_mv < min_mv) {
            min_mv = r.voltage_mv;
        }
        if (r.voltage_mv > max_mv) {
            max_mv = r.voltage_mv;
        }
        last_pct = r.percent;

        ESP_LOGI(TAG, "Gate M0-T7 [%d/%d]: %d mV (%d%%) valid=%s",
                 i + 1, M0_BAT_SAMPLES, r.voltage_mv, r.percent,
                 r.valid ? "sim" : "NÃO");

        if (i + 1 < M0_BAT_SAMPLES) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    ESP_LOGI(TAG, "Resumo M0-T7: min=%d mV max=%d mV spread=%d mV",
             min_mv, max_mv, max_mv - min_mv);
    ESP_LOGI(TAG, "Divider ratio compilado: %.2f (R38/R21 200k/200k)", BOARD_BAT_DIVIDER_RATIO);
    ESP_LOGI(TAG, "Calibração: compare multímetro em VBAT vs log; ajuste BOARD_BAT_DIVIDER_RATIO");

    if (min_mv < 2800 || max_mv > 4300) {
        ESP_LOGW(TAG, "Gate M0-T7: tensão fora da faixa Li-Po — bateria conectada? só USB?");
    } else {
        ESP_LOGI(TAG, "Gate M0-T7: faixa Li-Po OK (~%d%%)", last_pct);
    }

    return ESP_OK;
}
