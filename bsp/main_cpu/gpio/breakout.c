#include "gpio/breakout.h"
#include "fpga/io_dir.h"
#include "common/link/io_proto.h"
#include "common/link/link_uart.h"
#include "common/link/link_frame.h"
#include "common/diag.h"
#include "platform/board.h"
#include "hardware/gpio.h"
#include "pico/time.h"

static fwog_io_cfg_t s_last;
static bool          s_last_valid;
/* True unless the last-recorded config only partially took (F6): false
 * means the FPGA and the RP2040's own pads hold s_last but the expander does
 * not -- see fwog_io_dir_last_fully_applied()'s doc in breakout.h. Starts
 * true: before any apply, s_last is fwog_io_cfg_default(), which describes
 * the board's actual power-on state on all three surfaces. */
static bool s_last_fully_applied = true;

/* Per-message sequence counter (F5). Incremented before every CONFIG_ENABLE
 * or SET_DIRS send (including the CONFIG_ENABLE(false) that closes the
 * window -- it is its own message and gets its own ack). Wraps at 256, which
 * is harmless: wait_ack() only ever compares against the seq of the message
 * it just sent, never against history. */
static uint8_t s_seq;

/* --- display-side steps, over the link --- */

/* File-scope, NOT a local of wait_ack(): fwog_link_rx_t embeds
 * uint8_t buf[FWOG_LINK_MAX_PAYLOAD] (FWOG_LINK_MAX_PAYLOAD == 4160), so the
 * struct is ~4.2 KB (4168 bytes, per sizeof). The Pico SDK's default
 * PICO_STACK_SIZE is 0x800 (2048 bytes) -- putting this on wait_ack()'s
 * stack frame is a guaranteed overflow on the main CPU. Do not "tidy" this
 * back onto the stack. */
static fwog_link_rx_t s_ack_rx;

/* Discard any byte already sitting in the link RX FIFO or PL011 hardware
 * FIFO. Called before every send (F5): without this, a frame that arrives
 * late from a PREVIOUS attempt that already timed out -- entirely possible,
 * since a timeout does not mean the display never answered, only that it
 * answered too late (see breakout.h's FWOG_IO_ACK_TIMEOUT_MS note) -- would
 * still be sitting in the FIFO and could otherwise be misread as belonging
 * to this attempt. The seq check in wait_ack() is the second, independent
 * layer: even a frame that arrives AFTER this drain, in the gap between
 * draining and sending, carries the previous attempt's seq and is rejected
 * on that basis too. */
static void drain_link_rx(void) {
    uint8_t b;
    while (fwog_link_uart_read(&b)) {
        /* discard */
    }
}

/* Sends `frame` (already built with `seq`) and waits for a matching ack.
 * A match requires BOTH `answering` and `seq` -- matching only `answering`,
 * as this used to, lets a stale ack from a timed-out previous attempt
 * satisfy this one, which is exactly the transport-level hole F5 closes. */
static bool wait_ack(const uint8_t *frame, size_t frame_len,
                     uint8_t answering, uint8_t seq) {
    drain_link_rx();
    if (!fwog_link_uart_send_frame(frame, frame_len)) {
        DIAG("[io] CONFIG/SET_DIRS send failed type 0x%02X seq=%u\n",
             (unsigned)answering, (unsigned)seq);
        return false;
    }

    absolute_time_t deadline =
        make_timeout_time_ms(FWOG_IO_ACK_TIMEOUT_MS);
    fwog_link_rx_init(&s_ack_rx);

    while (!time_reached(deadline)) {
        uint8_t b;
        if (!fwog_link_uart_read(&b)) continue;
        /* fwog_link_rx_byte() only writes *out_len on a true (frame
         * completed) return, so len must be initialised before every call
         * rather than relying on it being set. */
        size_t len = 0u;
        if (!fwog_link_rx_byte(&s_ack_rx, b, &len)) continue;
        const uint8_t *p = s_ack_rx.buf;
        if (fwog_io_proto_type(p, len) != FWOG_IO_MSG_ACK) continue;
        const fwog_io_ack_t *a = (const fwog_io_ack_t *)p;
        if (a->answering != answering || a->seq != seq) continue;
        if (a->ok == 0u) {
            DIAG("[io] nack type 0x%02X seq=%u (display expander write)\n",
                 (unsigned)answering, (unsigned)seq);
            return false;
        }
        return true;
    }
    DIAG("[io] ack timeout for type 0x%02X seq=%u\n", (unsigned)answering,
         (unsigned)seq);
    return false;
}

