#include "rp_pins.h"
#include "gpio/breakout.h"
#include "platform/board.h"
#include "watchdog/watchdog.h"
#include "common/diag.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

static const char *io_result_text(fwog_io_result_t r) {
    switch (r) {
    case FWOG_IO_OK:            return "ok";
    case FWOG_IO_ERR_ENABLE:    return "display not acking IO_CONFIG (link/splash)";
    case FWOG_IO_ERR_FPGA_WRITE: return "FPGA io_dir write";
    case FWOG_IO_ERR_FPGA_READ:  return "FPGA io_dir read";
    case FWOG_IO_ERR_FPGA_VERIFY: return "FPGA io_dir readback mismatch";
    case FWOG_IO_ERR_EXPANDER:  return "expander SET_DIRS not acked";
    case FWOG_IO_ERR_UNSUPPORTED: return "unsupported cfg (gpio25)";
    default:                    return "unknown";
    }
}

void rp_ctx_init(rp_ctx_t *ctx) {
    memset(ctx, 0, sizeof *ctx);
    ctx->mode = RP_MODE_HIZ;
    ctx->pullups = true;
    ctx->uart_baud = 115200u;
    ctx->uart_data_bits = 8u;
    ctx->i2c_hz = FWOG_I2C_HZ;
    ctx->spi_hz = 1000000u;
    strncpy(ctx->status, "Ready", sizeof ctx->status);
}

static void cfg_hiz(fwog_io_cfg_t *cfg) {
    fwog_io_cfg_default(cfg);
    cfg->spi_tx_out = cfg->spi_rx_out = cfg->spi_cs_out = cfg->spi_sclk_out = false;
    cfg->uart_tx_out = cfg->uart_rx_out = cfg->uart_cts_out = cfg->uart_rts_out = false;
    cfg->gpio26_out = false;
    cfg->gpio27_out = false;
}

static void cfg_dio(fwog_io_cfg_t *cfg) {
    cfg_hiz(cfg);
    cfg->gpio26_out = false;
    cfg->gpio27_out = true;
}

static void cfg_i2c(fwog_io_cfg_t *cfg, bool pull) {
    cfg_hiz(cfg);
    cfg->i2c_pullup = pull;
}

static void cfg_uart(fwog_io_cfg_t *cfg) {
    cfg_hiz(cfg);
    cfg->uart_tx_out = true;
    cfg->uart_rx_out = false;
    cfg->uart_cts_out = false;
    cfg->uart_rts_out = true;
}

static void cfg_spi(fwog_io_cfg_t *cfg) {
    cfg_hiz(cfg);
    cfg->spi_tx_out = cfg->spi_cs_out = cfg->spi_sclk_out = true;
    cfg->spi_rx_out = false;
}

static uint32_t spi_baud(void) {
    const uint32_t peri = clock_get_hz(clk_peri);
    uint32_t hz = peri / 4u;
    if (hz > 10000000u) hz = 10000000u;
    return hz;
}

static void release_peripherals(void) {
    gpio_set_function(PIN_IO_UART_TX, GPIO_FUNC_SIO);
    gpio_set_function(PIN_IO_UART_RX, GPIO_FUNC_SIO);
    gpio_set_function(PIN_IO_UART_CTS, GPIO_FUNC_SIO);
    gpio_set_function(PIN_IO_UART_RTS, GPIO_FUNC_SIO);
    gpio_set_function(PIN_IO_SPI_MOSI, GPIO_FUNC_SIO);
    gpio_set_function(PIN_IO_SPI_MISO, GPIO_FUNC_SIO);
    gpio_set_function(PIN_IO_SPI_SCLK, GPIO_FUNC_SIO);
    gpio_set_function(PIN_IO_SPI_CS, GPIO_FUNC_SIO);
}

static bool setup_uart(const rp_ctx_t *ctx) {
    gpio_set_function(PIN_IO_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_IO_UART_RX, GPIO_FUNC_UART);
    gpio_set_function(PIN_IO_UART_CTS, GPIO_FUNC_UART);
    gpio_set_function(PIN_IO_UART_RTS, GPIO_FUNC_UART);
    uart_init(uart1, ctx->uart_baud);
    uart_set_format(uart1, ctx->uart_data_bits, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart1, true);
    return true;
}

