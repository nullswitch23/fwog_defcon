#include "ir/ir_comm.h"

/* ---- Pure: NEC encode/decode, PIO clock-divider arithmetic, the init
 * idempotency guard, and the three PIO instruction-word tables. See the
 * header for why these, specifically, are this driver's host-tested
 * surface, and for the derivation of ir_nec_receive_program_instructions[]
 * in particular. ---- */

size_t ir_nec_encode(uint32_t code, int *bit_times, size_t capacity) {
    /* Transcribed from BuildIRDataFromCode() (rpIRComm.cpp:214-238) -- see
     * the header's THE BUG note for why this takes a caller-supplied buffer
     * and capacity instead of writing into a fixed-size class member. */
    if (!bit_times || capacity < IR_NEC_FRAME_ENTRIES) return 0u;

    size_t idx = 0u;
    bit_times[idx++] = 9000;
    bit_times[idx++] = 4500;

    for (int bit = 0; bit < 32; bit++) {
        if (code & (1u << (unsigned)bit)) {
            bit_times[idx++] = 563;
            bit_times[idx++] = 1688;
        } else {
            bit_times[idx++] = 563;
            bit_times[idx++] = 563;
        }
    }
    bit_times[idx++] = 563;

    return idx; /* == IR_NEC_FRAME_ENTRIES (67) */
}

bool ir_nec_decode(const int *bit_times, size_t count, uint32_t *out_code,
                   int *out_bits) {
    /* Transcribed from ProcessIRData() (rpIRComm.cpp:173-204) -- see the
     * header for the one deliberate addition (the i+1 < count bounds
     * check) this port's shape requires that the original never needed. */
    uint32_t code = 0u;
    int bits = 0;

    if (out_code) *out_code = 0u;
    if (out_bits) *out_bits = 0;
    if (!bit_times) return false;
    if (count < 16u) return false;

    if (!(bit_times[0] > 7000 && bit_times[0] < 9500)) return false;
    if (!(bit_times[1] > 3500 && bit_times[1] < 5000)) return false;

    for (size_t i = 2u; i < count; i += 2u) {
        if (!(bit_times[i] > 400 && bit_times[i] < 650)) break; /* bad mark: stop, keep partial */
        if (i + 1u >= count) break; /* deliberate bounds check -- see header */

        if (bit_times[i + 1u] > 1000 && bit_times[i + 1u] < 2000) {
            code |= (1u << (unsigned)bits);
        }
        bits++;
        if (bits == 32) break;
    }

    if (out_code) *out_code = code;
    if (out_bits) *out_bits = bits;
    return true;
}

/* Frame layout, MSB first: [~cmd][cmd][addr_hi][addr_lo]. See the header for
 * why these exist -- three of 92 real received frames failed the command
 * check, and nothing applied it. */
bool ir_nec_command_valid(uint32_t code) {
    const uint8_t inv = (uint8_t)((code >> 24) & 0xFFu);
    const uint8_t cmd = (uint8_t)((code >> 16) & 0xFFu);
    return (uint8_t)(cmd ^ inv) == 0xFFu;
}

uint8_t ir_nec_command(uint32_t code) {
    return (uint8_t)((code >> 16) & 0xFFu);
}

uint16_t ir_nec_address16(uint32_t code) {
    return (uint16_t)(code & 0xFFFFu);
}

bool ir_nec_address_is_classic(uint32_t code) {
    const uint8_t hi = (uint8_t)((code >> 8) & 0xFFu);
    const uint8_t lo = (uint8_t)(code & 0xFFu);
    return (uint8_t)(hi ^ lo) == 0xFFu;
}

const char *ir_phy_name(ir_phy_t phy) {
    static const char *const k[] = {
        "NEC", "SIRC12", "SIRC15", "SIRC20", "RC5",
    };
    if ((unsigned)phy >= IR_PHY_COUNT) return "?";
    return k[phy];
}

float ir_phy_carrier_hz(ir_phy_t phy) {
    if (phy == IR_PHY_RC5) return 36000.0f;
    if (phy == IR_PHY_SIRC12 || phy == IR_PHY_SIRC15 || phy == IR_PHY_SIRC20)
        return 40000.0f;
    return 38222.0f;
}

