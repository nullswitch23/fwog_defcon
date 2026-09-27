#include "ed_jobs.h"
#include "ed_proto.h"
#include <string.h>

static uint8_t s_sel_req = 0xFFu;

void ed_note_sel(const uint8_t *buf, size_t n) {
    if (n >= sizeof(desk_sel_t) && buf[0] == DESK_MSG_SEL)
        s_sel_req = buf[1];
}

uint8_t ed_take_sel(void) {
    const uint8_t v = s_sel_req;
    s_sel_req = 0xFFu;
    return v;
}

/* Shared capture RAM: FobReplay slots (~15 KB) overlay ISMburst ring. */
uint8_t g_ed_cap[20480];