/* Main's belief about the expander's IO_CONFIG bit, maintained here because
 * main is the only thing that ever changes it. Starts UNKNOWN (the enum's
 * zero) and that is correct, not merely cautious: the PCAL6416 does NOT reset
 * when this RP2040 does, so at startup it can still hold a bit set before the
 * reboot. Only an acked CONFIG_ENABLE(false) makes it DISABLED. */
static fwog_io_config_state_t s_io_config;

fwog_io_config_state_t fwog_io_config_state(void) { return s_io_config; }

static bool op_config_enable(void *ctx, bool on) {
    (void)ctx;
    uint8_t seq = ++s_seq;
    uint8_t buf[8];
    size_t n = fwog_io_proto_build_config_enable(buf, sizeof buf, seq, on);
    if (n == 0u) return false;   /* nothing sent; the belief is unchanged */

    /* Once the frame is on the wire the previous belief is stale, so it goes
     * to UNKNOWN BEFORE the wait rather than after a failure. A timeout does
     * not mean the display ignored the message -- breakout.h's header
     * documents the display acking late, after main gave up -- so "we asked
     * and did not hear back" must never leave a confident DISABLED standing. */
    s_io_config = FWOG_IO_CONFIG_UNKNOWN;
    const bool ok = wait_ack(buf, n, FWOG_IO_MSG_CONFIG_ENABLE, seq);
    if (ok) {
        s_io_config = on ? FWOG_IO_CONFIG_ENABLED : FWOG_IO_CONFIG_DISABLED;
    }
    return ok;
}

static bool op_expander_apply(void *ctx, const fwog_io_cfg_t *cfg) {
    (void)ctx;
    uint8_t seq = ++s_seq;
    uint8_t buf[8];
    size_t n = fwog_io_proto_build_set_dirs(buf, sizeof buf, seq, cfg);
    if (n == 0u) return false;
    return wait_ack(buf, n, FWOG_IO_MSG_SET_DIRS, seq);
}

/* --- main-side steps --- */

static bool op_fpga_write(void *ctx, const uint8_t d[2]) {
    (void)ctx; return fwog_fpga_dir_write(d);
}
static bool op_fpga_read(void *ctx, uint8_t d[2]) {
    (void)ctx; return fwog_fpga_dir_read(d);
}

/* gpio_init() unconditionally does gpio_set_dir(IN); gpio_put(gpio, 0);
 * gpio_set_function(SIO) -- it ZEROES THE OUTPUT LATCH. gpio_set_dir(OUT)
 * only enables the output driver; it never re-asserts a level. So calling
 * gpio_init() then gpio_set_dir(OUT) with no gpio_put() in between leaves
 * every output pin actively driven LOW, regardless of what "low" means for
 * that line's protocol.
 *
 * Every pin therefore gets an explicit, documented idle level, applied
 * BEFORE gpio_set_dir() so there is no LOW glitch during the transition
 * (gpio_init() already forced the latch low; asserting the real idle level
 * first means gpio_set_dir(OUT) turns on the driver already sitting at the
 * intended level). The level is applied unconditionally, even when `out` is
 * false: for an input pin the latch is inert, and keeping the call
 * unconditional means a later direction flip never surprises anyone. */
static void set_dir(unsigned pin, bool out, bool idle_high) {
    gpio_init(pin);
    gpio_put(pin, idle_high);
    gpio_set_dir(pin, out ? GPIO_OUT : GPIO_IN);
}

static void op_pads_apply(void *ctx, const fwog_io_cfg_t *cfg) {
    (void)ctx;
    /* PIN_IO_SPI_CS idle HIGH: it is the FPGA's own SPI-slave chip select,
     * active-low, and fpga/ice40.c deliberately keeps it driven HIGH between
     * transactions so the FPGA stays reliably deselected (Task 6). Leaving
     * it LOW here would actively select the FPGA indefinitely -- worse than
     * the floating case that reasoning rejected. */
    set_dir(PIN_IO_SPI_CS,   cfg->spi_cs_out,   true);
    /* SPI mode 0 (CPOL=0), which both FPGA rates use, idles the clock LOW. */
    set_dir(PIN_IO_SPI_SCLK, cfg->spi_sclk_out, false);
    /* MOSI: don't-care while CS is deselected. */
    set_dir(PIN_IO_SPI_MOSI, cfg->spi_tx_out,   false);
    /* MISO: normally an input; the latch is inert when direction is IN. */
    set_dir(PIN_IO_SPI_MISO, cfg->spi_rx_out,   false);
    /* UART TX idle HIGH: that is the UART mark/idle state. Low is a
     * continuous break condition on the breakout UART. */
    set_dir(PIN_IO_UART_TX,  cfg->uart_tx_out,  true);
    /* UART RX: normally an input; the latch is inert when direction is IN. */
    set_dir(PIN_IO_UART_RX,  cfg->uart_rx_out,  true);
    /* RTS/CTS are active-low flow control; HIGH is de-asserted, the safe
     * resting state whichever direction ends up driving them. */
    set_dir(PIN_IO_UART_CTS, cfg->uart_cts_out, true);
    set_dir(PIN_IO_UART_RTS, cfg->uart_rts_out, true);
    /* Plain user GPIO with no protocol meaning: LOW is a defensible,
     * documented default. */
    set_dir(PIN_IO_GPIO_IN,  cfg->gpio26_out,   false);
    set_dir(PIN_IO_GPIO_OUT, cfg->gpio27_out,   false);
}

