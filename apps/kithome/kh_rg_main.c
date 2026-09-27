/* KitHome RigGlass main: parse RG lines on MAIN USB CDC (not UART0). */
#include "kh_rg_main.h"
#include "fwog_main.h"
#include "rg_proto.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static rg_status_t s_st;
static char s_line[160];
static unsigned s_llen;
static uint32_t s_push_ms;

static void copy_kv(const char *line, const char *key, char *dst, size_t n) {
    const char *p = strstr(line, key);
    if (!p || n == 0) {
        if (n) dst[0] = '\0';
        return;
    }
    p += strlen(key);
    size_t i = 0;
    while (*p && *p != ' ' && *p != '\r' && *p != '\n' && i + 1u < n) {
        dst[i++] = *p++;
    }
    dst[i] = '\0';
}

static uint8_t clamp_u8(unsigned x) {
    return x > 255u ? (uint8_t)255u : (uint8_t)x;
}

static void parse_triple(const char *s, uint8_t *now, uint8_t *a2, uint8_t *a10) {
    unsigned v[3] = {0};
    int n = 0;
    while (*s && n < 3) {
        if (*s >= '0' && *s <= '9') {
            unsigned x = 0;
            while (*s >= '0' && *s <= '9') {
                x = x * 10u + (unsigned)(*s - '0');
                s++;
            }
            v[n++] = x;
            if (*s == ':' || *s == ',') s++;
        } else {
            s++;
        }
    }
    if (n == 0) return;
    *now = clamp_u8(v[0]);
    *a2 = clamp_u8(n >= 2 ? v[1] : v[0]);
    *a10 = clamp_u8(n >= 3 ? v[2] : v[0]);
}

static void apply_metric(const char *line, const char *key,
                         uint8_t *now, uint8_t *a2, uint8_t *a10) {
    char tmp[24];
    copy_kv(line, key, tmp, sizeof tmp);
    if (tmp[0]) parse_triple(tmp, now, a2, a10);
}

static void handle_line(char *line) {
    if (strncmp(line, "RG ", 3) != 0) return;
    apply_metric(line, "cpu=", &s_st.cpu, &s_st.cpu_2m, &s_st.cpu_10m);
    apply_metric(line, "ram=", &s_st.ram, &s_st.ram_2m, &s_st.ram_10m);
    apply_metric(line, "net=", &s_st.net, &s_st.net_2m, &s_st.net_10m);
    apply_metric(line, "gpu=", &s_st.gpu, &s_st.gpu_2m, &s_st.gpu_10m);
    apply_metric(line, "tmp=", &s_st.tmp, &s_st.tmp_2m, &s_st.tmp_10m);
    copy_kv(line, "host=", s_st.host, sizeof s_st.host);
    s_st.type = RG_MSG_ST;
    (void)fwog_link_uart_send_frame(&s_st, sizeof s_st);
}

void kh_rg_main_init(void) {
    memset(&s_st, 0, sizeof s_st);
    s_st.type = RG_MSG_ST;
    s_st.gpu = s_st.gpu_2m = s_st.gpu_10m = (uint8_t)RG_NA;
    s_st.tmp = s_st.tmp_2m = s_st.tmp_10m = (uint8_t)RG_NA;
    DIAG("[kithome] rigglass: RG cpu=N:2m:10m ram= net= gpu= tmp= host=\n");
}

void kh_rg_main_poll(bool live) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (!live) {
        s_llen = 0;
        return;
    }
    int c = getchar_timeout_us(0);
    while (c >= 0) {
        if (c == '\r') {
            c = getchar_timeout_us(0);
            continue;
        }
        if (c == '\n') {
            s_line[s_llen] = '\0';
            if (s_llen) handle_line(s_line);
            s_llen = 0;
        } else if (s_llen + 1u < sizeof s_line) {
            s_line[s_llen++] = (char)c;
        } else {
            s_llen = 0;
        }
        c = getchar_timeout_us(0);
    }
    if ((now - s_push_ms) > 250u) {
        s_push_ms = now;
        s_st.type = RG_MSG_ST;
        (void)fwog_link_uart_send_frame(&s_st, sizeof s_st);
    }
}