unsigned ir_sirc_nbits(ir_phy_t phy) {
    if (phy == IR_PHY_SIRC12) return 12u;
    if (phy == IR_PHY_SIRC15) return 15u;
    if (phy == IR_PHY_SIRC20) return 20u;
    return 0u;
}

size_t ir_sirc_encode(uint32_t code, unsigned nbits, int *us, size_t cap) {
    size_t n = 0u;
    unsigned i;
    if (!us) return 0u;
    if (nbits != 12u && nbits != 15u && nbits != 20u) return 0u;
    if (cap < 2u + nbits * 2u) return 0u;
    us[n++] = 2400;
    us[n++] = 600;
    for (i = 0; i < nbits; i++) {
        us[n++] = (code & 1u) ? 1200 : 600;
        us[n++] = 600;
        code >>= 1;
    }
    return n;
}

size_t ir_rc5_encode(uint32_t code14, int *us, size_t cap) {
    /* Pairs of (mark, space) microseconds. RC5 bit 1 is space-then-mark. */
    size_t n = 0u;
    int b;
    if (!us || cap < 56u) return 0u;
    for (b = 13; b >= 0; b--) {
        if ((code14 >> (unsigned)b) & 1u) {
            us[n++] = 0;
            us[n++] = 889;
            us[n++] = 889;
            us[n++] = 0;
        } else {
            us[n++] = 889;
            us[n++] = 0;
            us[n++] = 0;
            us[n++] = 889;
        }
    }
    return n;
}

float ir_nec_carrier_burst_clkdiv(uint32_t sys_clk_hz) {
    /* Transcribed from nec_carrier_burst_program_init()'s own
     * clock_get_hz(clk_sys) / (freq * nec_carrier_burst_TICKS_PER_LOOP)
     * (rpIRComm.cpp:380), freq = 38.222 kHz -- the LIVE, tuned carrier
     * frequency (rpIRComm.cpp:454) -- see the header's trap 3 note for why
     * this is NOT the reference comment's 38.000 kHz. */
    return (float)sys_clk_hz / (38.222e3f * 4.0f);
}

float ir_nec_carrier_control_clkdiv(uint32_t sys_clk_hz) {
    /* Transcribed from initPIOTx()'s own 2 * (1 / 562.5e-6f) tick-rate
     * argument (rpIRComm.cpp:460) fed through
     * nec_carrier_control_program_init()'s clock_get_hz(clk_sys) /
     * tick_rate (:418) -- the REFERENCE 562.5 us, not the RX-only tuned
     * 556.00 us. See the header's trap 3 note for why these are
     * deliberately two separate functions. */
    const float tick_rate = 2.0f * (1.0f / 562.5e-6f);
    return (float)sys_clk_hz / tick_rate;
}

double ir_nec_rx_clkdiv(uint32_t sys_clk_hz) {
    /* Transcribed from initPIORx()'s own
     * (double)clock_get_hz(clk_sys) / (10.0 / 556.00e-6) (rpIRComm.cpp:482)
     * -- the LIVE, tuned RX tick period. Kept as double, matching the
     * original's own variable type; see the header's note on this
     * function for where the narrowing to float happens. */
    return (double)sys_clk_hz / (10.0 / 556.00e-6);
}

bool ir_comm_needs_init(bool already_ready) {
    return !already_ready;
}

/* nec_carrier_burst, transcribed UNCHANGED from the original's own compiled
 * array (rpIRComm.cpp:54-62 `nec_carrier_burst_program_instructions[]`),
 * against a floating origin instead of the original's fixed 14 -- see the
 * header's PIO-programs note for why the JMP target (index 2, LOCAL to this
 * program) does not need to change for that. */
const uint16_t ir_nec_carrier_burst_program_instructions[5] = {
    0xe034u, /* 0: set    x, 20 */
    0x20c7u, /* 1: wait   1 irq, 7 */
    0xe001u, /* 2: set    pins, 1 */
    0xe100u, /* 3: set    pins, 0            [1] */
    0x0042u, /* 4: jmp    x--, 2 */
};

