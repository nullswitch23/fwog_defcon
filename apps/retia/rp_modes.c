#include "rp_modes.h"
#include "rp_cmdline.h"
#include "rp_mode.h"
#include "rp_terminal.h"
#include "gpio/breakout.h"
#include "platform/board.h"
#include "common/i2c_bus.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char s_root[32];
static char s_arg1[64];
static char s_arg2[128];

static void parse_line(const char *line) {
    s_root[0] = s_arg1[0] = s_arg2[0] = '\0';
    if (!line || !line[0]) return;
    char buf[RP_CMD_MAX];
    strncpy(buf, line, sizeof buf - 1u);
    buf[sizeof buf - 1u] = '\0';
    char *p = buf;
    while (*p == ' ') p++;
    char *tok = strtok(p, " \t");
    if (!tok) return;
    strncpy(s_root, tok, sizeof s_root - 1u);
    tok = strtok(NULL, " \t");
    if (tok) {
        strncpy(s_arg1, tok, sizeof s_arg1 - 1u);
        tok = strtok(NULL, "");
        if (tok) {
            while (*tok == ' ') tok++;
            strncpy(s_arg2, tok, sizeof s_arg2 - 1u);
        }
    }
}

static uint8_t parse_u8(const char *s, bool *ok) {
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 0);
    *ok = (end && end != s && v <= 0xFFu);
    return (uint8_t)v;
}

static void help_global(void) {
    rp_terminal_println("Global: mode, help, P (pullups on), p (pullups off)");
}

static void help_hiz(void) {
    rp_terminal_println("HiZ: all breakout pins high-impedance.");
    rp_terminal_println("Use 'mode dio|i2c|uart|spi' to configure a bus.");
}

static void help_dio(void) {
    rp_terminal_println("DIO: read <26|27>, set <pin> <IN|OUT|HI|LO>, pins");
}

static void help_i2c(void) {
    rp_terminal_println("I2C: scan, ping <addr>, read <addr> <reg> <n>, write <addr> <reg> <byte>");
}

static void help_uart(void) {
    rp_terminal_println("UART: bridge, read, config baud <n>, config bits <n>");
}

static void help_spi(void) {
    rp_terminal_println("SPI: write <hex bytes...>, read <n>, cs <0|1>, config hz <n>");
}

void rp_modes_help(rp_mode_t mode) {
    help_global();
    switch (mode) {
    case RP_MODE_HIZ:  help_hiz(); break;
    case RP_MODE_DIO:  help_dio(); break;
    case RP_MODE_I2C:  help_i2c(); break;
    case RP_MODE_UART: help_uart(); break;
    case RP_MODE_SPI:  help_spi(); break;
    default: break;
    }
}

static void cfg_hiz_like(fwog_io_cfg_t *cfg) {
    fwog_io_cfg_default(cfg);
    cfg->spi_tx_out = cfg->spi_rx_out = cfg->spi_cs_out = cfg->spi_sclk_out = false;
    cfg->uart_tx_out = cfg->uart_rx_out = cfg->uart_cts_out = cfg->uart_rts_out = false;
    cfg->gpio26_out = false;
    cfg->gpio27_out = false;
}

static bool dio_pin_ok(unsigned pin) {
    return pin == PIN_IO_GPIO_IN || pin == PIN_IO_GPIO_OUT;
}

static void cmd_dio_read(void) {
    bool ok;
    const unsigned pin = parse_u8(s_arg1, &ok);
    if (!ok || !dio_pin_ok(pin)) {
        rp_terminal_println("Usage: read <26|27>");
        return;
    }
    bool level;
    if (fwog_io_pin_read(pin, &level) != FWOG_IO_PIN_OK) {
        rp_terminal_println("Read failed");
        return;
    }
    rp_terminal_printf("GPIO %u = %u (%s)\n", pin, level ? 1u : 0u, level ? "HIGH" : "LOW");
}

static void cmd_dio_set(void) {
    bool ok;
    const unsigned pin = parse_u8(s_arg1, &ok);
    if (!ok || !dio_pin_ok(pin) || !s_arg2[0]) {
        rp_terminal_println("Usage: set <26|27> <IN|OUT|HI|LO>");
        return;
    }
    fwog_io_cfg_t cfg;
    fwog_io_cfg_default(&cfg);
    cfg_hiz_like(&cfg);
    if (pin == PIN_IO_GPIO_IN) cfg.gpio26_out = (s_arg2[0] == 'O');
    else cfg.gpio27_out = (s_arg2[0] == 'O' || s_arg2[0] == 'H' || s_arg2[0] == '1');
    (void)fwog_io_dir_apply(&cfg);

    if (s_arg2[0] == 'H' || s_arg2[0] == '1') {
        (void)fwog_io_pin_drive(pin, true);
        rp_terminal_printf("GPIO %u HIGH\n", pin);
    } else if (s_arg2[0] == 'L' || s_arg2[0] == '0') {
        (void)fwog_io_pin_drive(pin, false);
        rp_terminal_printf("GPIO %u LOW\n", pin);
    } else {
        rp_terminal_printf("GPIO %u configured\n", pin);
    }
}

