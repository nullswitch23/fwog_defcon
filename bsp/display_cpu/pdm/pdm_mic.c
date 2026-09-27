#include "pdm/pdm_mic.h"

/* ---- Pure: capture-clock arithmetic, ping-pong index arithmetic, and the
 * volume-scaling/threshold logic. See the header for why these three are
 * this driver's host-tested surface. ---- */

float pdm_mic_clkdiv(uint32_t sys_clk_hz) {
    return (float)sys_clk_hz /
           ((float)PDM_SAMPLE_RATE_HZ * (float)PDM_DECIMATION * 4.0f);
}

unsigned pdm_mic_next_buffer_index(unsigned current) {
    return (current + 1u) % PDM_RAW_BUFFER_COUNT;
}

int16_t pdm_scale_sample(int16_t raw_sample, int volume_adjustment) {
    /* Transcribed bit-for-bit from rpMICpdm::process() (:94-105). See the
     * header's trap 5 note: the `< 5` branch amplifies rather than
     * attenuates, exactly like the original, and that is preserved rather
     * than "fixed". `int32_t` matches the original's `int` on this target
     * (32-bit ARM). */
    int32_t cur = (int32_t)raw_sample;
    if (volume_adjustment > 5) {
        cur = (cur << 8);
        cur += (int32_t)raw_sample * (26 * (volume_adjustment - 5));
        cur >>= 8;
    } else if (volume_adjustment < 5) {
        cur = (cur << 8);
        cur -= (int32_t)raw_sample * (26 * (volume_adjustment - 5));
        cur >>= 8;
    }
    return (int16_t)cur;
}

bool pdm_analyze_buffer(const int16_t *raw, unsigned count,
                        int volume_adjustment, int16_t threshold,
                        int16_t *scaled_out, int16_t *out_max, int16_t *out_min) {
    if (!raw || !count) return false;

    /* rpMICpdm::process() (:87-88, :110-113): both extrema start at 0, not
     * at raw[0] -- so a buffer that is entirely negative never raises
     * max_sample above 0, and a buffer that is entirely positive never
     * lowers min_sample below 0. Transcribed as-is; it only matters for the
     * threshold check below, which is symmetric around 0 anyway. */
    int16_t max_sample = 0;
    int16_t min_sample = 0;

    for (unsigned i = 0; i < count; i++) {
        if (scaled_out) scaled_out[i] = pdm_scale_sample(raw[i], volume_adjustment);
        if (raw[i] > max_sample) max_sample = raw[i];
        if (raw[i] < min_sample) min_sample = raw[i];
    }

    if (out_max) *out_max = max_sample;
    if (out_min) *out_min = min_sample;

    /* rpMICpdm::process() (:122-126). */
    bool detected = false;
    if (max_sample > threshold) detected = true;
    else if (min_sample < (int16_t)(-threshold)) detected = true;
    return detected;
}

bool pdm_mic_needs_init(bool already_ready) {
    /* Trap 8: pdm_mic_init() is hardware-only (DMA claim, PIO program load),
     * so this predicate is what makes its idempotency guard host-testable --
     * there is no host-side fake for dma_claim_unused_channel()/
     * pio_add_program() to exercise the real function end to end. See the
     * header's trap 8 note for exactly what a second call while already
     * ready used to leak (an armed, orphaned DMA channel) and why the
     * consequence is a hang, not just a leak (its unacknowledged completion
     * IRQ re-fires forever). */
    return !already_ready;
}

size_t pdm_mic_decode(fwog_cic_t *st, const uint8_t *raw, size_t len,
                      int16_t *out) {
    if (st == NULL || raw == NULL || out == NULL) return 0u;

    size_t n = 0u;
    for (size_t i = 0u; i < len; i++) {
        const uint8_t byte = raw[i];
        /* MSB FIRST. bit 7 is the EARLIEST sample, because the capture PIO
         * shifts LEFT (sm_config_set_in_shift(&c, false, false, 8u)). An
         * LSB-first loop here produces plausible audio with the wrong content
         * and looks exactly like a filter bug. See the header. */
        for (int b = 7; b >= 0; b--) {
            int16_t sample;
            if (fwog_cic_push_bit(st, (byte >> b) & 1u, &sample))
                out[n++] = sample;
        }
    }
    return n;
}

