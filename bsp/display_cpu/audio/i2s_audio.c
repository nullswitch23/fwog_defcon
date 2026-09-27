#include "audio/i2s_audio.h"

/* ---- Pure: volume table, clock-divider arithmetic, ping-pong bookkeeping,
 * and buffer-fill/format-conversion. See the header for why these,
 * specifically, are this driver's host-tested surface. ---- */

const float i2s_audio_volume_multipliers[11] = {
    0.00f,  /* Volume 0 - Off/Mute      */
    0.01f,  /* Volume 1 - Very quiet    */
    0.04f,  /* Volume 2 - Quiet         */
    0.09f,  /* Volume 3 - Low           */
    0.16f,  /* Volume 4 - Medium-low    */
    0.25f,  /* Volume 5 - Medium        */
    0.36f,  /* Volume 6 - Medium-high   */
    0.49f,  /* Volume 7 - High          */
    0.64f,  /* Volume 8 - Very high     */
    0.81f,  /* Volume 9 - Near max      */
    1.00f   /* Volume 10 - Full volume  */
};

uint32_t i2s_audio_pio_divider(uint32_t sys_clk_hz, uint32_t sample_rate_hz) {
    /* Transcribed from rpI2S.cpp:121: `system_clock_frequency * 4 /
     * sample_freq`, integer division throughout -- see the header's trap 6
     * note for why the caller, not this function, is responsible for the
     * later `>> 8u`. */
    return (sys_clk_hz * 4u) / sample_rate_hz;
}

/* The audio_i2s PIO program, transcribed UNCHANGED from rpI2S.cpp:78-89
 * (`audio_i2s_program_instructions[]`, the LIVE variant -- see the header
 * for why `audio_i2s_program_instructionsOppsite[]` is dead code and not
 * pinned here). */
const uint16_t i2s_audio_program_instructions[8] = {
    0x6801u, /* 0: out    pins, 1         side 1 */
    0x1840u, /* 1: jmp    x--, 0          side 3 */
    0x6001u, /* 2: out    pins, 1         side 0 */
    0xf02eu, /* 3: set    x, 14           side 2 */
    0x6001u, /* 4: out    pins, 1         side 0 */
    0x1044u, /* 5: jmp    x--, 4          side 2 */
    0x6801u, /* 6: out    pins, 1         side 1 */
    0xf82eu, /* 7: set    x, 14           side 3 */
};

/* The bit-loop counter the program above expects on entry, and the reason
 * starting this state machine is not just "jump to offset and enable".
 *
 * Entry is instruction 0, but the program only loads x ITSELF at instructions
 * 3 and 7 -- i.e. AFTER the left channel. So the first left channel after any
 * entry emits however many bits the STALE x dictates, and with autopull at 32
 * bits a frame that consumes anything other than 32 `out`s permanently offsets
 * the OSR refill boundary against the L/R frame. The symptom is bit-shifted
 * PCM: loud digital noise, not a click.
 *
 * MEASURED on hardware 2026-07-29, and it is why every entry point below does
 * an explicit `set x` before its jmp. 14 gives 15 loop iterations plus the
 * trailing `out`, i.e. the 16 bits per channel this program is built around --
 * it must match the literal in instructions 3 and 7. */
#define I2S_AUDIO_X_INIT 14u

i2s_audio_buffer_t i2s_audio_other_buffer(i2s_audio_buffer_t current) {
    return (current == I2S_AUDIO_BUFFER_A) ? I2S_AUDIO_BUFFER_B : I2S_AUDIO_BUFFER_A;
}

unsigned i2s_audio_fill_count(int total_samples, int current_sample, unsigned capacity) {
    int remaining = total_samples - current_sample;
    if (remaining <= 0) return 0u;
    return ((unsigned)remaining > capacity) ? capacity : (unsigned)remaining;
}

bool i2s_audio_samples_remain(int total_samples, int current_sample) {
    return current_sample < total_samples;
}

bool i2s_audio_chain_drained(unsigned sibling_consumed) {
    /* See the header's fix-round-1 note: this, not
     * i2s_audio_samples_remain(), is the correct stop gate for a
     * hardware-chained pair. */
    return sibling_consumed == 0u;
}

