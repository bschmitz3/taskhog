#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

esp_err_t pcf85063_init(void);
esp_err_t pcf85063_deinit(void);

/** true se o bit OS (oscilador parado) estiver setado — hora não confiável. */
bool rtc_check_os(void);
bool rtc_is_valid(void);

esp_err_t rtc_get(struct tm *out);
esp_err_t rtc_set(const struct tm *in);

/** Copia hora do PCF85063 para o relógio do sistema (timestamps FAT no SD). */
esp_err_t rtc_sync_system_time(void);

/** America/Sao_Paulo para mktime/localtime e timestamps FAT. */
void rtc_apply_timezone(void);

/** Se RTC estiver >2 h atrás da hora do build, grava hora do build no chip. */
esp_err_t rtc_refresh_if_stale(void);

/** Alarme por segundo absoluto (0–59); demais campos ignorados. */
esp_err_t rtc_set_alarm_second(uint8_t second);
/** Alarme por minuto+segundo; hora/dia/semana ignorados. */
esp_err_t rtc_set_alarm_minute_second(uint8_t minute, uint8_t second);
esp_err_t rtc_alarm_enable(bool enable);
esp_err_t rtc_alarm_clear_flag(void);
bool rtc_alarm_flag(void);
esp_err_t rtc_read_ctrl2(uint8_t *out);
