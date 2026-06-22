#include "m0_rtc_gate.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pcf85063.h"

#include <stdio.h>
#include <time.h>

static const char *TAG = "m0_rtc";

static void log_tm(const char *label, const struct tm *t)
{
    ESP_LOGI(TAG, "%s: %04d-%02d-%02d %02d:%02d:%02d wday=%d",
             label,
             t->tm_year + 1900,
             t->tm_mon + 1,
             t->tm_mday,
             t->tm_hour,
             t->tm_min,
             t->tm_sec,
             t->tm_wday);
}

static esp_err_t rtc_int_gpio_init(void)
{
    gpio_config_t cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << BOARD_RTC_INT_GPIO,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    return gpio_config(&cfg);
}

esp_err_t m0_rtc_gate_run(void)
{
    ESP_LOGI(TAG, "=== Gate M0-T8: RTC PCF85063 ===");

    bool os = rtc_check_os();
    ESP_LOGI(TAG, "Gate M0-T8: bit OS = %s", os ? "SET (parado)" : "clear (OK)");

    struct tm set_tm = {
        .tm_year = 126, /* 2026 */
        .tm_mon = 5,    /* junho */
        .tm_mday = 20,
        .tm_hour = 17,
        .tm_min = 45,
        .tm_sec = 0,
        .tm_wday = 6,   /* sábado */
    };
    ESP_RETURN_ON_ERROR(rtc_set(&set_tm), TAG, "rtc_set");
    log_tm("Gate M0-T8: gravado", &set_tm);

    struct tm read_tm = {0};
    ESP_RETURN_ON_ERROR(rtc_get(&read_tm), TAG, "rtc_get");
    log_tm("Gate M0-T8: lido", &read_tm);

    if (read_tm.tm_hour != set_tm.tm_hour || read_tm.tm_min != set_tm.tm_min ||
        read_tm.tm_mday != set_tm.tm_mday) {
        ESP_LOGE(TAG, "Gate M0-T8: set/get divergem");
        return ESP_FAIL;
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_RETURN_ON_ERROR(rtc_get(&read_tm), TAG, "rtc_get after wait");
    log_tm("Gate M0-T8: +3 s", &read_tm);
    if (read_tm.tm_sec < 2 || read_tm.tm_sec > 5) {
        ESP_LOGW(TAG, "Gate M0-T8: segundos após 3 s = %d (esperado ~3)", read_tm.tm_sec);
    } else {
        ESP_LOGI(TAG, "Gate M0-T8: relógio avançando OK");
    }

    ESP_RETURN_ON_ERROR(rtc_int_gpio_init(), TAG, "RTC_INT gpio");
    ESP_RETURN_ON_ERROR(rtc_alarm_clear_flag(), TAG, "clear AF");
    ESP_RETURN_ON_ERROR(rtc_alarm_enable(false), TAG, "alarm off");

    ESP_RETURN_ON_ERROR(rtc_get(&read_tm), TAG, "rtc_get for alarm");
    uint8_t alarm_sec = (uint8_t)((read_tm.tm_sec + 5) % 60);
    uint8_t alarm_min = (uint8_t)(read_tm.tm_min + ((read_tm.tm_sec + 5) >= 60 ? 1 : 0));
    alarm_min = (uint8_t)(alarm_min % 60);
    ESP_LOGI(TAG, "Gate M0-T8: alarme em %02u:%02u (INT GPIO%d)",
             alarm_min, alarm_sec, BOARD_RTC_INT_GPIO);
    ESP_RETURN_ON_ERROR(rtc_set_alarm_minute_second(alarm_min, alarm_sec), TAG, "set alarm");
    ESP_RETURN_ON_ERROR(rtc_alarm_enable(true), TAG, "alarm enable");

    uint8_t ctrl2 = 0;
    rtc_read_ctrl2(&ctrl2);
    ESP_LOGI(TAG, "Gate M0-T8: CTRL2 após enable = 0x%02X (AIE=%d AF=%d)",
             ctrl2, (ctrl2 & 0x80) != 0, (ctrl2 & 0x40) != 0);

    bool int_seen = false;
    bool af_seen = false;
    for (int i = 0; i < 800; i++) {
        if (gpio_get_level(BOARD_RTC_INT_GPIO) == 0) {
            int_seen = true;
        }
        if (rtc_alarm_flag()) {
            af_seen = true;
        }
        if (int_seen || af_seen) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Gate M0-T8: alarme INT=%s AF=%s",
             int_seen ? "LOW" : "não",
             af_seen ? "SET" : "clear");

    rtc_read_ctrl2(&ctrl2);
    ESP_LOGI(TAG, "Gate M0-T8: CTRL2 após wait = 0x%02X", ctrl2);

    ESP_RETURN_ON_ERROR(rtc_alarm_clear_flag(), TAG, "clear AF end");
    ESP_RETURN_ON_ERROR(rtc_alarm_enable(false), TAG, "alarm disable");

    ESP_RETURN_ON_ERROR(rtc_get(&read_tm), TAG, "rtc_get final");
    log_tm("Gate M0-T8: final", &read_tm);

    ESP_LOGI(TAG, "Gate M0-T8: desligue USB e religue para validar persistência");
    ESP_LOGI(TAG, "Gate M0-T8: hora deve permanecer ~2026-06-20 17:45:xx");

    return (int_seen || af_seen) ? ESP_OK : ESP_ERR_TIMEOUT;
}
