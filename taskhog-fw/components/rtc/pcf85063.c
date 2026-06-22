#include "pcf85063.h"

#include "board_pins.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sys/time.h"
#include <string.h>

static const char *TAG = "pcf85063";

#define PCF85063_I2C_PORT   I2C_NUM_0
#define PCF85063_TIMEOUT_MS 100

#define REG_CTRL1       0x00
#define REG_CTRL2       0x01
#define REG_SECONDS     0x04
#define REG_MINUTES     0x05
#define REG_HOURS       0x06
#define REG_DAYS        0x07
#define REG_WEEKDAYS    0x08
#define REG_MONTHS      0x09
#define REG_YEARS       0x0A
#define REG_SEC_ALRM    0x0B
#define REG_MIN_ALRM    0x0C
#define REG_HOUR_ALRM   0x0D
#define REG_DAY_ALRM    0x0E
#define REG_WDAY_ALRM   0x0F

#define SECONDS_OS_BIT  0x80
#define CTRL1_STOP_BIT  0x20
#define CTRL2_AIE_BIT   0x80  /* bit 7 — alarm interrupt enable */
#define CTRL2_AF_BIT    0x40  /* bit 6 — alarm flag */
#define ALRM_DISABLE    0x80  /* bit 7 nos regs de alarme = campo ignorado */

static bool s_rtc_valid;

static uint8_t dec_to_bcd(uint8_t val)
{
    return (uint8_t)(((val / 10) << 4) | (val % 10));
}

static uint8_t bcd_to_dec(uint8_t val)
{
    return (uint8_t)(((val >> 4) * 10) + (val & 0x0F));
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(PCF85063_I2C_PORT, BOARD_I2C_ADDR_PCF85063,
                                      buf, sizeof(buf), pdMS_TO_TICKS(PCF85063_TIMEOUT_MS));
}

static esp_err_t reg_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_write_read_device(PCF85063_I2C_PORT, BOARD_I2C_ADDR_PCF85063,
                                        &reg, 1, val, 1, pdMS_TO_TICKS(PCF85063_TIMEOUT_MS));
}

static esp_err_t time_regs_read(uint8_t *regs7)
{
    uint8_t reg = REG_SECONDS;
    return i2c_master_write_read_device(PCF85063_I2C_PORT, BOARD_I2C_ADDR_PCF85063,
                                        &reg, 1, regs7, 7, pdMS_TO_TICKS(PCF85063_TIMEOUT_MS));
}

static esp_err_t ctrl1_clear_stop(void)
{
    uint8_t c1 = 0;
    ESP_RETURN_ON_ERROR(reg_read(REG_CTRL1, &c1), TAG, "read CTRL1");
    c1 &= (uint8_t)~CTRL1_STOP_BIT;
    return reg_write(REG_CTRL1, c1);
}

esp_err_t pcf85063_init(void)
{
    if (rtc_check_os()) {
        ESP_LOGW(TAG, "Bit OS setado — RTC parou (hora não confiável até rtc_set/NTP)");
        s_rtc_valid = false;
    } else {
        s_rtc_valid = true;
        ESP_LOGI(TAG, "PCF85063 @ 0x%02X — oscilador OK", BOARD_I2C_ADDR_PCF85063);
    }
    return ESP_OK;
}

esp_err_t pcf85063_deinit(void)
{
    return ESP_OK;
}

bool rtc_check_os(void)
{
    uint8_t sec = 0;
    if (reg_read(REG_SECONDS, &sec) != ESP_OK) {
        return true;
    }
    return (sec & SECONDS_OS_BIT) != 0;
}

bool rtc_is_valid(void)
{
    return s_rtc_valid;
}

esp_err_t rtc_get(struct tm *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t r[7] = {0};
    ESP_RETURN_ON_ERROR(time_regs_read(r), TAG, "read time");

    out->tm_sec = bcd_to_dec(r[0] & 0x7F);
    out->tm_min = bcd_to_dec(r[1] & 0x7F);
    out->tm_hour = bcd_to_dec(r[2] & 0x3F);
    out->tm_mday = bcd_to_dec(r[3] & 0x3F);
    out->tm_wday = bcd_to_dec(r[4] & 0x07);
    out->tm_mon = bcd_to_dec(r[5] & 0x1F) - 1;
    out->tm_year = bcd_to_dec(r[6]) + 100; /* 2000–2099 */
    out->tm_isdst = 0;
    return ESP_OK;
}

static int month_from_string(const char *mon)
{
    static const char *names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    for (int i = 0; i < 12; i++) {
        if (strncmp(mon, names[i], 3) == 0) {
            return i;
        }
    }
    return 0;
}

static esp_err_t build_time_struct(struct tm *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char mon[4] = {0};
    int day = 0;
    int year = 0;
    int hour = 0;
    int min = 0;
    int sec = 0;

    if (sscanf(__DATE__, "%3s %d %d", mon, &day, &year) != 3) {
        return ESP_FAIL;
    }
    if (sscanf(__TIME__, "%d:%d:%d", &hour, &min, &sec) != 3) {
        return ESP_FAIL;
    }

    memset(out, 0, sizeof(*out));
    out->tm_year = year - 1900;
    out->tm_mon = month_from_string(mon);
    out->tm_mday = day;
    out->tm_hour = hour;
    out->tm_min = min;
    out->tm_sec = sec;
    out->tm_isdst = 0;
    return ESP_OK;
}

void rtc_apply_timezone(void)
{
    setenv("TZ", "BRT3", 1);
    tzset();
}