unsigned pdm_density_spread(const uint8_t *buf, size_t len,
                            unsigned block_bytes, unsigned *out_blocks) {
    unsigned blocks, lo, hi, b;

    if (out_blocks) *out_blocks = 0u;
    if (!buf || len == 0u || block_bytes == 0u) return 0u;

    /* Complete blocks only. A trailing partial block has fewer bits to hold
     * ones, so measuring it against the same scale as a full one invents
     * spread that is not in the signal -- see the header's note. */
    blocks = (unsigned)(len / block_bytes);
    if (blocks < 2u) return 0u;
    if (out_blocks) *out_blocks = blocks;

    lo = 0xFFFFFFFFu;
    hi = 0u;
    for (b = 0u; b < blocks; b++) {
        const uint8_t *p = buf + (size_t)b * block_bytes;
        unsigned ones = 0u, i;
        for (i = 0u; i < block_bytes; i++) {
            ones += (unsigned)__builtin_popcount((unsigned)p[i]);
        }
        if (ones < lo) lo = ones;
        if (ones > hi) hi = ones;
    }
    return hi - lo;
}

/* The PDM capture PIO program, transcribed UNCHANGED from
 * pdm_microphone.c:28-35 (equivalently pdm_microphone.pio) -- delay values
 * (none), wrap points, AND side-set polarity, all exactly as encoded there.
 * See the header's trap 3 note for why the sampled edge is correct for this
 * board's L/R=GND strapping. */
const uint16_t pdm_mic_program_instructions[4] = {
    0xa042u, /* 0: nop                   side 0 */
    0x4001u, /* 1: in     pins, 1        side 0 */
    0x9040u, /* 2: push   iffull noblock side 1 */
    0xb042u, /* 3: nop                   side 1 */
};

#ifndef HOST_TEST
#include "common/diag.h"
#include "platform/board.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

static const struct pio_program pdm_mic_program = {
    .instructions = pdm_mic_program_instructions,
    .length = 4u,
    .origin = -1,
};

/* One retained state machine's worth of driver state. Only one PDM
 * microphone exists on this board, so -- like ws2812_driver.c's s_pio/s_sm
 * and lis3dh.c's s_track -- this is a single static instance, not a context
 * struct threaded through every call. */
static PIO  s_pio;
static uint s_sm;
static int  s_dma_ch = -1;
static bool s_ready;

/* The two static raw buffers (trap 6) -- btBufferA/btBufferB in the
 * original. Static, not malloc'd: pdm_microphone_init() malloc's these in
 * the original (later overridden to point at static btBufferA/btBufferB
 * anyway -- see pdm_microphone.c:98-101's `// malloc(...)` comment), and a
 * failed allocation on this board's small SRAM is a worse failure mode than
 * a fixed 4 KiB reservation. */
static uint8_t s_raw_buf_a[PDM_RAW_BUFFER_BYTES];
static uint8_t s_raw_buf_b[PDM_RAW_BUFFER_BYTES];
static uint8_t *s_raw_buf[PDM_RAW_BUFFER_COUNT];

/* Shared with the DMA ISR -- guarded exactly like rpMICpdm.cpp's
 * g_bHasSamples (trap 7). s_write_index/s_read_index are also
 * ISR-written; both are only ever read outside the ISR with interrupts
 * disabled, in pdm_mic_take_raw_buffer() below. */
static volatile unsigned s_write_index;
static volatile unsigned s_read_index;
static volatile bool     s_buffer_ready;

