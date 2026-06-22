#include "rec_button.h"

#include "audio_capture.h"
#include "board_buttons.h"
#include "board_power.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rec_session.h"
#include "state_machine.h"
#include "wav_writer.h"

static const char *TAG = "rec_btn";

#define REC_POLL_MS           20
#define REC_DEBOUNCE_MS       50
#define REC_MIN_HOLD_US       (500 * 1000LL)
#define REC_MAX_DURATION_US   (120LL * 1000000LL)

static int64_t s_press_us;
static bool s_max_sent;
static bool s_pwr_warned;

static void warn_pwr_pressed(void)
{
    if (board_button_is_pressed(BOARD_BTN_PWR) && !s_pwr_warned) {
        s_pwr_warned = true;
        ESP_LOGW(TAG, "PWR pressionado — nesta placa DESLIGA alimentação/LEDs. Use só BOOT (REC).");
    }
    if (!board_button_is_pressed(BOARD_BTN_PWR)) {
        s_pwr_warned = false;
    }
}

static void on_rec_press(void)
{
    taskhog_state_t st = state_machine_get();
    if (st != TASKHOG_STATE_IDLE) {
        return;
    }

    s_press_us = esp_timer_get_time();
    s_max_sent = false;
    rec_session_begin();
    ESP_LOGI(TAG, "REC press (BOOT GPIO%d)", board_button_gpio(BOARD_BTN_BOOT));
    state_machine_post_event(TASKHOG_EVENT_REC_PRESS, NULL);
}

static void on_rec_release(void)
{
    if (state_machine_get() != TASKHOG_STATE_RECORDING) {
        return;
    }

    const int64_t dur_us = esp_timer_get_time() - s_press_us;
    ESP_LOGI(TAG, "REC release (%lld ms)", (long long)(dur_us / 1000));

    if (dur_us < REC_MIN_HOLD_US) {
        ESP_LOGI(TAG, "toque curto — descartando");
        rec_session_cancel();
        audio_capture_stop();
        wav_writer_session_abort();
        state_machine_post_event(TASKHOG_EVENT_REC_DISCARD, NULL);
        return;
    }

    state_machine_post_event(TASKHOG_EVENT_REC_RELEASE, NULL);
}

static void rec_button_task(void *arg)
{
    (void)arg;

    bool stable = board_button_is_pressed(BOARD_BTN_BOOT);

    while (true) {
        warn_pwr_pressed();

        const bool raw = board_button_is_pressed(BOARD_BTN_BOOT);

        if (raw != stable) {
            vTaskDelay(pdMS_TO_TICKS(REC_DEBOUNCE_MS));
            if (board_button_is_pressed(BOARD_BTN_BOOT) == raw) {
                stable = raw;
                if (stable) {
                    on_rec_press();
                } else {
                    on_rec_release();
                }
            }
        }

        if (state_machine_get() == TASKHOG_STATE_RECORDING && s_press_us > 0 && !s_max_sent) {
            if (esp_timer_get_time() - s_press_us >= REC_MAX_DURATION_US) {
                ESP_LOGI(TAG, "limite 120 s — finalizando");
                s_max_sent = true;
                state_machine_post_event(TASKHOG_EVENT_RECORD_MAX, NULL);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(REC_POLL_MS));
    }
}

esp_err_t rec_button_init(void)
{
    ESP_RETURN_ON_ERROR(board_power_hold_on(), TAG, "power hold");
    ESP_RETURN_ON_ERROR(board_buttons_init(), TAG, "buttons");

    BaseType_t ok = xTaskCreate(rec_button_task, "rec_btn", 3072, NULL, 6, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "REC = BOOT GPIO%d apenas (PWR GPIO%d = desligar placa — não usar)",
             board_button_gpio(BOARD_BTN_BOOT), board_button_gpio(BOARD_BTN_PWR));
    return ESP_OK;
}