esp_err_t rtc_refresh_if_stale(void)
{
    struct tm rtc_tm = {0};
    struct tm build_tm = {0};

    ESP_RETURN_ON_ERROR(rtc_get(&rtc_tm), TAG, "rtc_get");
    ESP_RETURN_ON_ERROR(build_time_struct(&build_tm), TAG, "build time");

    time_t rtc_epoch = mktime(&rtc_tm);
    time_t build_epoch = mktime(&build_tm);
    if (rtc_epoch < 0 || build_epoch < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const time_t lag_sec = build_epoch - rtc_epoch;
    if (lag_sec <= 2 * 3600) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "RTC desatualizado (%lld s atrás do build) — ajustando",
             (long long)lag_sec);
    ESP_RETURN_ON_ERROR(rtc_set(&build_tm), TAG, "rtc_set build");

    ESP_LOGI(TAG, "RTC atualizado para build %04d-%02d-%02d %02d:%02d:%02d",
             build_tm.tm_year + 1900, build_tm.tm_mon + 1, build_tm.tm_mday,
             build_tm.tm_hour, build_tm.tm_min, build_tm.tm_sec);
    return ESP_OK;
}

esp_err_t rtc_sync_system_time(void)
{
    if (!rtc_is_valid()) {
        return ESP_ERR_INVALID_STATE;
    }

    struct tm tm_rtc = {0};
    ESP_RETURN_ON_ERROR(rtc_get(&tm_rtc), TAG, "rtc_get");

    time_t epoch = mktime(&tm_rtc);
    if (epoch < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    struct timeval tv = {
        .tv_sec = epoch,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sistema sincronizado com RTC (%04d-%02d-%02d %02d:%02d:%02d)",
             tm_rtc.tm_year + 1900, tm_rtc.tm_mon + 1, tm_rtc.tm_mday,
             tm_rtc.tm_hour, tm_rtc.tm_min, tm_rtc.tm_sec);
    return ESP_OK;
}

esp_err_t rtc_set(const struct tm *in)
{
    if (in == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(ctrl1_clear_stop(), TAG, "clear STOP");

    uint8_t buf[8] = {
        REG_SECONDS,
        dec_to_bcd((uint8_t)in->tm_sec),
        dec_to_bcd((uint8_t)in->tm_min),
        dec_to_bcd((uint8_t)in->tm_hour),
        dec_to_bcd((uint8_t)in->tm_mday),
        dec_to_bcd((uint8_t)in->tm_wday),
        dec_to_bcd((uint8_t)(in->tm_mon + 1)),
        dec_to_bcd((uint8_t)(in->tm_year - 100)),
    };

    esp_err_t err = i2c_master_write_to_device(PCF85063_I2C_PORT, BOARD_I2C_ADDR_PCF85063,
                                               buf, sizeof(buf), pdMS_TO_TICKS(PCF85063_TIMEOUT_MS));
    if (err != ESP_OK) {
        return err;
    }

    s_rtc_valid = !rtc_check_os();
    ESP_LOGI(TAG, "Hora gravada no RTC (valid=%d)", s_rtc_valid);
    return ESP_OK;
}

esp_err_t rtc_set_alarm_second(uint8_t second)
{
    return rtc_set_alarm_minute_second(0xFF, second);
}

esp_err_t rtc_set_alarm_minute_second(uint8_t minute, uint8_t second)
{
    if (minute <= 59) {
        ESP_RETURN_ON_ERROR(reg_write(REG_MIN_ALRM, dec_to_bcd(minute) & 0x7F), TAG, "min alrm");
    } else {
        ESP_RETURN_ON_ERROR(reg_write(REG_MIN_ALRM, ALRM_DISABLE), TAG, "min alrm dis");
    }
    ESP_RETURN_ON_ERROR(reg_write(REG_SEC_ALRM, dec_to_bcd(second) & 0x7F), TAG, "sec alrm");
    ESP_RETURN_ON_ERROR(reg_write(REG_HOUR_ALRM, ALRM_DISABLE), TAG, "hour alrm dis");
    ESP_RETURN_ON_ERROR(reg_write(REG_DAY_ALRM, ALRM_DISABLE), TAG, "day alrm dis");
    ESP_RETURN_ON_ERROR(reg_write(REG_WDAY_ALRM, ALRM_DISABLE), TAG, "wday alrm dis");
    return ESP_OK;
}

esp_err_t rtc_alarm_enable(bool enable)
{
    uint8_t c2 = 0;
    ESP_RETURN_ON_ERROR(reg_read(REG_CTRL2, &c2), TAG, "read CTRL2");
    if (enable) {
        c2 = (uint8_t)((c2 & (uint8_t)~CTRL2_AF_BIT) | CTRL2_AIE_BIT);
    } else {
        c2 = (uint8_t)(c2 & (uint8_t)~CTRL2_AIE_BIT);
    }
    return reg_write(REG_CTRL2, c2);
}

esp_err_t rtc_alarm_clear_flag(void)
{
    uint8_t c2 = 0;
    ESP_RETURN_ON_ERROR(reg_read(REG_CTRL2, &c2), TAG, "read CTRL2");
    c2 = (uint8_t)(c2 & (uint8_t)~CTRL2_AF_BIT);
    return reg_write(REG_CTRL2, c2);
}

bool rtc_alarm_flag(void)
{
    uint8_t c2 = 0;
    if (reg_read(REG_CTRL2, &c2) != ESP_OK) {
        return false;
    }
    return (c2 & CTRL2_AF_BIT) != 0;
}

esp_err_t rtc_read_ctrl2(uint8_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return reg_read(REG_CTRL2, out);
}
