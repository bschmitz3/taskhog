#include "rec_session.h"

static uint32_t s_session_id;
static uint32_t s_cancelled_id;

uint32_t rec_session_begin(void)
{
    s_session_id++;
    return s_session_id;
}

uint32_t rec_session_current(void)
{
    return s_session_id;
}

void rec_session_cancel(void)
{
    s_cancelled_id = s_session_id;
}

bool rec_session_active(uint32_t session_id)
{
    return session_id != 0 && session_id == s_session_id && session_id != s_cancelled_id;
}
