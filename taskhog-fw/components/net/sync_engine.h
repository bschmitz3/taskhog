#pragma once

#include "esp_err.h"

typedef void (*sync_done_cb_t)(void);

esp_err_t sync_engine_init(void);
esp_err_t sync_engine_deinit(void);

/** Registra callback chamado ao fim de cada drenagem (main posta SYNC_DONE). */
void sync_engine_set_done_cb(sync_done_cb_t cb);

/** Acorda a task de sync para drenar a fila (chamar ao entrar em SYNC). */
void sync_engine_trigger(void);

/** Drenagem síncrona: conecta, checa health, sobe a fila. Retorna nº enviado. */
int sync_engine_drain(void);