int16_t i2s_audio_expand_8bit(uint8_t sample) {
    /* Transcribed from rpI2S.cpp:811. `sample` is unsigned (0-255) -- see
     * the header's sign-handling note. */
    return (int16_t)(((int16_t)sample - 128) * 50);
}

int16_t i2s_audio_select_sample(const int16_t *src, int index, bool mono) {
    /* Transcribed from rpI2S.cpp:842-845, MINUS the mono branch's original
     * `* 0.8` (dropped -- see the header's trap 5 note). */
    return mono ? src[index] : src[index * 2];
}

float i2s_audio_volume_multiplier(int volume_adjustment, float asset_gain) {
    /* Transcribed from rpI2S.cpp:767-771's clamp-then-multiply, against a
     * local copy of the parameter rather than a mutated class member. */
    if (volume_adjustment > 10) volume_adjustment = 10;
    if (volume_adjustment < 0) volume_adjustment = 0;
    return i2s_audio_volume_multipliers[volume_adjustment] * asset_gain;
}

int16_t i2s_audio_apply_volume(int16_t sample, float multiplier) {
    return (int16_t)((float)sample * multiplier);
}

unsigned i2s_audio_fill_buffer(int16_t *out, unsigned capacity,
                               const int16_t *src, int start_index,
                               unsigned count, bool mono,
                               int volume_adjustment, float asset_gain) {
    const float mult = i2s_audio_volume_multiplier(volume_adjustment, asset_gain);
    unsigned i;
    for (i = 0u; i < capacity; i++) {
        if (i < count) {
            const int16_t s = i2s_audio_select_sample(src, start_index + (int)i, mono);
            out[i] = i2s_audio_apply_volume(s, mult);
        } else {
            /* Transcribed from rpI2S.cpp:873's tail zero-pad. */
            out[i] = 0;
        }
    }
    return count;
}

unsigned i2s_audio_fill_buffer_8bit(int16_t *out, unsigned capacity,
                                    const uint8_t *src, int start_index,
                                    unsigned count, int volume_adjustment,
                                    float asset_gain) {
    const float mult = i2s_audio_volume_multiplier(volume_adjustment, asset_gain);
    unsigned i;
    for (i = 0u; i < capacity; i++) {
        if (i < count) {
            const int16_t s = i2s_audio_expand_8bit(src[start_index + (int)i]);
            out[i] = i2s_audio_apply_volume(s, mult);
        } else {
            /* NOT in the original's playing8BitAudio branch (rpI2S.cpp:808-822
             * loops only to iCurrentBuffSize) -- see the header's "buffer-fill
             * deviations" note for why this port zero-pads here anyway. */
            out[i] = 0;
        }
    }
    return count;
}

#ifndef HOST_TEST
#include "common/diag.h"
#include "platform/board.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"

static const struct pio_program i2s_audio_program = {
    .instructions = i2s_audio_program_instructions,
    .length = 8u,
    .origin = -1,   /* floating -- see the header's PIO note */
};

/* One retained state machine's worth of driver state. Only one I2S speaker
 * exists on this board, so -- like ws2812_driver.c's s_pio/s_sm and
 * pdm_mic.c's equivalents -- this is a single static instance, not a context
 * struct threaded through every call. */
static PIO      s_pio;
static uint     s_sm;
static int      s_dma_a = -1;
static int      s_dma_b = -1;
static bool     s_ready;

/* Where pio_add_program() put the program. Retained ONLY so unpark_internal()
 * can jump back to it: resuming a parked SM anywhere else is mid-frame, and
 * the next note comes out garbled. This is the value an application cannot
 * obtain, which is why parking has to live in here -- see the header. */
static uint     s_offset;
static bool     s_parked;             /* SM stopped: true silence, no BCLK  */

static i2s_audio_buffer_t s_active;   /* which buffer is currently playing */
static bool     s_playing;            /* idle vs mid-playback              */

static const void *s_src;
static bool        s_is_8bit;
static bool        s_mono;
static int         s_total_samples;
static int         s_current_sample;
static int         s_volume = 10;      /* m_iVolumeAdjustment's default    */
static float       s_asset_gain = 1.0f;/* m_fSingleAssetVolumeAdjustment's default */

