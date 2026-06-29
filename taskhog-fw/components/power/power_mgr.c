#include "power_mgr.h"

#include "board_pins.h"
#include "board_power.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "journal.h"
#include "pcf85063.h"
#include "queue.h"
#include "sdcard.h"
#include "sdkconfig.h"

#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
#include "driver/usb_serial_jtag.h"
#endif

#if CONFIG_TASKHOG_DEEP_SLEEP_ENABLE
#define POWER_IDLE_SLEEP_SEC   CONFIG_TASKHOG_DEEP_SLEEP_IDLE_SEC
#define POWER_TIMER_WAKE_MIN   CONFIG_TASKHOG_DEEP_SLEEP_TIMER_MIN
#else
#define POWER_IDLE_SLEEP_SEC   15
#define POWER_TIMER_WAKE_MIN   60
#endif

static const char *TAG = "power_mgr";

static power_wake_cause_t s_wake = POWER_WAKE_COLD;
static esp_timer_handle_t s_idle_sleep_timer;
static power_mgr_pre_sleep_cb_t s_pre_sleep_cb;

static power_wake_cause_t map_wake_cause(esp_sleep_wakeup_cause_t raw)
{
    switch (raw) {
    case ESP_SLEEP_WAKEUP_EXT0:
        return POWER_WAKE_REC_EXT0;
    case ESP_SLEEP_WAKEUP_EXT1:
        return POWER_WAKE_RTC_INT;
    case ESP_SLEEP_WAKEUP_TIMER:
        return POWER_WAKE_TIMER;
#if CONFIG_SOC_USB_SUPPORTED
    case ESP_SLEEP_WAKEUP_USB:
        return POWER_WAKE_USB;
#endif
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        return power_mgr_usb_connected() ? POWER_WAKE_USB : POWER_WAKE_COLD;
    default:
        return POWER_WAKE_OTHER;
    }
}

const char *power_wake_cause_name(power_wake_cause_t cause)
{
    switch (cause) {
    case POWER_WAKE_COLD:
        return "cold";
    case POWER_WAKE_USB:
        return "usb";
    case POWER_WAKE_REC_EXT0:
        return "rec_ext0";
    case POWER_WAKE_RTC_INT:
        return "rtc_int";
    case POWER_WAKE_TIMER:
        return "timer";
    case POWER_WAKE_OTHER:
    default:
        return "other";
    }
}

power_wake_cause_t power_mgr_get_wake_cause(void)
{
    return s_wake;
}

bool power_mgr_usb_connected(void)
{
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
    return usb_serial_jtag_is_connected();
#else
    return false;
#endif
}

static bool rec_button_pressed(void)
{
    return gpio_get_level(BOARD_REC_BTN_GPIO) == 0;
}

static esp_err_t ensure_rec_gpio_input(void)
{
    gpio_config_t cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << BOARD_REC_BTN_GPIO,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    return gpio_config(&cfg);
}

static uint64_t timer_wakeup_us(void)
{
    int min = POWER_TIMER_WAKE_MIN;
    if (min < 1) {
        min = 1;
    }
    return (uint64_t)min * 60ULL * 1000000ULL;
}

esp_err_t power_mgr_configure_wake_sources(void)
{
    if (rtc_gpio_is_valid_gpio(BOARD_REC_BTN_GPIO)) {
        rtc_gpio_init(BOARD_REC_BTN_GPIO);
        rtc_gpio_set_direction(BOARD_REC_BTN_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pullup_en(BOARD_REC_BTN_GPIO);
        rtc_gpio_pulldown_dis(BOARD_REC_BTN_GPIO);
    }

    esp_err_t err = esp_sleep_enable_ext0_wakeup(BOARD_REC_BTN_GPIO, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ext0 wake (REC GPIO%d) falhou: %s",
                 BOARD_REC_BTN_GPIO, esp_err_to_name(err));
        return err;
    }

    if (rtc_gpio_is_valid_gpio(BOARD_RTC_INT_GPIO)) {
        rtc_gpio_init(BOARD_RTC_INT_GPIO);
        rtc_gpio_set_direction(BOARD_RTC_INT_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pullup_en(BOARD_RTC_INT_GPIO);
        rtc_gpio_pulldown_dis(BOARD_RTC_INT_GPIO);
    }

    err = esp_sleep_enable_ext1_wakeup(1ULL << BOARD_RTC_INT_GPIO, ESP_EXT1_WAKEUP_ANY_LOW);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ext1 wake (RTC GPIO%d) falhou: %s",
                 BOARD_RTC_INT_GPIO, esp_err_to_name(err));
    }

    err = esp_sleep_enable_timer_wakeup(timer_wakeup_us());
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "timer wake falhou: %s", esp_err_to_name(err));
    }

#if CONFIG_SOC_USB_SUPPORTED
    err = esp_sleep_enable_usb_wakeup();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "usb wake falhou: %s", esp_err_to_name(err));
    }