static void cmd_dio_pins(void) {
    bool a, b;
    (void)fwog_io_pin_read(PIN_IO_GPIO_IN, &a);
    (void)fwog_io_pin_read(PIN_IO_GPIO_OUT, &b);
    rp_terminal_printf("GPIO %u=%u  GPIO %u=%u\n",
                       PIN_IO_GPIO_IN, a ? 1u : 0u,
                       PIN_IO_GPIO_OUT, b ? 1u : 0u);
}

static void cmd_i2c_scan(void) {
    uint8_t found[16];
    const size_t n = fwog_i2c_scan(found, 16u);
    if (n == 0) {
        rp_terminal_println("I2C scan: no devices found");
        return;
    }
    rp_terminal_println("I2C scan:");
    for (size_t i = 0; i < n; i++) {
        rp_terminal_printf("  0x%02X\n", found[i]);
    }
}

static void cmd_i2c_ping(void) {
    bool ok;
    const uint8_t addr = parse_u8(s_arg1, &ok);
    if (!ok) {
        rp_terminal_println("Usage: ping <addr>");
        return;
    }
    uint8_t dummy;
    const int r = i2c_read_timeout_us(FWOG_I2C, addr, &dummy, 1, false, 100000);
    rp_terminal_printf("I2C ping 0x%02X: %s\n", addr, r >= 0 ? "ACK" : "NACK");
}

static void cmd_i2c_read(void) {
    bool ok = false;
    char args[128];
    strncpy(args, s_arg2, sizeof args - 1u);
    args[sizeof args - 1u] = '\0';
    const uint8_t addr = parse_u8(s_arg1, &ok);
    char *reg_t = strtok(args, " \t");
    char *nstr  = strtok(NULL, " \t");
    const uint8_t reg = reg_t ? parse_u8(reg_t, &ok) : 0;
    const uint8_t n   = nstr ? parse_u8(nstr, &ok) : 0;
    if (!ok || n == 0) {
        rp_terminal_println("Usage: read <addr> <reg> <n>");
        return;
    }
    uint8_t buf[32];
    if (n > sizeof buf) {
        rp_terminal_println("Max 32 bytes");
        return;
    }
    if (!fwog_i2c_read_regs(addr, reg, buf, n)) {
        rp_terminal_println("I2C read failed");
        return;
    }
    rp_terminal_print("I2C read: ");
    for (uint8_t i = 0; i < n; i++) rp_terminal_printf("%02X ", buf[i]);
    rp_terminal_println("");
}

static void cmd_i2c_write(void) {
    bool ok = false;
    char args[128];
    strncpy(args, s_arg2, sizeof args - 1u);
    args[sizeof args - 1u] = '\0';
    const uint8_t addr = parse_u8(s_arg1, &ok);
    char *reg_t = strtok(args, " \t");
    char *vstr  = strtok(NULL, " \t");
    const uint8_t reg = reg_t ? parse_u8(reg_t, &ok) : 0;
    const uint8_t val = vstr ? parse_u8(vstr, &ok) : 0;
    if (!ok) {
        rp_terminal_println("Usage: write <addr> <reg> <byte>");
        return;
    }
    if (!fwog_i2c_write_reg(addr, reg, val)) {
        rp_terminal_println("I2C write failed");
        return;
    }
    rp_terminal_println("I2C write OK");
}

static void cmd_uart_bridge(rp_ctx_t *ctx) {
    (void)ctx;
    rp_terminal_println("UART bridge: press Enter to stop");
    while (true) {
        if (uart_is_readable(uart1)) {
            const char c = uart_getc(uart1);
            putchar(c);
        }
        int c = getchar_timeout_us(0);
        if (c == '\n' || c == '\r') break;
        if (c >= 0) uart_putc(uart1, (char)c);
        sleep_ms(1);
    }
    rp_terminal_println("");
}

static void cmd_uart_read(void) {
    rp_terminal_print("UART read: ");
    absolute_time_t deadline = make_timeout_time_ms(200);
    while (!time_reached(deadline)) {
        if (uart_is_readable(uart1)) {
            const char c = uart_getc(uart1);
            if (c >= 32 && c <= 126) putchar(c);
            else rp_terminal_printf("\\x%02X", (unsigned)(uint8_t)c);
        }
        sleep_ms(1);
    }
    rp_terminal_println("");
}

static void cmd_uart_config(rp_ctx_t *ctx) {
    if (strcmp(s_arg1, "baud") == 0) {
        ctx->uart_baud = (uint32_t)strtoul(s_arg2, NULL, 0);
        uart_set_baudrate(uart1, ctx->uart_baud);
        rp_terminal_printf("UART baud %u\n", (unsigned)ctx->uart_baud);
    } else if (strcmp(s_arg1, "bits") == 0) {
        bool ok;
        ctx->uart_data_bits = parse_u8(s_arg2, &ok);
        uart_set_format(uart1, ctx->uart_data_bits, 1, UART_PARITY_NONE);
        rp_terminal_printf("UART data bits %u\n", (unsigned)ctx->uart_data_bits);
    } else {
        rp_terminal_println("Usage: config baud <n> | config bits <n>");
    }
}