/* The two static ping-pong buffers -- btBufferA/btBufferB in the original.
 * Static, not malloc'd, for the same reason pdm_mic.c's raw buffers are:
 * a failed allocation on this board's small SRAM is a worse failure mode
 * than a fixed reservation (2 * 1024 * 2 bytes = 4 KiB total here). */
static int16_t s_buf_a[I2S_AUDIO_BUFF_SIZE];
static int16_t s_buf_b[I2S_AUDIO_BUFF_SIZE];

/* How many REAL (non-padding) samples each buffer's MOST RECENT fill
 * produced -- indexed by i2s_audio_buffer_t. See the header's fix-round-1
 * note: this, not the fill cursor, is what i2s_audio_process() checks
 * before it is allowed to stop the chain, because the fill cursor runs up
 * to one whole buffer ahead of what the hardware has actually been told to
 * play. */
static unsigned s_buf_consumed[2];

static int dma_channel_for(i2s_audio_buffer_t buf) {
    return (buf == I2S_AUDIO_BUFFER_A) ? s_dma_a : s_dma_b;
}

static int16_t *buffer_for(i2s_audio_buffer_t buf) {
    return (buf == I2S_AUDIO_BUFFER_A) ? s_buf_a : s_buf_b;
}

/* Fill one ping-pong buffer from the caller's source, advancing
 * s_current_sample by however many input samples were consumed -- the
 * i2s_audio_fill_count()-sized chunk fillBuffer() computes per call
 * (rpI2S.cpp:773-776). */
static void refill(i2s_audio_buffer_t buf) {
    int16_t *out = buffer_for(buf);
    const unsigned count = i2s_audio_fill_count(s_total_samples, s_current_sample,
                                                I2S_AUDIO_BUFF_SIZE);
    unsigned consumed;
    if (s_is_8bit) {
        consumed = i2s_audio_fill_buffer_8bit(out, I2S_AUDIO_BUFF_SIZE,
                                              (const uint8_t *)s_src,
                                              s_current_sample, count,
                                              s_volume, s_asset_gain);
    } else {
        consumed = i2s_audio_fill_buffer(out, I2S_AUDIO_BUFF_SIZE,
                                         (const int16_t *)s_src,
                                         s_current_sample, count, s_mono,
                                         s_volume, s_asset_gain);
    }
    s_current_sample += (int)consumed;
    s_buf_consumed[(int)buf] = consumed;
}

/* setupDMAChain(), transcribed from rpI2S.cpp:890-929. Channel A starts
 * immediately; channel B is configured but not triggered -- it only starts
 * because channel A's completion chains into it in hardware (trap 4). */
static void configure_dma_chain(void) {
    volatile void *dst = (volatile void *)&s_pio->txf[s_sm];

    dma_channel_config b_cfg = dma_channel_get_default_config((uint)s_dma_b);
    channel_config_set_read_increment(&b_cfg, true);
    channel_config_set_write_increment(&b_cfg, false);
    channel_config_set_transfer_data_size(&b_cfg, DMA_SIZE_16);
    channel_config_set_dreq(&b_cfg, pio_get_dreq(s_pio, s_sm, true));
    channel_config_set_chain_to(&b_cfg, (uint)s_dma_a);
    dma_channel_configure((uint)s_dma_b, &b_cfg, dst, s_buf_b,
                         I2S_AUDIO_BUFF_SIZE, false);

    dma_channel_config a_cfg = dma_channel_get_default_config((uint)s_dma_a);
    channel_config_set_read_increment(&a_cfg, true);
    channel_config_set_write_increment(&a_cfg, false);
    channel_config_set_transfer_data_size(&a_cfg, DMA_SIZE_16);
    channel_config_set_dreq(&a_cfg, pio_get_dreq(s_pio, s_sm, true));
    channel_config_set_chain_to(&a_cfg, (uint)s_dma_b);
    dma_channel_configure((uint)s_dma_a, &a_cfg, dst, s_buf_a,
                         I2S_AUDIO_BUFF_SIZE, true);
}