#endif

    ESP_LOGI(TAG, "wake sources: REC ext0, RTC ext1, timer %d min, USB",
             POWER_TIMER_WAKE_MIN);
    return ESP_OK;
}

esp_err_t power_mgr_prepare_sleep(void)
{
    if (s_pre_sleep_cb != NULL) {
        s_pre_sleep_cb();
    }
    journal_flush();
    (void)sdcard_deinit();
    board_power_epd_off();
    board_power_audio_off();
    return board_power_sleep_latch();
    rtc_alarm_clear_flag();
    return ESP_OK;
}

void power_mgr_enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "deep sleep (causa anterior: %s)", power_wake_cause_name(s_wake));
    ESP_ERROR_CHECK(power_mgr_prepare_sleep());
    ESP_ERROR_CHECK(power_mgr_configure_wake_sources());
    esp_deep_sleep_start();
}

static bool sleep_blocked_by_usb(void)
{
#if CONFIG_TASKHOG_DEEP_SLEEP_ENABLE && CONFIG_TASKHOG_DEEP_SLEEP_SKIP_ON_USB
    if (power_mgr_usb_connected()) {
        ESP_LOGI(TAG, "USB conectado — deep sleep adiado");
        return true;
    }
#endif
    return false;
}

void power_mgr_request_sleep(void)
{
#if !CONFIG_TASKHOG_DEEP_SLEEP_ENABLE
    ESP_LOGD(TAG, "deep sleep desabilitado (menuconfig)");
    return;
#endif
    if (sleep_blocked_by_usb()) {
        return;
    }
    power_mgr_enter_deep_sleep();
}

static void idle_sleep_timer_cb(void *arg)
{
    (void)arg;
    power_mgr_request_sleep();
}

void power_mgr_arm_idle_sleep(void)
{
#if !CONFIG_TASKHOG_DEEP_SLEEP_ENABLE
    return;
#endif
    if (s_idle_sleep_timer == NULL) {
        return;
    }
    if (sleep_blocked_by_usb()) {
        return;
    }
    esp_timer_stop(s_idle_sleep_timer);
    esp_timer_start_once(s_idle_sleep_timer,
                         (uint64_t)POWER_IDLE_SLEEP_SEC * 1000000ULL);
}

void power_mgr_cancel_idle_sleep(void)
{
    if (s_idle_sleep_timer != NULL) {
        esp_timer_stop(s_idle_sleep_timer);
    }
}

power_boot_action_t power_mgr_boot_action(void)
{
    switch (s_wake) {
    case POWER_WAKE_COLD:
    case POWER_WAKE_USB:
        if (rec_button_pressed()) {
            return POWER_BOOT_ACTION_PROVISION;
        }
        return POWER_BOOT_ACTION_IDLE_SYNC;

    case POWER_WAKE_REC_EXT0:
        if (rec_button_pressed()) {
            return POWER_BOOT_ACTION_FAST_REC;
        }
        return POWER_BOOT_ACTION_IDLE_SYNC;

    case POWER_WAKE_RTC_INT:
    case POWER_WAKE_TIMER:
        if (queue_pending_hint() > 0 || queue_hub_pending_count() > 0) {
            return POWER_BOOT_ACTION_SYNC_QUEUE;
        }
        return POWER_BOOT_ACTION_SLEEP_AGAIN;

    case POWER_WAKE_OTHER:
    default:
        return POWER_BOOT_ACTION_IDLE_SYNC;
    }
}

void power_mgr_set_pre_sleep_cb(power_mgr_pre_sleep_cb_t cb)
{
    s_pre_sleep_cb = cb;
}

esp_err_t power_mgr_init(void)
{
    s_wake = map_wake_cause(esp_sleep_get_wakeup_cause());
    ESP_LOGI(TAG, "wake: %s", power_wake_cause_name(s_wake));

    ESP_RETURN_ON_ERROR(ensure_rec_gpio_input(), TAG, "rec gpio");

    const esp_timer_create_args_t idle_args = {
        .callback = idle_sleep_timer_cb,
        .name = "idle_sleep",
    };
    esp_err_t err = esp_timer_create(&idle_args, &s_idle_sleep_timer);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "timer idle sleep indisponível: %s", esp_err_to_name(err));
        s_idle_sleep_timer = NULL;
    }

#if CONFIG_TASKHOG_DEEP_SLEEP_ENABLE
    ESP_LOGI(TAG, "deep sleep ON — idle %ds, timer %d min",
             POWER_IDLE_SLEEP_SEC,
             POWER_TIMER_WAKE_MIN);
#else
    ESP_LOGI(TAG, "deep sleep OFF — habilite Taskhog → Enable deep sleep p/ modo portátil");
#endif

    return ESP_OK;
}

esp_err_t power_mgr_deinit(void)
{
    power_mgr_cancel_idle_sleep();
    if (s_idle_sleep_timer != NULL) {
        esp_timer_delete(s_idle_sleep_timer);
        s_idle_sleep_timer = NULL;
    }
    return ESP_OK;
}