fwog_io_result_t fwog_io_dir_apply(const fwog_io_cfg_t *cfg) {
    /* F2: reject before touching anything. GPIO 25 is PIN_LED, driven
     * GPIO_OUT permanently by board_init_pins(), and op_pads_apply() below
     * deliberately has no pad write for it. Accepting gpio25_out == false
     * would tell the expander to point that level shifter AT main's GPIO 25
     * while the RP2040 still drives it as an output -- output driving
     * output, indefinitely, with no hardware fix once it happens. See
     * gpio25_out's doc in common/io_cfg.h. */
    if (!cfg->gpio25_out) {
        DIAG("[io] apply refused: gpio25_out=false is not achievable "
             "(GPIO25 is PIN_LED, permanently GPIO_OUT)\n");
        return FWOG_IO_ERR_UNSUPPORTED;
    }

    fwog_io_ops_t ops = {
        .config_enable  = op_config_enable,
        .fpga_write     = op_fpga_write,
        .fpga_read      = op_fpga_read,
        .pads_apply     = op_pads_apply,
        .expander_apply = op_expander_apply,
        .ctx            = NULL,
    };
    fwog_io_result_t r = fwog_io_sequence(&ops, cfg);
    if (r == FWOG_IO_OK) {
        s_last = *cfg; s_last_valid = true; s_last_fully_applied = true;
    } else if (r == FWOG_IO_ERR_EXPANDER) {
        /* F6: by the time the expander can fail, the FPGA and the RP2040's
         * own pads have ALREADY moved to cfg (see io_seq.h's ordering
         * note) -- so recording the previous config here would misreport
         * two of the three surfaces. Record cfg, but mark the record
         * partial so a caller building its next command (cmd_iodir) can
         * tell the difference. */
        s_last = *cfg; s_last_valid = true; s_last_fully_applied = false;
        DIAG("[io] apply failed: %d (fpga+pads moved to the new config; "
             "the expander did not)\n", (int)r);
    } else {
        DIAG("[io] apply failed: %d\n", (int)r);
    }
    return r;
}

const fwog_io_cfg_t *fwog_io_dir_last(void) {
    if (!s_last_valid) fwog_io_cfg_default(&s_last);
    return &s_last;
}

bool fwog_io_dir_last_fully_applied(void) {
    return s_last_fully_applied;
}

fwog_io_pin_result_t fwog_io_pin_drive(unsigned gpio, bool level) {
    const fwog_io_pin_result_t r =
        fwog_io_pin_check_drive(gpio, fwog_io_dir_last(), s_io_config);
    if (r != FWOG_IO_PIN_OK) {
        DIAG("[io] drive gpio%u refused: %d\n", gpio, (int)r);
        return r;
    }

    if (gpio == 25u) {
        /* PIN_LED. board_init_pins() owns its direction and op_pads_apply()
         * deliberately has no pad write for it (io_cfg.h). Set the latch and
         * nothing else -- touching its direction is the one thing
         * fwog_io_dir_apply() refuses outright. */
        gpio_put(gpio, level);
        return FWOG_IO_PIN_OK;
    }

    /* Reclaim the pad from any SPI muxing, then level BEFORE direction. The
     * ordering is set_dir()'s reasoning above: the output latch is asserted
     * first so enabling the driver turns it on already sitting at the
     * intended level, with no LOW glitch on the way. */
    gpio_set_function(gpio, GPIO_FUNC_SIO);
    gpio_put(gpio, level);
    gpio_set_dir(gpio, GPIO_OUT);
    return FWOG_IO_PIN_OK;
}

fwog_io_pin_result_t fwog_io_pin_read(unsigned gpio, bool *level_out) {
    if (!fwog_io_pin_lookup(gpio)) return FWOG_IO_PIN_ERR_NOT_BREAKOUT;

    gpio_set_function(gpio, GPIO_FUNC_SIO);
    /* If the config calls this line an input, make sure the pad really is one
     * before sampling. Otherwise gpio_get() would report our own output latch
     * and a loopback would PASS while sensing nothing at all -- see
     * breakout.h. */
    if (!fwog_io_pin_is_output(fwog_io_dir_last(), gpio)) {
        gpio_set_dir(gpio, GPIO_IN);
    }
    *level_out = gpio_get(gpio);
    return FWOG_IO_PIN_OK;
}

