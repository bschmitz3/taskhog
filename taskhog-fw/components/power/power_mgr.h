#pragma once

#include <stdbool.h>

#include "esp_err.h"

/** Causa de wake mapeada (Spec 01 §5 / §7.1). */
typedef enum {
    POWER_WAKE_COLD = 0,
    POWER_WAKE_USB,
    POWER_WAKE_REC_EXT0,
    POWER_WAKE_RTC_INT,
    POWER_WAKE_TIMER,
    POWER_WAKE_OTHER,
} power_wake_cause_t;

/** Ação recomendada no boot conforme causa de wake. */
typedef enum {
    POWER_BOOT_ACTION_IDLE_SYNC = 0,
    POWER_BOOT_ACTION_FAST_REC,
    POWER_BOOT_ACTION_SYNC_QUEUE,
    POWER_BOOT_ACTION_PROVISION,
    POWER_BOOT_ACTION_SLEEP_AGAIN,
} power_boot_action_t;

/** Callback opcional antes de dormir (ex.: wifi_sta_drop no app_main). */
typedef void (*power_mgr_pre_sleep_cb_t)(void);

esp_err_t power_mgr_init(void);
esp_err_t power_mgr_deinit(void);

void power_mgr_set_pre_sleep_cb(power_mgr_pre_sleep_cb_t cb);

power_wake_cause_t power_mgr_get_wake_cause(void);
const char *power_wake_cause_name(power_wake_cause_t cause);

/** Chamar após queue_init/journal — usa fila para despacho timer/RTC. */
power_boot_action_t power_mgr_boot_action(void);

bool power_mgr_usb_connected(void);

/** Habilita ext0/ext1/timer/USB antes de dormir (M6-T2). */
esp_err_t power_mgr_configure_wake_sources(void);

/** Flush journal, desmonta SD, desliga rails, drop Wi-Fi. */
esp_err_t power_mgr_prepare_sleep(void);

/** Não retorna. Respeita menuconfig (deep sleep / skip USB). */
void power_mgr_request_sleep(void);

/** Não retorna. Ignora skip USB (SAFE_OFF / sleep imediato). */
void power_mgr_enter_deep_sleep(void);

/** Timer one-shot em IDLE → request_sleep (se habilitado). */
void power_mgr_arm_idle_sleep(void);
void power_mgr_cancel_idle_sleep(void);