/* ---- Park: the only true mute this board has ----
 *
 * IC17 has no shutdown line (trap 2), so the amplifier amplifies whatever it
 * is clocked. A RUNNING state machine with a drained FIFO keeps re-shifting
 * its last contents indefinitely -- silence from boot, because that content
 * is zeros, but NOISE after a note, because then it is audio. Same mechanism,
 * opposite outcome, decided entirely by what played last. Stopping the SM
 * stops BCLK and LRCLK, and an amplifier with no bit clock has nothing to
 * amplify.
 *
 * Feeding zeros from software instead cannot work: the SM consumes 8000
 * frames/s and a 4-entry FIFO holds 0.5 ms, so a 2 ms application loop leaves
 * the FIFO empty most of every iteration no matter how tight it is. */
static void park_internal(void) {
    if (s_parked) return;
    pio_sm_set_enabled(s_pio, s_sm, false);
    /* Drop anything still queued, so a resume starts from a known state
     * rather than shifting out a sample from the note that just ended. */
    pio_sm_clear_fifos(s_pio, s_sm);
    s_parked = true;
}

/* Resume FRAME-ALIGNED, which is the part that makes parking safe.
 *
 * pio_sm_restart() clears the shift counters and the clock divider phase but
 * NOT the program counter, so a bare re-enable resumes at whatever instruction
 * the SM happened to stop on -- mid-frame -- and the next note is misaligned.
 * That failure is INTERMITTENT, so it looks like it works. The jmp back to
 * s_offset is what prevents it, and this is the same sequence
 * i2s_audio_init() uses to start the SM in the first place.
 *
 * The `set x` is equally load-bearing and was MISSING from the first version
 * of this function, on the mistaken reasoning that the program reloads its own
 * counter every frame. It does -- at instructions 3 and 7, which are AFTER the
 * left channel, so the first channel out of any entry uses the STALE value.
 * That desynchronises the 32-bit autopull boundary from the L/R frame and
 * every sample after it is bit-shifted: loud digital noise, found on hardware
 * 2026-07-29. Restart, SET X, jump, enable -- in that order, every time. */
static void unpark_internal(void) {
    if (!s_parked) return;
    pio_sm_restart(s_pio, s_sm);
    pio_sm_exec(s_pio, s_sm, pio_encode_set(pio_x, I2S_AUDIO_X_INIT));
    pio_sm_exec(s_pio, s_sm, pio_encode_jmp(s_offset));
    pio_sm_set_enabled(s_pio, s_sm, true);
    s_parked = false;
}

/* finishAndReset(), transcribed from rpI2S.cpp:981-1000, minus the
 * m_bFillBuffFromFile file-close and m_bPlayNumberInProgress chaining
 * (both app-layer, out of scope). See the header's trap 4 note for why both
 * channels are aborted unconditionally.
 *
 * The two bare dma_channel_abort() calls on a MUTUALLY CHAINED pair (A->B,
 * B->A) look like the fault wilibsp fixed on the FreeWili 2 by switching to
 * dma_channel_cleanup(), which clears CHAIN_TO and EN first. MEASURED on this
 * board, 2026-07-28: it is not happening here. dma_busy read 0x000 across all
 * 12 channels for 33 consecutive one-second samples while noise was audible,
 * so the aborts do stop this chain on RP2040 -- and the SDK documents the
 * re-trigger erratum as RP2350-E5, i.e. not this part. Recorded so the next
 * reader who spots the bare aborts does not spend a session on it: the noise
 * that looks like a runaway DMA is the parked-SM problem above, not this.
 *
 * The zero sample is kept as a faithful transcription of finishAndReset()'s
 * own sendI2SData(0), but the park_internal() call at the end of this
 * function supersedes it -- a stopped SM shifts nothing at all, which is what
 * the zero push was reaching for. */
static void stop_internal(void) {
    dma_channel_abort((uint)s_dma_a);
    dma_channel_abort((uint)s_dma_b);
    s_asset_gain = 1.0f;
    pio_sm_put(s_pio, s_sm, 0u);
    s_total_samples = 0;
    s_playing = false;
    park_internal();
}

