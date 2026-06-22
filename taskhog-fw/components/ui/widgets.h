#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define WIDGET_REC_MAX_S 120

typedef enum {
    WIFI_DOWN = 0,
    WIFI_UP,
    WIFI_ERROR,
} wifi_state_t;

typedef struct {
    int battery_pct;
    bool battery_valid;
    bool charging;
    int queue_count;
    wifi_state_t wifi;
} widget_status_t;

esp_err_t widgets_init(void);
esp_err_t widgets_deinit(void);

esp_err_t widget_read_status(widget_status_t *out);

/* Barra de status superior: hora à esquerda, wifi+bateria+% à direita. */
void widget_status_bar(const widget_status_t *st);

/* Texto de dica no rodapé (centralizado). */
void widget_footer(const char *text);

void widget_format_clock(char *buf, size_t len);
void widget_format_date_subtitle(char *buf, size_t len);
void widget_format_duration(int seconds, char *buf, size_t len);
