#include "pd_rp.h"
#include "rp_device_view.h"
#include "rp_proto.h"
#include <string.h>

static rp_state_t s_st;
static bool s_have;

void pd_rp_enter(void) {
    memset(&s_st, 0, sizeof s_st);
    s_st.type = RP_MSG_STATE;
    strncpy(s_st.status, "Waiting", sizeof s_st.status);
    s_have = true;
    rp_device_view_attach();
    rp_device_view_paint(&s_st);
}

void pd_rp_frame(const uint8_t *buf, size_t n) {
    if (n >= sizeof(rp_state_t) && buf[0] == RP_MSG_STATE) {
        memcpy(&s_st, buf, sizeof s_st);
        s_have = true;
    }
}

void pd_rp_paint(void) {
    if (s_have) rp_device_view_paint(&s_st);
}