bool i2s_audio_init(PIO pio, uint sm) {
    if (s_ready) {
        DIAG("[i2s] already initialised (dma ch %d/%d, pio%u sm%u); ignoring "
             "repeat i2s_audio_init() call\n",
             s_dma_a, s_dma_b, (unsigned)pio_get_index(s_pio), s_sm);
        return true;
    }

    /* Trap 1: two non-panicking claims, with rollback at every partial-
     * failure point -- see the header for the three cases. */
    const int ch_a = dma_claim_unused_channel(false);
    if (ch_a < 0) {
        DIAG("[i2s] no free DMA channel (a); audio not started\n");
        return false;
    }
    const int ch_b = dma_claim_unused_channel(false);
    if (ch_b < 0) {
        DIAG("[i2s] no free DMA channel (b); releasing channel %d\n", ch_a);
        dma_channel_unclaim((uint)ch_a);
        return false;
    }

    if (!pio_can_add_program(pio, &i2s_audio_program)) {
        DIAG("[i2s] no room for the PIO program on pio%u\n",
             (unsigned)pio_get_index(pio));
        dma_channel_unclaim((uint)ch_a);
        dma_channel_unclaim((uint)ch_b);
        return false;
    }
    const int offset = pio_add_program(pio, &i2s_audio_program);

    /* Park DIN/LRCLK/BCLK as GPIO outputs low, transcribed from
     * rpI2S.cpp:179-187, before PIO takes over the pins below. */
    gpio_init(PIN_I2S_LRCLK);
    gpio_put(PIN_I2S_LRCLK, 0);
    gpio_set_dir(PIN_I2S_LRCLK, GPIO_OUT);
    gpio_init(PIN_I2S_BCLK);
    gpio_put(PIN_I2S_BCLK, 0);
    gpio_set_dir(PIN_I2S_BCLK, GPIO_OUT);
    gpio_init(PIN_I2S_DIN);
    gpio_put(PIN_I2S_DIN, 0);
    gpio_set_dir(PIN_I2S_DIN, GPIO_OUT);

    pio_gpio_init(pio, PIN_I2S_LRCLK);
    pio_gpio_init(pio, PIN_I2S_BCLK);
    pio_gpio_init(pio, PIN_I2S_DIN);

    /* audio_i2s_program_get_default_config() + audio_i2s_program_init(),
     * transcribed from rpI2S.cpp:98-115, against the CAPTURED offset rather
     * than a caller-supplied one -- see the header's PIO note. */
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, (uint)offset, (uint)offset + 7u);
    sm_config_set_sideset(&c, 2, false, false);
    sm_config_set_out_pins(&c, PIN_I2S_DIN, 1);
    sm_config_set_sideset_pins(&c, PIN_I2S_LRCLK);
    sm_config_set_out_shift(&c, false, true, 32);
    pio_sm_init(pio, sm, (uint)offset, &c);

    const uint pin_mask = (1u << PIN_I2S_DIN) | (3u << PIN_I2S_LRCLK);
    pio_sm_set_pindirs_with_mask(pio, sm, pin_mask, pin_mask);
    pio_sm_set_pins(pio, sm, 0);
    /* Entry-point jump, transcribed from rpI2S.cpp:114.
     *
     * This does NOT pre-set x, whatever the original's intent was: encoding a
     * JMP moves the program counter to offset+7, it does not EXECUTE the
     * `set x, 14` that lives there -- and the restart-and-jump below sends the
     * PC back to offset+0 before the SM ever steps. Kept because it is
     * harmless and faithful; the explicit `set x` below is what actually
     * establishes the counter. See I2S_AUDIO_X_INIT. */
    pio_sm_exec(pio, sm, pio_encode_jmp((uint)offset + 7u));

    /* update_pio_frequency(pio, sm, 8000) -- transcribed from rpI2S.cpp:249,
     * against the live system clock (trap 6: integer divider only). */
    const uint32_t divider = i2s_audio_pio_divider((uint32_t)clock_get_hz(clk_sys), 8000u);
    pio_sm_set_clkdiv_int_frac(pio, sm, (uint16_t)(divider >> 8u), 0);

    /* rpI2S.cpp:251-253's own restart-and-jump-and-enable, run again after
     * audio_i2s_program_init()'s internal one -- see the header's PIO note
     * for why both are preserved.
     *
     * The `set x` is NOT from the original and is not optional: without it the
     * SM starts with whatever x the hardware last held (0 out of reset), the
     * first left channel is the wrong length, and every 32-bit word after it
     * is misaligned against the L/R frame. From cold that is inaudible only
     * because nothing is playing yet -- see I2S_AUDIO_X_INIT. */
    pio_sm_restart(pio, sm);
    pio_sm_exec(pio, sm, pio_encode_set(pio_x, I2S_AUDIO_X_INIT));
    pio_sm_exec(pio, sm, pio_encode_jmp((uint)offset));
    pio_sm_set_enabled(pio, sm, true);

    s_pio = pio;
    s_sm = sm;
    s_dma_a = ch_a;
    s_dma_b = ch_b;
    s_offset = (uint)offset;
    s_parked = false;      /* set_enabled(true) above left it running */
    s_volume = 10;
    s_asset_gain = 1.0f;
    s_playing = false;
    s_ready = true;

    DIAG("[i2s] dma ch %d/%d, pio%u sm%u, clkdiv ~%u\n", ch_a, ch_b,
        (unsigned)pio_get_index(pio), sm, (unsigned)(divider >> 8u));
    return true;
}

