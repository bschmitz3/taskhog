#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "events.h"
#include "state_machine.h"

#include "config.h"
#include "board_power.h"
#include "power_mgr.h"
#include "battery.h"
#include "es8311_codec.h"
#include "audio_capture.h"
#include "audio_beep.h"
#include "wav_writer.h"
#include "sdcard.h"
#include "queue.h"
#include "journal.h"
#include "wifi_sta.h"
#include "sync_engine.h"
#include "http_uploader.h"
#include "time_sync.h"
#include "softap.h"
#include "captive_dns.h"
#include "portal_http.h"
#include "pcf85063.h"
#include "shtc3.h"
#include "epaper_drv.h"
#include "epaper_cfg.h"
#include "framebuffer.h"
#include "widgets.h"
#include "screens.h"
#include "ota_update.h"
#include "rec_button.h"
#include "rec_worker.h"

static const char *TAG = "taskhog";

ESP_EVENT_DEFINE_BASE(TASKHOG_EVENTS);

static esp_event_handler_instance_t s_state_bridge_instance;
static taskhog_state_t s_prev_state = TASKHOG_STATE_BOOT;

static void on_sync_done(void)
{
    state_machine_post_event(TASKHOG_EVENT_SYNC_DONE, NULL);
}

static esp_err_t init_all_stubs(void)
{
    ESP_RETURN_ON_ERROR(config_init(), TAG, "config");
    ESP_RETURN_ON_ERROR(power_mgr_init(), TAG, "power_mgr");
    ESP_RETURN_ON_ERROR(battery_init(), TAG, "battery");
    ESP_RETURN_ON_ERROR(es8311_codec_init(), TAG, "es8311");
    ESP_RETURN_ON_ERROR(audio_beep_init(), TAG, "audio_beep");
    ESP_RETURN_ON_ERROR(audio_capture_init(), TAG, "audio_capture");
    ESP_RETURN_ON_ERROR(wav_writer_init(), TAG, "wav_writer");
    ESP_RETURN_ON_ERROR(sdcard_init(), TAG, "sdcard");
    ESP_RETURN_ON_ERROR(queue_init(), TAG, "queue");
    ESP_RETURN_ON_ERROR(journal_init(), TAG, "journal");
    ESP_RETURN_ON_ERROR(wifi_sta_init(), TAG, "wifi_sta");
    ESP_RETURN_ON_ERROR(sync_engine_init(), TAG, "sync_engine");
    ESP_RETURN_ON_ERROR(http_uploader_init(), TAG, "http_uploader");
    ESP_RETURN_ON_ERROR(time_sync_init(), TAG, "time_sync");
    ESP_RETURN_ON_ERROR(softap_init(), TAG, "softap");
    ESP_RETURN_ON_ERROR(captive_dns_init(), TAG, "captive_dns");
    ESP_RETURN_ON_ERROR(portal_http_init(), TAG, "portal_http");
    ESP_RETURN_ON_ERROR(pcf85063_init(), TAG, "pcf85063");
    ESP_RETURN_ON_ERROR(shtc3_init(), TAG, "shtc3");
    ESP_RETURN_ON_ERROR(epaper_drv_init(), TAG, "epaper_drv");
    ESP_RETURN_ON_ERROR(framebuffer_init(), TAG, "framebuffer");
    ESP_RETURN_ON_ERROR(widgets_init(), TAG, "widgets");
    ESP_RETURN_ON_ERROR(screens_init(), TAG, "screens");
    ESP_RETURN_ON_ERROR(ota_update_init(), TAG, "ota_update");
    return ESP_OK;
}

static void taskhog_state_bridge(void *handler_arg,
                                 esp_event_base_t base,
                                 int32_t id,
                                 void *event_data)
{
    (void)handler_arg;

    if (base != TASKHOG_EVENTS || id != TASKHOG_EVENT_STATE_CHANGED || event_data == NULL) {
        return;
    }

    taskhog_state_t state = *(const taskhog_state_t *)event_data;
    rec_worker_on_state_changed(state);
    screens_on_state_changed((screen_state_t)state);

    if (state == TASKHOG_STATE_SYNC) {
        sync_engine_trigger();
    } else if (state == TASKHOG_STATE_IDLE &&
               (s_prev_state == TASKHOG_STATE_CONFIRM || s_prev_state == TASKHOG_STATE_BOOT)) {
        /* Sync best-effort após captura ou no boot. Não fazemos I/O de SD aqui
         * (handler roda na task do event loop com stack pequena); a task de sync
         * varre a fila com segurança e retorna rápido se não houver pendências. */
        state_machine_post_event(TASKHOG_EVENT_SYNC_REQUEST, NULL);
    }

    s_prev_state = state;
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(board_power_init());
    ESP_ERROR_CHECK(state_machine_init());
    ESP_ERROR_CHECK(esp_event_handler_instance_register(TASKHOG_EVENTS,
                                                        TASKHOG_EVENT_STATE_CHANGED,
                                                        taskhog_state_bridge,
                                                        NULL,
                                                        &s_state_bridge_instance));
    ESP_ERROR_CHECK(init_all_stubs());
    sync_engine_set_done_cb(on_sync_done);

#if EPD_CALIBRATION
    ESP_LOGW(TAG, "EPD_CALIBRATION ativo — desenhando padrao de orientacao e parando boot");
    ESP_ERROR_CHECK(epaper_drv_draw_calibration());
    ESP_LOGW(TAG, "Confira o display: bloco solido no canto SUP-ESQ, 'F' legivel, seta p/ DIREITA");
    return;
#endif

    rtc_apply_timezone();
    if (rtc_is_valid()) {
        if (rtc_refresh_if_stale() != ESP_OK) {
            ESP_LOGW(TAG, "RTC refresh falhou — mantendo hora do chip");
        }
        if (rtc_sync_system_time() != ESP_OK) {
            ESP_LOGW(TAG, "RTC→sistema falhou — timestamps SD podem ficar em 1980");
        }
    } else {
        ESP_LOGW(TAG, "RTC inválido — timestamps SD em 1980 até rtc_set ou NTP");
    }
    ESP_ERROR_CHECK(rec_worker_init());
    ESP_ERROR_CHECK(rec_button_init());

    ESP_ERROR_CHECK(state_machine_post_event(TASKHOG_EVENT_BOOT_COMPLETE, NULL));
    ESP_LOGI(TAG, "Taskhog firmware ready (state=%s)",
             state_machine_state_name(state_machine_get()));
}