static void cmd_spi_cs(void) {
    bool ok;
    const unsigned v = parse_u8(s_arg1, &ok);
    if (!ok) {
        rp_terminal_println("Usage: cs <0|1>");
        return;
    }
    gpio_put(PIN_IO_SPI_CS, v ? 1 : 0);
}

static void cmd_spi_write(void) {
    char *tok = strtok(s_arg1, " \t");
    rp_terminal_print("SPI TX: ");
    while (tok) {
        bool ok;
        const uint8_t b = parse_u8(tok, &ok);
        if (!ok) break;
        const uint8_t rx = spi_write_blocking(FWOG_FPGA_SPI, &b, 1);
        rp_terminal_printf("%02X ", rx);
        tok = strtok(NULL, " \t");
    }
    rp_terminal_println("");
}

static void cmd_spi_read(void) {
    bool ok;
    const uint8_t n = parse_u8(s_arg1, &ok);
    if (!ok || n == 0) {
        rp_terminal_println("Usage: read <n>");
        return;
    }
    uint8_t tx[32], rx[32];
    if (n > sizeof tx) {
        rp_terminal_println("Max 32 bytes");
        return;
    }
    memset(tx, 0xFF, n);
    spi_write_read_blocking(FWOG_FPGA_SPI, tx, rx, n);
    rp_terminal_print("SPI RX: ");
    for (uint8_t i = 0; i < n; i++) rp_terminal_printf("%02X ", rx[i]);
    rp_terminal_println("");
}

static void cmd_spi_config(rp_ctx_t *ctx) {
    if (strcmp(s_arg1, "hz") == 0) {
        ctx->spi_hz = (uint32_t)strtoul(s_arg2, NULL, 0);
        spi_set_baudrate(FWOG_FPGA_SPI, ctx->spi_hz);
        rp_terminal_printf("SPI %u Hz\n", (unsigned)ctx->spi_hz);
    } else {
        rp_terminal_println("Usage: config hz <n>");
    }
}

static void dispatch_mode(rp_ctx_t *ctx) {
    switch (ctx->mode) {
    case RP_MODE_HIZ:
        rp_terminal_println("HiZ: type 'help' or 'mode <name>'");
        break;
    case RP_MODE_DIO:
        if (strcmp(s_root, "read") == 0) cmd_dio_read();
        else if (strcmp(s_root, "set") == 0) cmd_dio_set();
        else if (strcmp(s_root, "pins") == 0 || strcmp(s_root, "status") == 0) cmd_dio_pins();
        else rp_modes_help(ctx->mode);
        break;
    case RP_MODE_I2C:
        if (strcmp(s_root, "scan") == 0) cmd_i2c_scan();
        else if (strcmp(s_root, "ping") == 0) cmd_i2c_ping();
        else if (strcmp(s_root, "read") == 0) cmd_i2c_read();
        else if (strcmp(s_root, "write") == 0) cmd_i2c_write();
        else rp_modes_help(ctx->mode);
        break;
    case RP_MODE_UART:
        if (strcmp(s_root, "bridge") == 0) cmd_uart_bridge(ctx);
        else if (strcmp(s_root, "read") == 0) cmd_uart_read();
        else if (strcmp(s_root, "config") == 0) cmd_uart_config(ctx);
        else rp_modes_help(ctx->mode);
        break;
    case RP_MODE_SPI:
        if (strcmp(s_root, "cs") == 0) cmd_spi_cs();
        else if (strcmp(s_root, "write") == 0) cmd_spi_write();
        else if (strcmp(s_root, "read") == 0) cmd_spi_read();
        else if (strcmp(s_root, "config") == 0) cmd_spi_config(ctx);
        else rp_modes_help(ctx->mode);
        break;
    default:
        break;
    }
}

void rp_modes_dispatch(rp_ctx_t *ctx, const char *line) {
    parse_line(line);
    if (!s_root[0]) return;

    if (strcmp(s_root, "help") == 0 || strcmp(s_root, "?") == 0 || strcmp(s_root, "h") == 0) {
        rp_modes_help(ctx->mode);
        return;
    }
    if (strcmp(s_root, "P") == 0) {
        ctx->pullups = true;
        if (ctx->mode == RP_MODE_I2C) (void)rp_pins_apply_mode(ctx, RP_MODE_I2C);
        rp_terminal_println("Pull-ups enabled");
        return;
    }
    if (strcmp(s_root, "p") == 0) {
        ctx->pullups = false;
        if (ctx->mode == RP_MODE_I2C) (void)rp_pins_apply_mode(ctx, RP_MODE_I2C);
        rp_terminal_println("Pull-ups disabled");
        return;
    }

    dispatch_mode(ctx);
}