/* nec_carrier_control, transcribed UNCHANGED from the original's own
 * compiled array (rpIRComm.cpp:32-46
 * `nec_carrier_control_program_instructions[]`), against a floating origin
 * instead of the original's fixed 19. */
const uint16_t ir_nec_carrier_control_program_instructions[11] = {
    0x80a0u, /*  0: pull   block */
    0xe02fu, /*  1: set    x, 15 */
    0xc007u, /*  2: irq    nowait 7 */
    0x0042u, /*  3: jmp    x--, 2 */
    0xaf42u, /*  4: nop                       [15] */
    0xc107u, /*  5: irq    nowait 7          [1] */
    0x6021u, /*  6: out    x, 1 */
    0x0029u, /*  7: jmp    !x, 9 */
    0xa342u, /*  8: nop                       [3] */
    0xc007u, /*  9: irq    nowait 7 */
    0x00e6u, /* 10: jmp    !osre, 6 */
};

/* nec_receive, HAND-ENCODED from the original's runtime rpPIO::encode_*()
 * call sequence (rpIRComm.cpp:506-541), which itself builds the mjcross
 * `nec_receive` reference program with this board's own tuned
 * IR_RX_BOOST_COUNTER (31, not the reference's 30 -- see the header's
 * trap 3 note) and IR_RX_BIT_SAMPLE_DELAY (15, unchanged from the
 * reference). See the header's PIO-programs note for the derivation and
 * cross-check against known-correct encodings already present in this BSP. */
const uint16_t ir_nec_receive_program_instructions[9] = {
    0xe03fu, /* 0: set    x, 31            ; next_burst: IR_RX_BOOST_COUNTER */
    0x2020u, /* 1: wait   0 pin, 0          ; wait for the next burst to start */
    0x00c7u, /* 2: jmp    pin, 7            ; burst_loop: burst ended early -> data_bit */
    0x0042u, /* 3: jmp    x--, 2            ; wait for the burst to end */
    0xa0c3u, /* 4: mov    isr, null         ; sync burst: reset the ISR */
    0x20a0u, /* 5: wait   1 pin, 0          ; wait for the sync burst to finish */
    0x0000u, /* 6: jmp    0                 ; back to next_burst */
    0xae42u, /* 7: nop                [14]  ; data_bit: wait 1.5 burst periods */
    0x4001u, /* 8: in     pins, 1           ; sample: 0 if next burst has begun, else 1 */
};

#ifndef HOST_TEST
#include "common/diag.h"
#include "platform/board.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio_instructions.h"
#include "pico/time.h"

static const struct pio_program ir_nec_receive_program = {
    .instructions = ir_nec_receive_program_instructions,
    .length = 9u,
    .origin = -1, /* floating -- see the header's PIO-programs note */
};

static const struct pio_program ir_nec_carrier_burst_program = {
    .instructions = ir_nec_carrier_burst_program_instructions,
    .length = 5u,
    .origin = -1,
};

static const struct pio_program ir_nec_carrier_control_program = {
    .instructions = ir_nec_carrier_control_program_instructions,
    .length = 11u,
    .origin = -1,
};

/* One retained set of driver state -- only one IR transmitter/receiver pair
 * exists on this board, so, like every other single-instance driver in this
 * BSP, this is a static instance, not a context struct threaded through
 * every call. */
static PIO  s_pio;
static uint s_sm_rx;
static uint s_sm_burst;
static uint s_sm_control;
static bool     s_ready;
static ir_phy_t s_phy;

