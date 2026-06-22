#pragma once

#include <stdint.h>
#include <stdbool.h>

/** Incrementado a cada REC press; retorna id da sessão corrente. */
uint32_t rec_session_begin(void);

/** Id da sessão de gravação corrente (último REC press). */
uint32_t rec_session_current(void);

/** Marca a sessão corrente como cancelada (toque curto). */
void rec_session_cancel(void);

/** false se a sessão id foi cancelada ou substituída por press posterior. */
bool rec_session_active(uint32_t session_id);