static bool setup_spi(const rp_ctx_t *ctx) {
    const uint32_t hz = ctx->spi_hz ? ctx->spi_hz : spi_baud();
    gpio_set_function(PIN_IO_SPI_SCLK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_IO_SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_IO_SPI_MISO, GPIO_FUNC_SPI);
    gpio_init(PIN_IO_SPI_CS);
    gpio_set_dir(PIN_IO_SPI_CS, GPIO_OUT);
    gpio_put(PIN_IO_SPI_CS, 1);
    spi_init(FWOG_FPGA_SPI, hz);
    spi_set_format(FWOG_FPGA_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    return true;
}

bool rp_pins_apply_mode(rp_ctx_t *ctx, rp_mode_t mode) {
    fwog_io_cfg_t cfg;
    release_peripherals();

    switch (mode) {
    case RP_MODE_HIZ: cfg_hiz(&cfg); break;
    case RP_MODE_DIO: cfg_dio(&cfg); break;
    case RP_MODE_I2C: cfg_i2c(&cfg, ctx->pullups); break;
    case RP_MODE_UART: cfg_uart(&cfg); break;
    case RP_MODE_SPI:  cfg_spi(&cfg); break;
    default: return false;
    }

    fwog_io_result_t ir = FWOG_IO_ERR_ENABLE;
    for (unsigned t = 0; t < 8u; t++) {
        ir = fwog_io_dir_apply(&cfg);
        if (ir == FWOG_IO_OK) break;
        if (ir != FWOG_IO_ERR_ENABLE && ir != FWOG_IO_ERR_EXPANDER) break;
        board_watchdog_kick();
        sleep_ms(250);
    }
    if (ir != FWOG_IO_OK) {
        snprintf(ctx->status, sizeof ctx->status, "io_dir %d", (int)ir);
        DIAG("[retia] io_dir=%d (%s)\n", (int)ir, io_result_text(ir));
        return false;
    }

    ctx->mode = mode;

    switch (mode) {
    case RP_MODE_I2C:
        board_init_i2c();
        snprintf(ctx->status, sizeof ctx->status, "I2C %u Hz", (unsigned)ctx->i2c_hz);
        break;
    case RP_MODE_UART:
        setup_uart(ctx);
        snprintf(ctx->status, sizeof ctx->status, "UART %u baud", (unsigned)ctx->uart_baud);
        break;
    case RP_MODE_SPI:
        setup_spi(ctx);
        snprintf(ctx->status, sizeof ctx->status, "SPI %u Hz", (unsigned)ctx->spi_hz);
        break;
    case RP_MODE_DIO:
        snprintf(ctx->status, sizeof ctx->status, "DIO GP26/27");
        break;
    default:
        snprintf(ctx->status, sizeof ctx->status, "HiZ");
        break;
    }
    return true;
}

static void add_line(rp_state_t *st, const char *text) {
    if (st->line_count >= RP_MAX_LINES) return;
    strncpy(st->lines[st->line_count], text, RP_LINE_LEN - 1u);
    st->lines[st->line_count][RP_LINE_LEN - 1u] = '\0';
    st->line_count++;
}

void rp_state_build(const rp_ctx_t *ctx, rp_state_t *st) {
    memset(st, 0, sizeof *st);
    st->type = RP_MSG_STATE;
    st->mode = (uint8_t)ctx->mode;
    st->pullups = ctx->pullups ? 1u : 0u;
    strncpy(st->status, ctx->status, RP_STATUS_LEN - 1u);

    switch (ctx->mode) {
    case RP_MODE_HIZ:
        add_line(st, "All breakout HiZ");
        break;
    case RP_MODE_DIO:
        add_line(st, "IN  GPIO 26");
        add_line(st, "OUT GPIO 27");
        break;
    case RP_MODE_I2C:
        add_line(st, "SDA GPIO 16");
        add_line(st, "SCL GPIO 17");
        snprintf(st->lines[2], RP_LINE_LEN, "FREQ %u", (unsigned)ctx->i2c_hz);
        st->line_count = 3;
        break;
    case RP_MODE_UART:
        add_line(st, "TX GPIO 8");
        add_line(st, "RX GPIO 9");
        add_line(st, "CTS GPIO 10");
        add_line(st, "RTS GPIO 11");
        snprintf(st->lines[4], RP_LINE_LEN, "BAUD %u", (unsigned)ctx->uart_baud);
        st->line_count = 5;
        break;
    case RP_MODE_SPI:
        add_line(st, "MOSI GPIO 15");
        add_line(st, "MISO GPIO 12");
        add_line(st, "SCLK GPIO 14");
        add_line(st, "CS GPIO 13");
        break;
    default:
        break;
    }
}