bool ir_comm_init(PIO pio, uint sm_rx) {
    if (!ir_comm_needs_init(s_ready)) {
        DIAG("[ir] already initialised (pio%u sm %u/%u/%u); ignoring repeat "
             "ir_comm_init() call\n",
             (unsigned)pio_get_index(s_pio), s_sm_rx, s_sm_burst, s_sm_control);
        return true;
    }

    const uint sm_burst = sm_rx + 1u;
    const uint sm_control = sm_rx + 2u;

    /* Load all three programs, checked (not asserted) against this PIO
     * block's free instruction memory, with rollback at every partial-
     * failure point -- mirroring i2s_audio_init()'s own multi-point
     * rollback (there, two DMA channel claims; here, three PIO program
     * loads). In practice this driver's own dedicated block (recon-dma.md)
     * starts empty and needs only 25 of 32 words, so this is not expected
     * to fail -- see the header's ir_comm_init() note. */
    if (!pio_can_add_program(pio, &ir_nec_receive_program)) {
        DIAG("[ir] no room for nec_receive on pio%u\n",
             (unsigned)pio_get_index(pio));
        return false;
    }
    int off_rx = pio_add_program(pio, &ir_nec_receive_program);

    if (!pio_can_add_program(pio, &ir_nec_carrier_burst_program)) {
        DIAG("[ir] no room for nec_carrier_burst on pio%u\n",
             (unsigned)pio_get_index(pio));
        pio_remove_program(pio, &ir_nec_receive_program, (uint)off_rx);
        return false;
    }
    int off_burst = pio_add_program(pio, &ir_nec_carrier_burst_program);

    if (!pio_can_add_program(pio, &ir_nec_carrier_control_program)) {
        DIAG("[ir] no room for nec_carrier_control on pio%u\n",
             (unsigned)pio_get_index(pio));
        pio_remove_program(pio, &ir_nec_carrier_burst_program, (uint)off_burst);
        pio_remove_program(pio, &ir_nec_receive_program, (uint)off_rx);
        return false;
    }
    int off_control = pio_add_program(pio, &ir_nec_carrier_control_program);

    const uint32_t sys_clk_hz = (uint32_t)clock_get_hz(clk_sys);

    /* ---- RX: park the pin, bring up nec_receive ----
     * Transcribed from init()'s RX pin setup (rpIRComm.cpp:84-86), MINUS
     * the original's own gpio_set_dir(m_iInputPin, iInputPin) call -- see
     * the header's ir_comm_init() note for why that call (passing the pin
     * NUMBER as the direction bool) was a latent bug harmlessly overridden
     * a moment later, and is not reproduced here. gpio_pull_up() IS kept:
     * it is a PADS_BANK0 property independent of function-select, so it
     * survives the pin's switch to PIO function below. */
    gpio_init(PIN_IR_RX);
    gpio_pull_up(PIN_IR_RX);

    pio_sm_config c_rx = pio_get_default_sm_config();
    sm_config_set_wrap(&c_rx, (uint)off_rx + 0u, (uint)off_rx + 8u);
    sm_config_set_in_shift(&c_rx, true /* shift right */, true /* autopush */,
                           32u);
    sm_config_set_fifo_join(&c_rx, PIO_FIFO_JOIN_RX);
    sm_config_set_in_pins(&c_rx, PIN_IR_RX);
    sm_config_set_jmp_pin(&c_rx, PIN_IR_RX);
    /* double->float narrowing here, matching what the original's own
     * rpPIO::setupClockPeriodByDiv() wrapper does internally -- see the
     * header's ir_nec_rx_clkdiv() note. */
    sm_config_set_clkdiv(&c_rx, (float)ir_nec_rx_clkdiv(sys_clk_hz));
    pio_gpio_init(pio, PIN_IR_RX);
    pio_sm_set_consecutive_pindirs(pio, sm_rx, PIN_IR_RX, 1u, false);
    pio_sm_init(pio, sm_rx, (uint)off_rx, &c_rx);

    /* ---- TX burst: park the pin, bring up nec_carrier_burst ----
     * Transcribed from init()'s TX pin setup (rpIRComm.cpp:88-91). */
    gpio_init(PIN_IR_TX);
    gpio_put(PIN_IR_TX, 0);
    gpio_set_dir(PIN_IR_TX, GPIO_OUT);
    gpio_set_drive_strength(PIN_IR_TX, GPIO_DRIVE_STRENGTH_12MA);

    pio_sm_config c_burst = pio_get_default_sm_config();
    sm_config_set_wrap(&c_burst, (uint)off_burst + 0u, (uint)off_burst + 4u);
    sm_config_set_set_pins(&c_burst, PIN_IR_TX, 1u);
    sm_config_set_clkdiv(&c_burst, ir_nec_carrier_burst_clkdiv(sys_clk_hz));
    pio_gpio_init(pio, PIN_IR_TX);
    pio_sm_set_consecutive_pindirs(pio, sm_burst, PIN_IR_TX, 1u, true);
    pio_sm_init(pio, sm_burst, (uint)off_burst, &c_burst);

    /* ---- TX control: bring up nec_carrier_control -- no pin of its own,
     * it only shifts the queued 32-bit code and signals nec_carrier_burst
     * via PIO IRQ 7 (SM-to-SM, not NVIC -- see the header). ---- */
    pio_sm_config c_ctrl = pio_get_default_sm_config();
    sm_config_set_wrap(&c_ctrl, (uint)off_control + 0u, (uint)off_control + 10u);
    sm_config_set_out_shift(&c_ctrl, true /* shift right */,
                            false /* autopull disabled */, 32u);
    sm_config_set_fifo_join(&c_ctrl, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c_ctrl, ir_nec_carrier_control_clkdiv(sys_clk_hz));
    pio_sm_init(pio, sm_control, (uint)off_control, &c_ctrl);

    /* All three come up together -- matching initPIORx()'s unconditional
     * call into initPIOTx() (rpIRComm.cpp:546). */
    pio_sm_set_enabled(pio, sm_rx, true);
    pio_sm_set_enabled(pio, sm_burst, true);
    pio_sm_set_enabled(pio, sm_control, true);

    s_pio = pio;
    s_sm_rx = sm_rx;
    s_sm_burst = sm_burst;
    s_sm_control = sm_control;
    s_ready = true;
    s_phy = IR_PHY_NEC;

    DIAG("[ir] pio%u sm %u(rx)/%u(burst)/%u(control)\n",
         (unsigned)pio_get_index(pio), sm_rx, sm_burst, sm_control);
    return true;
}

