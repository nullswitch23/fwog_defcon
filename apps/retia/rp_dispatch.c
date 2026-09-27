#include "rp_dispatch.h"
#include "rp_cmdline.h"
#include "rp_mode.h"
#include "rp_modes.h"
#include "rp_proto.h"
#include "rp_terminal.h"
#include "watchdog/watchdog.h"
#include "common/link/link_uart.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void push_state(const rp_ctx_t *ctx) {
    rp_state_t st;
    rp_state_build(ctx, &st);
    (void)fwog_link_uart_send_frame(&st, sizeof st);
}

static bool handle_mode_change(rp_ctx_t *ctx, const char *line) {
    char buf[RP_CMD_MAX];
    strncpy(buf, line, sizeof buf - 1u);
    buf[sizeof buf - 1u] = '\0';
    char *p = buf;
    while (*p == ' ') p++;
    if (strncmp(p, "mode", 4) != 0 && strncmp(p, "m ", 2) != 0 && strcmp(p, "m") != 0)
        return false;
    if (strncmp(p, "mode", 4) == 0) p += 4;
    else p += 1;
    while (*p == ' ') p++;
    if (!*p) {
        rp_terminal_println("");
        rp_terminal_println("Modes: HIZ DIO I2C UART SPI");
        for (unsigned i = 0; i < RP_MODE_COUNT; i++) {
            rp_terminal_printf("  %u. %s\n", i + 1u, rp_mode_name((rp_mode_t)i));
        }
        return true;
    }
    rp_mode_t nm;
    if (p[0] >= '1' && p[0] <= '9' && p[1] == '\0') {
        const unsigned idx = (unsigned)(p[0] - '1');
        if (idx >= RP_MODE_COUNT) {
            rp_terminal_println("Unknown mode. Try: mode hiz|dio|i2c|uart|spi");
            return true;
        }
        nm = (rp_mode_t)idx;
    } else if (!rp_mode_parse(p, &nm)) {
        rp_terminal_println("Unknown mode. Try: mode hiz|dio|i2c|uart|spi");
        return true;
    }
    if (rp_pins_apply_mode(ctx, nm)) {
        rp_terminal_printf("Mode changed to %s\n", rp_mode_name(nm));
        push_state(ctx);
    } else {
        rp_terminal_printf("Mode change failed: %s\n", ctx->status);
    }
    return true;
}

void rp_dispatch_setup(rp_ctx_t *ctx) {
    rp_terminal_init();
    rp_terminal_welcome();
    (void)rp_pins_apply_mode(ctx, RP_MODE_HIZ);
    push_state(ctx);
}

void rp_dispatch_run(rp_ctx_t *ctx) {
    char line[RP_CMD_MAX];
    while (true) {
        board_watchdog_kick();
        rp_terminal_prompt(rp_mode_name(ctx->mode));
        rp_cmdline_read(rp_mode_name(ctx->mode), line, sizeof line);
        if (!line[0]) continue;
        if (handle_mode_change(ctx, line)) continue;
        rp_modes_dispatch(ctx, line);
        push_state(ctx);
    }
}

void rp_dispatch_poll(rp_ctx_t *ctx) {
    char line[RP_CMD_MAX];
    if (!rp_cmdline_poll(rp_mode_name(ctx->mode), line, sizeof line)) return;
    if (!line[0]) return;
    if (handle_mode_change(ctx, line)) return;
    rp_modes_dispatch(ctx, line);
    push_state(ctx);
}
