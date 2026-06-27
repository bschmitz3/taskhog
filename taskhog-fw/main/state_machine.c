#include "state_machine.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "state";

#define CONFIRM_AUTO_MS 500

static taskhog_state_t s_state = TASKHOG_STATE_BOOT;
static esp_timer_handle_t s_confirm_timer;
static esp_event_handler_instance_t s_handler_instance;

static const char *event_name(taskhog_event_id_t event)
{
    switch (event) {
    case TASKHOG_EVENT_BOOT_COMPLETE: return "BOOT_COMPLETE";
    case TASKHOG_EVENT_STATE_CHANGED: return "STATE_CHANGED";
    case TASKHOG_EVENT_REC_PRESS: return "REC_PRESS";
    case TASKHOG_EVENT_REC_RELEASE: return "REC_RELEASE";
    case TASKHOG_EVENT_REC_DISCARD: return "REC_DISCARD";
    case TASKHOG_EVENT_FINALIZE_DONE: return "FINALIZE_DONE";
    case TASKHOG_EVENT_CONFIRM_DONE: return "CONFIRM_DONE";
    case TASKHOG_EVENT_RECORD_MAX: return "RECORD_MAX";
    case TASKHOG_EVENT_SYNC_REQUEST: return "SYNC_REQUEST";
    case TASKHOG_EVENT_SYNC_DONE: return "SYNC_DONE";
    case TASKHOG_EVENT_PROVISION_REQUEST: return "PROVISION_REQUEST";
    case TASKHOG_EVENT_PROVISION_DONE: return "PROVISION_DONE";
    case TASKHOG_EVENT_OTA_REQUEST: return "OTA_REQUEST";
    case TASKHOG_EVENT_OTA_DONE: return "OTA_DONE";
    case TASKHOG_EVENT_BATTERY_LOW: return "BATTERY_LOW";
    case TASKHOG_EVENT_ERROR: return "ERROR";
    default: return "?";
    }
}

static const char *state_name(taskhog_state_t state)
{
    switch (state) {
    case TASKHOG_STATE_BOOT: return "BOOT";
    case TASKHOG_STATE_IDLE: return "IDLE";
    case TASKHOG_STATE_RECORDING: return "RECORDING";
    case TASKHOG_STATE_FINALIZING: return "FINALIZING";
    case TASKHOG_STATE_CONFIRM: return "CONFIRM";
    case TASKHOG_STATE_SYNC: return "SYNC";
    case TASKHOG_STATE_PROVISION: return "PROVISION";
    case TASKHOG_STATE_OTA: return "OTA";
    case TASKHOG_STATE_SAFE_OFF: return "SAFE_OFF";
    default: return "?";
    }
}

const char *state_machine_state_name(taskhog_state_t state)
{
    return state_name(state);
}

static void confirm_timer_cb(void *arg)
{
    (void)arg;
    esp_event_post(TASKHOG_EVENTS, TASKHOG_EVENT_CONFIRM_DONE, NULL, 0, portMAX_DELAY);
}

static void cancel_confirm_timer(void)
{
    if (s_confirm_timer != NULL) {
        esp_timer_stop(s_confirm_timer);
    }
}

static void on_state_enter(taskhog_state_t state)
{
    ESP_LOGI(TAG, "enter %s", state_name(state));

    switch (state) {
    case TASKHOG_STATE_CONFIRM:
        if (s_confirm_timer != NULL) {
            esp_timer_start_once(s_confirm_timer, (uint64_t)CONFIRM_AUTO_MS * 1000ULL);
        }
        break;
    case TASKHOG_STATE_SAFE_OFF:
        ESP_LOGW(TAG, "battery shutdown — deep sleep path (M6)");
        break;
    default:
        break;
    }
}

static void on_state_exit(taskhog_state_t state)
{
    if (state == TASKHOG_STATE_CONFIRM) {
        cancel_confirm_timer();
    }
}

static esp_err_t transition_to(taskhog_state_t next)
{
    if (next == s_state) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "%s -> %s", state_name(s_state), state_name(next));
    on_state_exit(s_state);
    s_state = next;
    on_state_enter(s_state);

    esp_event_post(TASKHOG_EVENTS,
                   TASKHOG_EVENT_STATE_CHANGED,
                   &s_state,
                   sizeof(s_state),
                   portMAX_DELAY);
    return ESP_OK;
}