static void drain_nec_fifo(void) {
    while (pio_sm_get_rx_fifo_level(s_pio, s_sm_rx) != 0u) {
        (void)pio_sm_get(s_pio, s_sm_rx);
    }
}

static void ir_tx_idle(void) {
    pio_sm_set_enabled(s_pio, s_sm_burst, false);
    pio_sm_exec(s_pio, s_sm_burst, pio_encode_set(pio_pins, 0));
}

static void ir_mark_us(uint32_t us) {
    if (us == 0u) return;
    pio_sm_set_enabled(s_pio, s_sm_burst, true);
    busy_wait_us(us);
    pio_sm_set_enabled(s_pio, s_sm_burst, false);
    pio_sm_exec(s_pio, s_sm_burst, pio_encode_set(pio_pins, 0));
}

static void play_mark_space(const int *us, size_t n) {
    size_t i;
    ir_tx_idle();
    for (i = 0; i + 1u < n; i += 2u) {
        if (us[i] > 0) ir_mark_us((uint32_t)us[i]);
        if (us[i + 1] > 0) busy_wait_us((uint32_t)us[i + 1]);
    }
}

static void rx_as_gpio(void) {
    gpio_init(PIN_IR_RX);
    gpio_set_dir(PIN_IR_RX, GPIO_IN);
    gpio_pull_up(PIN_IR_RX);
}

static void rx_as_pio(void) {
    pio_gpio_init(s_pio, PIN_IR_RX);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm_rx, PIN_IR_RX, 1u, false);
}

static bool wait_rx(bool want_low, uint32_t timeout_us) {
    const uint32_t t0 = time_us_32();
    while ((uint32_t)(time_us_32() - t0) < timeout_us) {
        const bool low = gpio_get(PIN_IR_RX) == 0;
        if (low == want_low) return true;
    }
    return false;
}

static bool read_sirc_gpio(unsigned nbits, uint32_t *out) {
    uint32_t t0;
    uint32_t hdr;
    uint32_t code = 0u;
    unsigned i;
    t0 = time_us_32();
    if (!wait_rx(false, 8000u)) return false;
    hdr = (uint32_t)(time_us_32() - t0);
    if (hdr < 1000u || hdr > 3200u) return false;
    if (!wait_rx(true, 2000u)) return false;
    for (i = 0; i < nbits; i++) {
        t0 = time_us_32();
        if (!wait_rx(false, 2500u)) return false;
        if ((uint32_t)(time_us_32() - t0) > 900u) code |= (1u << i);
        (void)wait_rx(true, 1500u);
    }
    *out = code;
    return true;
}

