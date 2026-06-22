#include "battery.h"

#include "board_pins.h"
#include "board_power.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "battery";

#define BAT_ADC_UNIT        ADC_UNIT_1
#define BAT_ADC_CHANNEL     ADC_CHANNEL_3 /* GPIO4 */
#define BAT_SAMPLE_COUNT    16
#define BAT_SAMPLE_DELAY_MS 2

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_inited;

typedef struct {
    int mv;
    int pct;
} bat_point_t;

static const bat_point_t s_curve[] = {
    { 4200, 100 },
    { 3850, 75 },
    { 3700, 50 },
    { 3550, 25 },
    { 3400, 10 },
    { 3300, 5 },
    { 3000, 0 },
};

#define BAT_CURVE_COUNT (sizeof(s_curve) / sizeof(s_curve[0]))

static esp_err_t adc_calibration_init(adc_cali_handle_t *out)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id = BAT_ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cfg, &handle);
    if (ret == ESP_OK) {
        calibrated = true;
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_line_fitting_config_t cfg = {
            .unit_id = BAT_ADC_UNIT,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cfg, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    if (!calibrated) {
        ESP_LOGW(TAG, "ADC sem calibração eFuse — usar multímetro p/ calibrar ratio");
        *out = NULL;
        return ESP_OK;
    }

    *out = handle;
    return ESP_OK;
}

esp_err_t battery_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(board_power_init(), TAG, "board power");
    ESP_RETURN_ON_ERROR(board_power_vbat_on(), TAG, "vbat rail");

    adc_oneshot_unit_init_cfg_t init = {
        .unit_id = BAT_ADC_UNIT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init, &s_adc), TAG, "adc unit");

    adc_oneshot_chan_cfg_t chan = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, BAT_ADC_CHANNEL, &chan), TAG, "adc chan");

    ESP_RETURN_ON_ERROR(adc_calibration_init(&s_cali), TAG, "adc cali");

    s_inited = true;
    ESP_LOGI(TAG, "ADC GPIO%d (CH3), divider=%.2f, VBAT_PWR=GPIO%d",
             BOARD_BAT_ADC_GPIO, BOARD_BAT_DIVIDER_RATIO, BOARD_VBAT_PWR_GPIO);
    return ESP_OK;
}

esp_err_t battery_deinit(void)
{
    if (!s_inited) {
        return ESP_OK;
    }
    if (s_cali != NULL) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(s_cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(s_cali);
#endif
        s_cali = NULL;
    }
    if (s_adc != NULL) {
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
    }
    s_inited = false;
    return ESP_OK;
}

int battery_mv_to_percent(int voltage_mv)
{
    if (voltage_mv >= s_curve[0].mv) {
        return 100;
    }
    if (voltage_mv <= s_curve[BAT_CURVE_COUNT - 1].mv) {
        return 0;
    }

    for (size_t i = 0; i < BAT_CURVE_COUNT - 1; i++) {
        const bat_point_t *hi = &s_curve[i];
        const bat_point_t *lo = &s_curve[i + 1];
        if (voltage_mv <= hi->mv && voltage_mv >= lo->mv) {
            int span_mv = hi->mv - lo->mv;
            int span_pct = hi->pct - lo->pct;
            return lo->pct + (span_pct * (voltage_mv - lo->mv)) / span_mv;
        }
    }
    return 0;
}

esp_err_t battery_read(battery_reading_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    int raw_sum = 0;
    for (int i = 0; i < BAT_SAMPLE_COUNT; i++) {
        int raw = 0;
        ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc, BAT_ADC_CHANNEL, &raw), TAG, "adc read");
        raw_sum += raw;
        if (i + 1 < BAT_SAMPLE_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(BAT_SAMPLE_DELAY_MS));
        }
    }
    int raw_avg = raw_sum / BAT_SAMPLE_COUNT;

    int mv_adc = 0;
    if (s_cali != NULL) {
        ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(s_cali, raw_avg, &mv_adc), TAG, "cali");
    } else {
        /* Fallback grosseiro ~3.3 V full scale @ 12 dB (só bring-up). */
        mv_adc = (raw_avg * 3300) / 4095;
    }

    int vbat_mv = (int)(mv_adc * BOARD_BAT_DIVIDER_RATIO);
    out->voltage_mv = vbat_mv;
    out->percent = battery_mv_to_percent(vbat_mv);
    out->valid = (vbat_mv >= 2800 && vbat_mv <= 4300);
    return ESP_OK;
}