bool fwog_io_config_window(bool open) {
    /* op_config_enable() already maintains s_io_config, including the
       UNKNOWN-on-timeout rule, so this is deliberately nothing but a public
       door onto it. */
    return op_config_enable(NULL, open);
}

fwog_io_pin_result_t fwog_io_i2c_pull(unsigned gpio, bool low,
                                      uint32_t *rise_us_out) {
    if (!fwog_io_is_i2c_line(gpio)) return FWOG_IO_PIN_ERR_NOT_BREAKOUT;

    if (rise_us_out) *rise_us_out = FWOG_I2C_RISE_NONE;

    gpio_set_function(gpio, GPIO_FUNC_SIO);
    if (low) {
        /* Level before direction, as everywhere else here. */
        gpio_put(gpio, 0);
        gpio_set_dir(gpio, GPIO_OUT);
        return FWOG_IO_PIN_OK;
    }

    /* Release: input, and no internal pull of our own. See breakout.h -- an
     * internal pull-up would make every released line read high regardless of
     * whether the external one works. */
    gpio_set_dir(gpio, GPIO_IN);
    gpio_disable_pulls(gpio);

    /* Then MEASURE the rise rather than assuming it is instant.
     *
     * This exists because sampling immediately after a release caught the line
     * still low on 2026-07-28 -- once out of two identical passes -- and the
     * next console command, milliseconds later, read it high. That is a race
     * in the observer, not a fault in the bus: an open-drain line rises only
     * as fast as the pull-up can charge the bus capacitance, and a 10k pull-up
     * is deliberately weak. Reporting the time turns an intermittent
     * false-low into a number. */
    const uint32_t t0 = time_us_32();
    while ((uint32_t)(time_us_32() - t0) < FWOG_I2C_RISE_TIMEOUT_US) {
        if (gpio_get(gpio)) {
            if (rise_us_out) *rise_us_out = (uint32_t)(time_us_32() - t0);
            return FWOG_IO_PIN_OK;
        }
    }
    /* Still low after the timeout. Left as FWOG_I2C_RISE_NONE, which is NOT
     * an error return: a line held low by a device on the bus is a legitimate
     * state, and this function cannot tell that from a missing pull-up. The
     * caller reports the number and lets the operator judge. */
    return FWOG_IO_PIN_OK;
}

fwog_io_pin_result_t fwog_io_i2c_sense(unsigned gpio, bool *level_out) {
    if (!fwog_io_is_i2c_line(gpio)) return FWOG_IO_PIN_ERR_NOT_BREAKOUT;
    /* No set_function, no set_dir, no pull change: gpio_get() reads the pad
     * whatever the pin is muxed to, and touching direction here would release
     * a line the caller is deliberately holding low. */
    *level_out = gpio_get(gpio);
    return FWOG_IO_PIN_OK;
}

/* See breakout.h. Deliberately does NOT call fwog_io_sequence(): that
 * function's whole contract is to abort before touching the pads or the
 * expander when the readback disagrees, and here the readback is EXPECTED to
 * disagree -- reusing it would mean the diagnostic path and the path under
 * investigation could never differ, which is the wrong way round for an
 * experiment. It also does not update s_last / s_last_fully_applied: nothing
 * here changes what the pads or the shifters hold, so recording this as an
 * applied configuration would make fwog_io_dir_last() lie. */
void fwog_io_dir_probe(const fwog_io_cfg_t *cfg, fwog_io_dir_probe_t *out) {
    const uint8_t seed[2] = { 0xA5u, 0x5Au };

    fwog_io_pack_fpga(cfg, out->want);

    out->pre[0] = seed[0]; out->pre[1] = seed[1];
    (void)fwog_fpga_dir_read(out->pre);

    out->enable_ok = op_config_enable(NULL, true);

    out->open_pre[0] = seed[0]; out->open_pre[1] = seed[1];
    (void)fwog_fpga_dir_read(out->open_pre);

    (void)fwog_fpga_dir_write(out->want);
    out->open_rd[0] = seed[0]; out->open_rd[1] = seed[1];
    (void)fwog_fpga_dir_read(out->open_rd);

    out->disable_ok = op_config_enable(NULL, false);

    out->post[0] = seed[0]; out->post[1] = seed[1];
    (void)fwog_fpga_dir_read(out->post);
}
