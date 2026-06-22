#include "m0_bringup_gate.h"

#include "esp_log.h"
#include "m0_rtc_gate.h"
#include "m0_sdcard_gate.h"

static const char *TAG = "m0_bringup";

esp_err_t m0_bringup_gate_run(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  M0 bring-up final — T6 SD + T8 RTC");
    ESP_LOGI(TAG, "========================================");

    esp_err_t sd_err = m0_sdcard_gate_run();
    if (sd_err != ESP_OK) {
        ESP_LOGE(TAG, "M0-T6 falhou: %s", esp_err_to_name(sd_err));
    }

    esp_err_t rtc_err = m0_rtc_gate_run();
    if (rtc_err != ESP_OK) {
        ESP_LOGE(TAG, "M0-T8 falhou: %s", esp_err_to_name(rtc_err));
    }

    if (sd_err == ESP_OK && rtc_err == ESP_OK) {
        ESP_LOGI(TAG, "M0 COMPLETE — T6+T8 OK (remover gate do boot após confirmar log)");
    } else {
        ESP_LOGE(TAG, "M0 INCOMPLETO — corrigir falhas acima antes de M1");
    }

    return (sd_err != ESP_OK) ? sd_err : rtc_err;
}
