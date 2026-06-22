#include "screens.h"

#include <stdio.h>

#include "assets_fonts.h"
#include "assets_images.h"
#include "epaper_drv.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "framebuffer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "widgets.h"

static const char *TAG = "screens";

#define UI_QUEUE_LEN     4
#define UI_STACK_WORDS   4096
#define REC_TICK_MS      1000

#define MASCOT_Y 26

typedef enum {
    UI_CMD_SHOW_STATE = 0,
    UI_CMD_REC_TICK,
} ui_cmd_type_t;

typedef struct {
    ui_cmd_type_t type;
    screen_state_t state;
} ui_cmd_t;

static QueueHandle_t s_ui_queue;
static TaskHandle_t s_ui_task;
static esp_timer_handle_t s_rec_timer;
static int64_t s_rec_start_us;
static float s_last_duration_s;
static screen_state_t s_visible_state = SCREEN_STATE_BOOT;

static esp_err_t refresh_display(void)
{
    if (!epaper_drv_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    return epaper_drv_refresh_full();
}

/* Linha com ícone à esquerda + texto, centralizada no conjunto. */
static void icon_label(const gfx_image_t *icon, const gfx_font_t *font,
                       const char *text, int y, int gap)
{
    int tw = fb_text_width(font, text);
    int total = icon->width + gap + tw;
    int x = (FB_W - total) / 2;
    int icon_y = y + (font->height - icon->height) / 2;
    fb_image(icon, x, icon_y, true);
    fb_text(font, x + icon->width + gap, y, text, true);
}

static void render_boot_splash(void)
{
    fb_clear(true);
    fb_text_center(&g_font_tt, 14, "TASKHOG", true);
    fb_text_center(&g_font_sm, 52, "v1.0.0", true);
    fb_image_center(&g_img_mascot_default, 84, true);
}

static void render_home(void)
{
    widget_status_t st;
    widget_read_status(&st);

    fb_clear(true);
    widget_status_bar(&st);

    if (st.queue_count > 0) {
        char line[24];
        snprintf(line, sizeof(line), "%d na fila", st.queue_count);
        fb_image_center(&g_img_mascot_default, MASCOT_Y, true);
        icon_label(&g_img_cloud_sm, &g_font_hd, line, 142, 6);
    } else {
        fb_image_center(&g_img_mascot_sleeping, MASCOT_Y, true);
        icon_label(&g_img_cloud_sm, &g_font_hd, "Nada na fila", 142, 6);
    }

    widget_footer("Segure REC para gravar");
}

static void render_recording(int elapsed_s)
{
    widget_status_t st;
    widget_read_status(&st);

    fb_clear(true);
    widget_status_bar(&st);

    fb_image_center(&g_img_mascot_angry, MASCOT_Y, true);

    char dur[8];
    widget_format_duration(elapsed_s, dur, sizeof(dur));
    char label[24];
    snprintf(label, sizeof(label), "Gravando %s", dur);
    icon_label(&g_img_mic_sm, &g_font_hd, label, 142, 6);

    widget_footer("Solte REC para finalizar");
}

static void render_saved(void)
{
    widget_status_t st;
    widget_read_status(&st);

    fb_clear(true);
    widget_status_bar(&st);

    fb_image_center(&g_img_mascot_happy, 20, true);
    icon_label(&g_img_check_sm, &g_font_hd, "Salvo", 138, 8);
    fb_text_center(&g_font_md, 162, "Salvo no dispositivo", true);
}

static void render_sync(void)
{
    widget_status_t st;
    widget_read_status(&st);

    fb_clear(true);
    widget_status_bar(&st);

    fb_image_center(&g_img_cloud_lg, 36, true);
    fb_text_center(&g_font_hd, 110, "Sincronizando", true);

    if (st.queue_count > 0) {
        char n[12];
        snprintf(n, sizeof(n), "%d", st.queue_count);
        fb_text_center(&g_font_xl, 132, n, true);
    }
}

static void render_for_state(screen_state_t state, int elapsed_s)
{
    switch (state) {
    case SCREEN_STATE_BOOT:
        render_boot_splash();
        break;
    case SCREEN_STATE_IDLE:
        render_home();
        break;
    case SCREEN_STATE_RECORDING:
    case SCREEN_STATE_FINALIZING:
        render_recording(elapsed_s);
        break;
    case SCREEN_STATE_CONFIRM:
        render_saved();
        break;
    case SCREEN_STATE_SYNC:
        render_sync();
        break;
    default:
        render_home();
        break;
    }
}

static void rec_timer_cb(void *arg)
{
    (void)arg;
    if (s_ui_queue == NULL) {
        return;
    }
    ui_cmd_t cmd = {.type = UI_CMD_REC_TICK};
    xQueueSend(s_ui_queue, &cmd, 0);
}

static void start_rec_timer(void)
{
    s_rec_start_us = esp_timer_get_time();
    if (s_rec_timer != NULL) {
        esp_timer_start_periodic(s_rec_timer, (uint64_t)REC_TICK_MS * 1000ULL);
    }
}

static void stop_rec_timer(void)
{
    if (s_rec_timer != NULL) {
        esp_timer_stop(s_rec_timer);
    }
}

static int recording_elapsed_s(void)
{
    if (s_rec_start_us <= 0) {
        return 0;
    }
    int64_t us = esp_timer_get_time() - s_rec_start_us;
    if (us < 0) {
        return 0;
    }
    return (int)(us / 1000000LL);
}

static void handle_show_state(screen_state_t state)
{
    s_visible_state = state;

    if (state == SCREEN_STATE_RECORDING) {
        start_rec_timer();
        render_for_state(state, 0);
        refresh_display();
        return;
    }

    stop_rec_timer();

    if (state == SCREEN_STATE_FINALIZING) {
        render_for_state(SCREEN_STATE_RECORDING, recording_elapsed_s());
    } else {
        render_for_state(state, recording_elapsed_s());
    }
    refresh_display();
}

static void handle_rec_tick(void)
{
    if (s_visible_state != SCREEN_STATE_RECORDING) {
        return;
    }
    render_recording(recording_elapsed_s());
    refresh_display();
}

static void ui_task(void *arg)
{
    (void)arg;
    ui_cmd_t cmd;

    while (true) {
        if (xQueueReceive(s_ui_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (cmd.type) {
        case UI_CMD_SHOW_STATE:
            handle_show_state(cmd.state);
            break;
        case UI_CMD_REC_TICK:
            handle_rec_tick();
            break;
        default:
            break;
        }
    }
}

esp_err_t screens_init(void)
{
    if (s_ui_queue != NULL) {
        return ESP_OK;
    }

    s_ui_queue = xQueueCreate(UI_QUEUE_LEN, sizeof(ui_cmd_t));
    if (s_ui_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t rec_args = {
        .callback = rec_timer_cb,
        .name = "ui_rec",
    };
    ESP_ERROR_CHECK(esp_timer_create(&rec_args, &s_rec_timer));

    BaseType_t ok = xTaskCreate(ui_task, "tk_ui", UI_STACK_WORDS, NULL, 3, &s_ui_task);
    if (ok != pdPASS) {
        esp_timer_delete(s_rec_timer);
        s_rec_timer = NULL;
        vQueueDelete(s_ui_queue);
        s_ui_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "tk_ui pronto");
    return ESP_OK;
}

esp_err_t screens_deinit(void)
{
    stop_rec_timer();
    if (s_rec_timer != NULL) {
        esp_timer_delete(s_rec_timer);
        s_rec_timer = NULL;
    }
    if (s_ui_task != NULL) {
        vTaskDelete(s_ui_task);
        s_ui_task = NULL;
    }
    if (s_ui_queue != NULL) {
        vQueueDelete(s_ui_queue);
        s_ui_queue = NULL;
    }
    return ESP_OK;
}

void screens_on_state_changed(screen_state_t state)
{
    if (s_ui_queue == NULL) {
        return;
    }
    ui_cmd_t cmd = {
        .type = UI_CMD_SHOW_STATE,
        .state = state,
    };
    if (xQueueSend(s_ui_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "fila UI cheia — descartando estado %d", (int)state);
    }
}

void screens_set_last_duration(float duration_s)
{
    s_last_duration_s = duration_s;
}