static bool transition_allowed(taskhog_state_t from, taskhog_event_id_t event, taskhog_state_t *out_next)
{
    switch (from) {
    case TASKHOG_STATE_BOOT:
        switch (event) {
        case TASKHOG_EVENT_BOOT_COMPLETE: *out_next = TASKHOG_STATE_IDLE; return true;
        case TASKHOG_EVENT_REC_PRESS: *out_next = TASKHOG_STATE_RECORDING; return true;
        case TASKHOG_EVENT_PROVISION_REQUEST: *out_next = TASKHOG_STATE_PROVISION; return true;
        case TASKHOG_EVENT_BATTERY_LOW: *out_next = TASKHOG_STATE_SAFE_OFF; return true;
        default: return false;
        }

    case TASKHOG_STATE_IDLE:
        switch (event) {
        case TASKHOG_EVENT_REC_PRESS: *out_next = TASKHOG_STATE_RECORDING; return true;
        case TASKHOG_EVENT_SYNC_REQUEST: *out_next = TASKHOG_STATE_SYNC; return true;
        case TASKHOG_EVENT_PROVISION_REQUEST: *out_next = TASKHOG_STATE_PROVISION; return true;
        case TASKHOG_EVENT_OTA_REQUEST: *out_next = TASKHOG_STATE_OTA; return true;
        case TASKHOG_EVENT_BATTERY_LOW: *out_next = TASKHOG_STATE_SAFE_OFF; return true;
        default: return false;
        }

    case TASKHOG_STATE_RECORDING:
        switch (event) {
        case TASKHOG_EVENT_REC_RELEASE:
        case TASKHOG_EVENT_RECORD_MAX:
        case TASKHOG_EVENT_ERROR:
            *out_next = TASKHOG_STATE_FINALIZING;
            return true;
        case TASKHOG_EVENT_REC_DISCARD:
            *out_next = TASKHOG_STATE_IDLE;
            return true;
        default:
            return false;
        }

    case TASKHOG_STATE_FINALIZING:
        switch (event) {
        case TASKHOG_EVENT_FINALIZE_DONE: *out_next = TASKHOG_STATE_CONFIRM; return true;
        case TASKHOG_EVENT_ERROR: *out_next = TASKHOG_STATE_IDLE; return true;
        default: return false;
        }

    case TASKHOG_STATE_CONFIRM:
        switch (event) {
        case TASKHOG_EVENT_CONFIRM_DONE: *out_next = TASKHOG_STATE_IDLE; return true;
        case TASKHOG_EVENT_SYNC_REQUEST: *out_next = TASKHOG_STATE_SYNC; return true;
        default: return false;
        }

    case TASKHOG_STATE_SYNC:
        switch (event) {
        case TASKHOG_EVENT_SYNC_DONE:
        case TASKHOG_EVENT_ERROR:
            *out_next = TASKHOG_STATE_IDLE;
            return true;
        default:
            return false;
        }

    case TASKHOG_STATE_PROVISION:
        switch (event) {
        case TASKHOG_EVENT_PROVISION_DONE: *out_next = TASKHOG_STATE_IDLE; return true;
        case TASKHOG_EVENT_ERROR: *out_next = TASKHOG_STATE_IDLE; return true;
        default: return false;
        }

    case TASKHOG_STATE_OTA:
        switch (event) {
        case TASKHOG_EVENT_OTA_DONE: *out_next = TASKHOG_STATE_IDLE; return true;
        case TASKHOG_EVENT_ERROR: *out_next = TASKHOG_STATE_IDLE; return true;
        default: return false;
        }

    case TASKHOG_STATE_SAFE_OFF:
    default:
        return false;
    }
}

static void state_machine_event_handler(void *handler_arg,
                                        esp_event_base_t base,
                                        int32_t id,
                                        void *event_data)
{
    (void)handler_arg;
    (void)event_data;

    if (base != TASKHOG_EVENTS) {
        return;
    }

    taskhog_event_id_t event = (taskhog_event_id_t)id;

    if (event == TASKHOG_EVENT_STATE_CHANGED) {
        return;
    }

    taskhog_state_t next = s_state;

    if (!transition_allowed(s_state, event, &next)) {
        ESP_LOGW(TAG, "ignored %s in %s", event_name(event), state_name(s_state));
        return;
    }

    transition_to(next);
}

esp_err_t state_machine_init(void)
{
    s_state = TASKHOG_STATE_BOOT;
    on_state_enter(s_state);

    const esp_timer_create_args_t confirm_timer_args = {
        .callback = confirm_timer_cb,
        .name = "confirm",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&confirm_timer_args, &s_confirm_timer), TAG, "confirm timer");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(TASKHOG_EVENTS,
                                            ESP_EVENT_ANY_ID,
                                            state_machine_event_handler,
                                            NULL,
                                            &s_handler_instance),
        TAG,
        "handler");

    return ESP_OK;
}

taskhog_state_t state_machine_get(void)
{
    return s_state;
}

esp_err_t state_machine_post_event(taskhog_event_id_t event, void *event_data)
{
    return esp_event_post(TASKHOG_EVENTS, event, event_data, 0, portMAX_DELAY);
}
