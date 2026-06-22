#include "m0_epaper_gate.h"

#include "assets_fonts.h"
#include "epaper_drv.h"
#include "esp_check.h"
#include "esp_log.h"
#include "framebuffer.h"

static const char *TAG = "m0_epaper";

esp_err_t m0_epaper_gate_run(void)
{
    ESP_LOGI(TAG, "=== Gate M0-T5: e-Paper hello world ===");

    ESP_RETURN_ON_ERROR(epaper_drv_init(), TAG, "epaper init");
    fb_clear(true);

    fb_rect(4, 4, FB_W - 8, FB_H - 8, true);
    fb_text_center(&g_font_tt, 70, "TASKHOG", true);
    fb_text_center(&g_font_md, 110, "M0-T5 OK", true);

    ESP_RETURN_ON_ERROR(epaper_drv_refresh_full(), TAG, "refresh");

    ESP_LOGI(TAG, "Gate M0-T5: verifique o display fisicamente (texto + moldura)");
    return ESP_OK;
}