static bool read_rc5_gpio(uint32_t *out) {
    uint32_t code = 0u;
    int b;
    for (b = 13; b >= 0; b--) {
        bool first_low;
        bool second_low;
        busy_wait_us(444u);
        first_low = gpio_get(PIN_IR_RX) == 0;
        busy_wait_us(889u);
        second_low = gpio_get(PIN_IR_RX) == 0;
        busy_wait_us(445u);
        if (!first_low && second_low) code |= (1u << (unsigned)b);
    }
    *out = code & 0x3FFFu;
    return true;
}

bool ir_comm_set_phy(ir_phy_t phy) {
    const uint32_t sys = clock_get_hz(clk_sys);
    if (!s_ready) return false;
    if ((unsigned)phy >= IR_PHY_COUNT) return false;
    if (phy == s_phy) return true;
    drain_nec_fifo();
    s_phy = phy;
    pio_sm_set_clkdiv(s_pio, s_sm_burst,
                      (float)sys / (ir_phy_carrier_hz(phy) * 4.0f));
    if (phy == IR_PHY_NEC) {
        pio_sm_set_clkdiv(s_pio, s_sm_control, ir_nec_carrier_control_clkdiv(sys));
        pio_sm_set_clkdiv(s_pio, s_sm_rx, (float)ir_nec_rx_clkdiv(sys));
        rx_as_pio();
        pio_sm_set_enabled(s_pio, s_sm_rx, true);
        pio_sm_set_enabled(s_pio, s_sm_control, true);
        pio_sm_set_enabled(s_pio, s_sm_burst, true);
    } else {
        pio_sm_set_enabled(s_pio, s_sm_rx, false);
        pio_sm_set_enabled(s_pio, s_sm_control, false);
        ir_tx_idle();
        rx_as_gpio();
    }
    DIAG("[ir] phy %s %.0f Hz\n", ir_phy_name(phy),
         (double)ir_phy_carrier_hz(phy));
    return true;
}

ir_phy_t ir_comm_phy(void) {
    return s_phy;
}

void ir_comm_send(uint32_t code) {
    int us[64];
    size_t n;
    unsigned nbits;
    unsigned r;
    if (!s_ready) return;
    if (s_phy == IR_PHY_NEC) {
        pio_sm_put(s_pio, s_sm_control, code);
        return;
    }
    nbits = ir_sirc_nbits(s_phy);
    if (nbits != 0u) {
        n = ir_sirc_encode(code, nbits, us, sizeof us / sizeof us[0]);
        for (r = 0; r < 3u; r++) {
            if (r != 0u) busy_wait_us(25000u);
            play_mark_space(us, n);
        }
        return;
    }
    n = ir_rc5_encode(code, us, sizeof us / sizeof us[0]);
    play_mark_space(us, n);
    busy_wait_us(114000u);
    play_mark_space(us, n);
}

bool ir_comm_read(uint32_t *out_code) {
    unsigned nbits;
    if (!s_ready || !out_code) return false;
    if (s_phy == IR_PHY_NEC) {
        if (pio_sm_get_rx_fifo_level(s_pio, s_sm_rx) == 0u) return false;
        *out_code = pio_sm_get(s_pio, s_sm_rx);
        return true;
    }
    if (gpio_get(PIN_IR_RX) != 0) return false;
    nbits = ir_sirc_nbits(s_phy);
    if (nbits != 0u) return read_sirc_gpio(nbits, out_code);
    return read_rc5_gpio(out_code);
}

void ir_comm_stop(void) {
    if (!s_ready) return;
    pio_sm_set_enabled(s_pio, s_sm_rx, false);
    pio_sm_set_enabled(s_pio, s_sm_burst, false);
    pio_sm_set_enabled(s_pio, s_sm_control, false);
    s_ready = false;
}
#endif