static void pdm_mic_isr(void) {
    /* Clear IRQ. Unlike the original (which supports either DMA_IRQ_0 or
     * DMA_IRQ_1 generically via a runtime field), this driver hardcodes
     * DMA_IRQ_0 -- see the header's trap 2: it is the only DMA IRQ this BSP
     * uses, and being exclusive to this one driver is the point. */
    dma_hw->ints0 = (1u << (uint)s_dma_ch);

    /* Ping-pong handoff, transcribed from pdm_microphone.c:242-245: the
     * buffer DMA just finished filling becomes "ready"; the other one
     * becomes the new fill target. */
    s_read_index = s_write_index;
    s_write_index = pdm_mic_next_buffer_index(s_write_index);

    /* Software re-arm into the other buffer (trap 6) -- NOT a hardware
     * chain. */
    dma_channel_transfer_to_buffer_now((uint)s_dma_ch, s_raw_buf[s_write_index],
                                       PDM_RAW_BUFFER_BYTES);

    /* Unlike the original, which calls a samples_ready_handler() function
     * pointer synchronously from ISR context (running the decimation filter
     * inside the interrupt, since that library ships the filter) -- this
     * port carries no filter (trap 4), so there is nothing to run here.
     * Setting a flag and letting the caller collect the buffer at its own
     * pace keeps this ISR's work to the minimum the hardware needs. */
    s_buffer_ready = true;
}