bool i2s_audio_start(const void *samples, unsigned count, bool force_mono,
                     bool is_8bit) {
    if (!s_ready || s_playing) return false;
    if (!samples || !count) return false;

    s_src = samples;
    s_is_8bit = is_8bit;
    s_mono = force_mono;
    s_total_samples = (int)count;
    s_current_sample = 0;

    refill(I2S_AUDIO_BUFFER_A);
    /* ALWAYS fill B too, unlike startSound()'s conditional pre-fill -- see
     * the header's "buffer-fill deviations" note for the stale-buffer
     * hazard this closes. */
    refill(I2S_AUDIO_BUFFER_B);

    /* Unpark BEFORE the chain is armed: configure_dma_chain() triggers
     * channel A immediately, and a stopped SM never drains its FIFO, so the
     * transfer would stall until something re-enabled it. Callers never have
     * to think about parking because of this line. */
    unpark_internal();

    configure_dma_chain();
    s_active = I2S_AUDIO_BUFFER_A;
    s_playing = true;
    return true;
}

void i2s_audio_process(void) {
    if (!s_ready || !s_playing) return;

    if (dma_channel_is_busy((uint)dma_channel_for(s_active))) return;

    /* Fix round 1: the stop gate is whether the SIBLING buffer -- the one
     * the hardware chain has already auto-triggered, or is about to, the
     * instant s_active's transfer completed -- holds any REAL content from
     * its own most recent fill, NOT whether the fill cursor has reached
     * the end. See the header's fix-round-1 note: checking the fill
     * cursor here stops the chain up to one whole buffer before that
     * buffer has actually been given a chance to play, truncating real
     * audio. */
    const i2s_audio_buffer_t sibling = i2s_audio_other_buffer(s_active);
    if (i2s_audio_chain_drained(s_buf_consumed[(int)sibling])) {
        stop_internal();
        return;
    }

    /* Reset the just-completed channel's read address back to its buffer's
     * start BEFORE refilling it -- rpI2S.cpp:951/:971's
     * `dma_channel_set_read_addr(..., false)`, without which its next
     * hardware chain-trigger would read from wherever the transfer just
     * completed (the buffer's end), not its start. */
    dma_channel_set_read_addr((uint)dma_channel_for(s_active),
                             buffer_for(s_active), false);
    refill(s_active);
    s_active = sibling;
}

void i2s_audio_stop(void) {
    if (!s_ready) return;
    stop_internal();
}

void i2s_audio_park(void) {
    if (!s_ready) return;
    if (s_playing) stop_internal();   /* which parks */
    park_internal();                  /* idempotent */
}

bool i2s_audio_is_parked(void) {
    return s_parked;
}

bool i2s_audio_is_idle(void) {
    return !s_playing;
}

void i2s_audio_set_volume(int volume_adjustment) {
    if (volume_adjustment > 10) volume_adjustment = 10;
    if (volume_adjustment < 0) volume_adjustment = 0;
    s_volume = volume_adjustment;
}

void i2s_audio_set_asset_gain(float gain) {
    s_asset_gain = gain;
}
#endif