bool pdm_mic_init(PIO pio, uint sm) {
    /* Trap 8: idempotency guard, checked BEFORE anything below touches
     * hardware state. A second call while capture is already running must
     * not claim a new DMA channel (leaking the old one, still armed) or
     * re-add the PIO program -- see the header's trap 8 note for the hang
     * that leak causes. pdm_mic_needs_init() is the host-tested predicate
     * (tests/test_pdm.c) implementing this decision. */
    if (!pdm_mic_needs_init(s_ready)) {
        DIAG("[pdm] already initialised (dma ch %d, pio%u sm%u); ignoring "
             "repeat pdm_mic_init() call\n",
             s_dma_ch, (unsigned)pio_get_index(s_pio), s_sm);
        return true;
    }

    s_ready = false;

    /* Claim the DMA channel FIRST (trap 1: non-panicking, unlike
     * pdm_microphone.c:109's `dma_claim_unused_channel(true)`), before the
     * PIO program touches any shared instruction memory -- so a failed claim
     * never has to unwind a program load. */
    s_dma_ch = dma_claim_unused_channel(false);
    if (s_dma_ch < 0) {
        DIAG("[pdm] no free DMA channel; capture not started\n");
        return false;
    }

    if (!pio_can_add_program(pio, &pdm_mic_program)) {
        /* Checked, not asserted: this PIO block is shared with WS2812 and
         * (later) I2S -- recon-dma.md -- so a block that is already full is
         * a reachable integration mistake, not a "can't happen". Unclaim the
         * DMA channel already taken above so a failed init leaks nothing. */
        DIAG("[pdm] no room for the PIO program on pio%u\n",
             (unsigned)pio_get_index(pio));
        dma_channel_unclaim((uint)s_dma_ch);
        s_dma_ch = -1;
        return false;
    }
    int offset = pio_add_program(pio, &pdm_mic_program);

    s_raw_buf[0] = s_raw_buf_a;
    s_raw_buf[1] = s_raw_buf_b;

    /* PIO/pin setup, transcribed in the same order as
     * pdm_microphone_data_init() (pdm_microphone.c:51-64). */
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_MIC_DATA, 1u, false);
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_MIC_CLK, 1u, true);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, (uint)offset, (uint)offset + 3u);
    sm_config_set_sideset(&c, 1, /*optional=*/false, /*pindirs=*/false);
    sm_config_set_sideset_pins(&c, PIN_MIC_CLK);
    sm_config_set_in_pins(&c, PIN_MIC_DATA);

    pio_gpio_init(pio, PIN_MIC_CLK);
    pio_gpio_init(pio, PIN_MIC_DATA);

    /* shift_right=false, autopush=false (the program pushes explicitly with
     * `push iffull noblock`), threshold=8: one byte per DMA transfer. */
    sm_config_set_in_shift(&c, false, false, 8u);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

    /* Rate is derived, never hardcoded (AGENTS.md) -- PIO state machines
     * clock from clk_sys, not clk_peri, so this is correct at this board's
     * 200 MHz clk_sys regardless of what clk_peri is doing. */
    float div = pdm_mic_clkdiv((uint32_t)clock_get_hz(clk_sys));
    sm_config_set_clkdiv(&c, div);

    pio_sm_init(pio, sm, (uint)offset, &c);

    /* DMA setup, transcribed from pdm_microphone.c:129-145. */
    dma_channel_config dc = dma_channel_get_default_config((uint)s_dma_ch);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_8);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, false));
    /* Explicit, unlike st7789.c's fill channel: THIS channel must actually
     * raise DMA_IRQ_0 to the NVIC (trap 2), so it must not be quiet. Left at
     * its SDK default (false) already, but stated for the same reason
     * st7789.c states its own choice the other way. */
    channel_config_set_irq_quiet(&dc, false);

    s_write_index = 0u;
    s_read_index = 0u;
    s_buffer_ready = false;

    dma_channel_configure((uint)s_dma_ch, &dc,
                          s_raw_buf[0],            /* write, incrementing */
                          &pio->rxf[sm],           /* read,  fixed        */
                          PDM_RAW_BUFFER_BYTES, false);

    s_pio = pio;
    s_sm = sm;

    /* Exclusive DMA_IRQ_0 handler -- see the header's trap 2 note. Installed
     * and enabled before the state machine starts and before the first
     * transfer triggers, so no completion can occur with the handler not yet
     * in place. */
    irq_set_exclusive_handler(DMA_IRQ_0, pdm_mic_isr);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_channel_set_irq0_enabled((uint)s_dma_ch, true);

    pio_sm_set_enabled(pio, sm, true);
    /* Trigger the first transfer explicitly (pdm_microphone.c:200-204's
     * `dma_channel_transfer_to_buffer_now()`), rather than passing
     * trigger=true to the dma_channel_configure() call above -- this is one
     * simplification from the original's separate init()/start() pair, which
     * called pio_sm_set_enabled() a second, redundant time
     * (pdm_microphone.c:206-210) with no functional difference; that
     * duplicate call is not reproduced here. */
    dma_channel_transfer_to_buffer_now((uint)s_dma_ch, s_raw_buf[0],
                                       PDM_RAW_BUFFER_BYTES);

    s_ready = true;
    /* Integer part only: this BSP's DIAG() callers never print floats
     * elsewhere (float printf support is not assumed to be linked in), and
     * the divider's integer part is enough to sanity-check against
     * clock_get_hz(clk_sys) in the field. */
    DIAG("[pdm] dma ch %d, pio%u sm%u, clkdiv ~%d\n", s_dma_ch,
         (unsigned)pio_get_index(pio), sm, (int)div);
    return true;
}

bool pdm_mic_take_raw_buffer(const uint8_t **buf, size_t *len) {
    if (!s_ready || !buf || !len) return false;

    /* Guarded exactly like rpMICpdm.cpp's g_bHasSamples (trap 7): the ISR
     * can advance s_read_index/s_write_index and set s_buffer_ready at any
     * point between the read of the flag and the read of the index below, so
     * both must be sampled under the same interrupt-disabled window. */
    uint32_t save = save_and_disable_interrupts();
    bool ready = s_buffer_ready;
    unsigned idx = s_read_index;
    if (ready) s_buffer_ready = false;
    restore_interrupts(save);

    if (!ready) return false;

    *buf = s_raw_buf[idx];
    *len = PDM_RAW_BUFFER_BYTES;
    return true;
}

void pdm_mic_stop(void) {
    if (!s_ready) return;
    pio_sm_set_enabled(s_pio, s_sm, false);
    dma_channel_abort((uint)s_dma_ch);
    dma_channel_set_irq0_enabled((uint)s_dma_ch, false);
    irq_set_enabled(DMA_IRQ_0, false);
    s_ready = false;
}
#endif
