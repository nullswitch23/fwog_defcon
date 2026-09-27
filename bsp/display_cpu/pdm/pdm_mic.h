/* PDM microphone capture on PIN_MIC_CLK (GPIO 17) / PIN_MIC_DATA (GPIO 29) --
 * the display CPU's single onboard microphone, IC18 (MP34DT06JTR).
 * bsp/display_cpu/platform/board.h -- PIN_MIC_CLK, PIN_MIC_DATA.
 *
 * Ported from rmpLib/rpMICpdm.{h,cpp} (365 lines), which itself wraps the
 * third-party pdm_microphone.{c,h,pio} PIO/DMA capture library (Arm Ltd,
 * Apache-2.0). This is the SECOND DMA claimant in this BSP (after
 * lcd/st7789.c's permanent fill channel) and the FIRST to install a DMA
 * interrupt handler (DMA_IRQ_0, exclusive). The display CPU's 12 DMA channels
 * are claimed as: 1 permanent fill channel (lcd/st7789.c), 1 capture channel
 * (here), and a hardware-chained pair (audio/i2s_audio.c). The hardware traps
 * this port depends on are documented inline below.
 *
 * ---- PIO block and state machine are caller-chosen, like ws2812_driver.c --
 * This driver shares its PIO block with WS2812 (4 words) and, later, I2S
 * (8 words) -- 16 of 32 words total, one block, three state machines. IR
 * gets the other PIO block entirely to itself (25 of 32 words). pdm_mic_init()
 * below takes `pio` and `sm` as parameters and honours them, exactly as
 * ws2812_init() does, instead of hardcoding pio0/sm0 the way the legacy
 * rpNeoPixel::init() did before that bug was fixed.
 *
 * ---- Trap 1: non-panicking DMA claim, with an explicit failure path ----
 * pdm_microphone.c:109 claims its DMA channel with
 * `dma_claim_unused_channel(true)` -- the PANICKING form -- and if that
 * somehow still failed, nothing above it has any fallback at all: the
 * microphone would silently never start. This project's convention (the
 * catalog's DMA note, `lcd/st7789.c:196`) is `dma_claim_unused_channel(false)`
 * with the failure handled explicitly, because the display CPU is the one
 * processor on this board that cannot be re-flashed without physical BOOTSEL
 * access it does not have -- a panic in a display binary is the single most
 * expensive failure mode available. pdm_mic_init() below claims with
 * `(false)`, and on failure logs via DIAG() and returns false without
 * touching the PIO program space at all (the DMA channel is claimed BEFORE
 * `pio_add_program()` runs, precisely so a failed claim never needs to undo a
 * program load). A failed init leaves the driver not-ready: no capture, no
 * DIAG spam beyond the one line, no half-configured hardware.
 *
 * ---- Trap 2: this driver installs an EXCLUSIVE DMA_IRQ_0 handler ----
 * `pdm_microphone.c:179` (`irq_set_exclusive_handler(pdm_mic.dma_irq,
 * pdm_dma_handler)`) claims the whole of DMA_IRQ_0 for itself, and
 * `irq_set_exclusive_handler()` asserts if a second driver ever tries to
 * install its own handler on the same IRQ number. THIS IS THE ONLY DRIVER IN
 * THIS BSP THAT INSTALLS A DMA IRQ HANDLER, as of this port. That is safe
 * today because nothing else does -- ST7789's DMA channel is deliberately
 * `irq_quiet` (see st7789.c's comment) and raises no interrupt at all.
 *
 * WHEN I2S IS PORTED NEXT, IT MUST NOT CALL `irq_set_exclusive_handler(
 * DMA_IRQ_0, ...)`. The legacy `rpI2S` polls its two chained DMA channels
 * rather than using an interrupt at all (recon-dma.md), so there is no
 * conflict coming from I2S's own reference behaviour -- but a future port
 * that "modernises" I2S to interrupt-driven completion must pick a DIFFERENT
 * IRQ (DMA_IRQ_1) or share this one cooperatively, not call the exclusive
 * form again. This paragraph is the second place that ownership is recorded;
 * the first is the peripheral catalog's DMA budget note.
 *
 * ---- Trap 3: the clock edge that carries valid data ----
 * IC18's L/R pin (pin 2) is strapped to GND (recon-schematic.md trap 3),
 * which per the MP34DT06J datasheet (DS12698 Rev 2, Table 6, "L/R channel
 * selection") means:
 *
 *     L/R = GND:  CLK low  -> data valid
 *                 CLK high -> high impedance
 *
 * i.e. DOUT is only driven while CLK is low, and floats while CLK is high.
 * The legacy PIO program (pdm_microphone.pio, transcribed unchanged below)
 * is:
 *
 *     nop              side 0   ; CLK low  (cycle 0)
 *     in   pins, 1     side 0   ; CLK low  (cycle 1) -- SAMPLE HERE
 *     push iffull noblock side 1 ; CLK high (cycle 2)
 *     nop              side 1   ; CLK high (cycle 3)
 *
 * The `in pins, 1` instruction -- the only one that reads DOUT -- executes in
 * cycle 1, the SECOND of the two `side 0` (CLK-low) cycles, i.e. in the last
 * moment before CLK rises. That is squarely inside the "CLK low -> data
 * valid" window the datasheet documents for L/R=GND. The datasheet and the
 * original PIO program AGREE: this port samples the same edge the original
 * does, transcribed unchanged, because it is already correct for how this
 * board's L/R pin is strapped. (Compare the hardware record fact 33 and
 * ws2812_driver.h's long note: that port had to trust the reference against
 * an SDK example that looked "more standard" but was wrong for this board's
 * hardware. Here the reference and the datasheet already agree, so there is
 * nothing to second-guess.)
 *
 * ---- Trap 4: the decimation filter is NOT ported ----
 * `Open_PDM_Filter_64()` / `Open_PDM_Filter_Init()` live in a third-party
 * `OpenPDMFilter` library that is not part of `rpMICpdm.{h,cpp}`,
 * `pdm_microphone.{c,h,pio}`, or this BSP. This port carries only the
 * CAPTURE path -- PIO program, DMA channel, double-buffered raw PDM bit
 * handoff -- and exposes raw PDM buffers to the caller via
 * pdm_mic_take_raw_buffer() below. No decimation filter is invented here.
 * A future decimation stage (porting OpenPDMFilter, or a simpler CIC/boxcar
 * filter written against this BSP's own conventions) is what would turn
 * these raw 1-bit-per-sample buffers into 16-bit PCM; see the port report
 * for a concrete recommendation.
 *
 * AMENDED: that filter now exists, and OpenPDMFilter is still not it. The
 * decimator is common/dsp/cic.{c,h} -- a 3rd-order CIC ADAPTED FROM wilibsp
 * (the FreeWili 2 BSP, MIT, same copyright holder), whose decimation factor is
 * 64, exactly this path's PDM_DECIMATION, and which is already verified on FW2
 * hardware. pdm_mic_decode() below is the binding. Everything above stays
 * true: this driver still carries only the CAPTURE path, the filter is a
 * separate pure module under bsp/common/, and ST's Apache-2.0 OpenPDMFilter.c
 * is still deliberately not imported. Adapting a proven sibling beat writing a
 * third implementation.
 *
 * ---- Trap 5: the genuinely pure logic -- volume scaling and threshold
 *      detection -- IS ported and host-tested ----
 * `rpMICpdm::process()` (`:90-129`) does two things to an ALREADY-DECIMATED
 * int16 sample buffer: a volume-scaling multiply and a loudness-threshold
 * check. Both are pure integer arithmetic with no PIO/DMA/filesystem
 * dependency, so both are ported here as free functions -- pdm_scale_sample()
 * and pdm_analyze_buffer() -- that operate on a caller-supplied int16 array.
 * They are NOT wired to the raw capture path automatically, because nothing
 * in this BSP performs the decimation that would bridge "raw PDM bits" to
 * "int16 samples" (trap 4) -- an app or a future filter stage is expected to
 * call pdm_analyze_buffer() once it has decimated samples of its own.
 *
 * pdm_scale_sample() is transcribed BIT-FOR-BIT from `process()`'s two
 * branches, including a genuine asymmetry in the original: for
 * `m_iVolumeAdjustment > 5` the scaling amplifies (correct: higher setting,
 * louder). For `m_iVolumeAdjustment < 5` the coded expression is
 * `cur -= raw * (26*(volume-5))`; since `volume-5` is already negative there,
 * the double negation makes this branch ALSO amplify, by the same factor a
 * symmetric volume ABOVE 5 would use (verified in tests/test_pdm.c: volume 4
 * and volume 6 produce IDENTICAL scaling, and volume 0 and volume 10 produce
 * IDENTICAL scaling). A volume control where turning it below the midpoint
 * makes the signal louder, not quieter, looks like a bug. It is transcribed
 * unchanged rather than "fixed", for the same reason ws2812_driver.h's side-
 * set polarity was not "fixed": this is arithmetic, not a hardware polarity
 * question a schematic or datasheet can settle, and this port's brief is to
 * find defects "in a path the original never exercised" and prove them, not
 * to invent corrections to control-surface arithmetic on suspicion alone. If
 * `m_iVolumeAdjustment` is ever driven below 5 in practice, whoever owns that
 * control should decide whether this is intentional (e.g. a "boost either
 * way from center" control) or a genuine off-by-sign bug, with the product's
 * actual UI in front of them -- this port only documents and preserves it.
 *
 * This is NOT a hypothetical, unexercised path. `FreeWilliDisplay.cpp:807`
 * feeds `obMIC.m_iVolumeAdjustment` from `obMenuSettings.m_iSettingSoundVolume`,
 * a user-editable setting (`fwMenuSettingsDisplay.h:143`, default 5, read via
 * `sscanf(szValue, "%d", ...)` with no visible clamp for the MIC assignment
 * specifically) -- so a user turning this down in the settings menu reaches
 * `m_iVolumeAdjustment < 5` directly.
 *
 * CORRECTION (fix round 1): an earlier version of this note claimed the
 * IDENTICAL formula "exists for I2S playback volume" at `rpI2S.cpp:853-865`
 * and told whoever ports I2S next to "expect to find this exact pattern
 * again." That was wrong, and was verified wrong directly against
 * `rpI2S.cpp` rather than taken on faith: `:852-869` is wrapped in
 * `#if (0)`, carrying the original author's own comment --
 * `//skellenger: removing this for now since m_iVolumeAdjustment == 5 and
 * this code is ignored anyway` -- so that block never executes on I2S. I2S's
 * REAL, LIVE volume path is a different mechanism entirely:
 * `VOLUME_MULTIPLIERS[m_iVolumeAdjustment] * m_fSingleAssetVolumeAdjustment`
 * (`rpI2S.cpp:771`), with `m_iVolumeAdjustment` clamped to 0-10 immediately
 * above (`:767-770`) and fed by `rpPanelManager.cpp:514,551`'s explicit
 * increment/decrement handlers. Whoever ports I2S next should DROP the dead
 * `#if (0)` block entirely rather than transcribe it, and port the
 * `VOLUME_MULTIPLIERS` lookup as the real path -- do not expect to find "this
 * exact pattern again"; that was never true for I2S.
 *
 * ---- Trap 6: single DMA channel, SOFTWARE re-arm, two static buffers ----
 * `pdm_microphone.c:248`'s `pdm_dma_handler()` re-triggers the SAME DMA
 * channel from inside the completion ISR
 * (`dma_channel_transfer_to_buffer_now()`), alternating between two
 * statically-allocated raw buffers (`btBufferA`/`btBufferB` there,
 * `s_raw_buf_a`/`s_raw_buf_b` here) by SOFTWARE index arithmetic --
 * `(write_index + 1) % PDM_RAW_BUFFER_COUNT`. This is NOT a hardware DMA
 * chain (there is no second channel, no linked `CHAIN_TO`). Preserved
 * exactly as this shape: one channel, software re-arm, two buffers. I2S is
 * the driver that uses a real hardware chain of two channels, and it comes
 * later -- do not "upgrade" this driver to match it.
 *
 * ---- Trap 7: the ISR-shared ready flag is guarded, on purpose ----
 * `rpMICpdm.cpp:84,120` guards `g_bHasSamples` -- read from the main loop,
 * written from the DMA ISR -- with `save_and_disable_interrupts()` /
 * `restore_interrupts()`. That pattern is correct as written and is
 * preserved here: `pdm_mic_take_raw_buffer()` below disables interrupts
 * around its read-and-clear of the equivalent ready flag and ready-buffer
 * index, for exactly the same reason -- a read that raced the ISR could
 * observe a flag that was true a moment ago but whose buffer index has
 * already been advanced out from under it, handing the caller a buffer the
 * DMA channel is now overwriting.
 *
 * ---- Trap 8 (fix round 1): pdm_mic_init() must be idempotent ----
 * A second call to `pdm_mic_init()` while capture is already running used to
 * be latent but genuinely dangerous, not merely wasteful:
 *
 *   - it claims a NEW DMA channel via `dma_claim_unused_channel(false)`
 *     without ever unclaiming the old one -- the old channel is leaked AND
 *     stays armed;
 *   - the orphaned old channel keeps running, and when it completes it
 *     raises `DMA_IRQ_0`, but `pdm_mic_isr()` clears `dma_hw->ints0` only for
 *     the CURRENT `s_dma_ch` -- the orphan's bit is never acknowledged.
 *     `DMA_INTR` is level-sensitive and write-to-clear, so an unacknowledged
 *     completion re-fires the handler in a tight loop forever;
 *   - `pio_add_program()` re-adds the 4-word program with nothing removing
 *     the first copy, consuming more of the tightly-budgeted shared PIO
 *     block (WS2812 + PDM + I2S share one block, 16 of 32 words already
 *     spoken for -- see recon-dma.md).
 *
 * An unacknowledged, level-sensitive IRQ that loops forever presents as a
 * hang -- on the display CPU, the one processor in this system with NO
 * reachable BOOTSEL button. This is exactly the failure mode this driver's
 * whole DMA-claim convention (trap 1) exists to avoid, so `pdm_mic_init()`
 * guards against it directly:
 *
 *     if (!pdm_mic_needs_init(s_ready)) { ...DIAG()...; return true; }
 *
 * `pdm_mic_needs_init()` is a pure, host-tested predicate (see
 * tests/test_pdm.c) -- extracted the same way `pdm_mic_next_buffer_index()`
 * is, because `pdm_mic_init()` itself is hardware-only and this driver has no
 * host-side fake for `dma_claim_unused_channel()`/`pio_add_program()` to
 * exercise the guard end to end. A repeat call while already ready returns
 * `true` (capture is already running -- that is success, not failure) and
 * touches nothing: no new DMA claim, no new PIO program, the original
 * `pio`/`sm` keep running exactly as first configured. This is a GUARD, not a
 * teardown-then-reinit: it mirrors `st7789_init_begin()`'s own
 * `if (s_state != ST7789_INIT_IDLE) return;` idempotency check rather than
 * inventing a new pattern, and it avoids contradicting this driver's own
 * `pdm_mic_stop()` note below that this BSP's drivers never release a claim
 * once taken. Calling `pdm_mic_init()` again after an explicit
 * `pdm_mic_stop()` remains out of scope (see `pdm_mic_stop()`'s doc comment)
 * -- this guard only covers the two-inits-with-no-stop-between case the
 * finding was about, which is also the only one that was ever silently
 * reachable.
 *
 * ---- GPIO 29 note ----
 * The schematic calls this net `MIC_SIG`, not `MIC_DATA`
 * (recon-schematic.md) -- a future reader grepping the schematic for
 * `MIC_DATA` finds nothing. GPIO 29 is also `PIN_FPGA_RESET`, a driven
 * output, on a MAIN-CPU image (the hardware record) -- this driver configures it as a
 * PIO INPUT, so there is no conflict within a correct display image; the
 * hazard is purely "do not test this with a main-CPU image on the board",
 * which `fw bootsel --cpu display` now prevents.
 */
#ifndef FWOG_PDM_MIC_H
#define FWOG_PDM_MIC_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "common/dsp/cic.h"

/* ---- Board-fixed capture parameters ----
 * Transcribed from rpMICpdm::init()'s `stPDMConfig` (sample_rate=8000,
 * sample_buffer_size=256 -- `MIC_SAMPLE_BUFF_SIZE`) and pdm_microphone.c's
 * fixed `PDM_DECIMATION` (64) and `PDM_RAW_BUFFER_COUNT` (2). PDM_DECIMATION
 * here is the PDM OVERSAMPLING RATIO used to size the raw capture buffer and
 * derive the PIO clock divider -- it is NOT the decimation filter itself
 * (trap 4): no filter is ported, only the capture-side arithmetic that used
 * to feed one. */
#define PDM_SAMPLE_RATE_HZ     8000u
#define PDM_DECIMATION         64u
#define PDM_SAMPLE_BUFFER_SIZE 256u
#define PDM_RAW_BUFFER_COUNT   2u
/* One raw buffer's size in bytes: 256 * (64/8) = 2048, matching the legacy
 * `btBufferA`/`btBufferB[2048]`. */
#define PDM_RAW_BUFFER_BYTES   (PDM_SAMPLE_BUFFER_SIZE * (PDM_DECIMATION / 8u))

/* The decimation filter's factor and this capture path's oversampling ratio
 * are the same number and must stay so. common/dsp/cic.h cannot include this
 * header (bsp/common/ has no business reaching into a display-CPU header, and
 * this one pulls in hardware/pio.h outside HOST_TEST), so the agreement is
 * asserted instead of shared -- the same belt-and-braces app_meta.h's flash
 * offset uses against the CMake side. A mismatch is a compile error rather
 * than a wrong sample rate discovered at a bench. */
_Static_assert(FWOG_CIC_DECIMATE == PDM_DECIMATION,
               "cic.h's FWOG_CIC_DECIMATE and pdm_mic.h's PDM_DECIMATION "
               "have drifted apart");

/* ---- Pure: host-tested, no PIO/DMA ---- */

/* PIO clock divider for this board's PDM capture, given the live system
 * clock in Hz. Transcribed from pdm_microphone.c:118
 * (`clock_get_hz(clk_sys) / (sample_rate * PDM_DECIMATION * 4.0)` -- the `4`
 * is this PIO program's fixed 4 cycles per captured bit), expressed against a
 * parameter instead of calling clock_get_hz() directly so it is
 * host-testable. Never hardcode this: this board's clk_sys is 200 MHz, not
 * the Pico SDK's 125 MHz default (see the test for both). */
float pdm_mic_clkdiv(uint32_t sys_clk_hz);

/* Ping-pong buffer index arithmetic, transcribed from
 * pdm_microphone.c:245's `(raw_buffer_write_index + 1) % PDM_RAW_BUFFER_COUNT`
 * -- the exact place a software-re-armed double buffer (trap 6) goes wrong if
 * a port gets the wrap condition wrong. Pure and host-tested so that
 * regression is caught without hardware. */
unsigned pdm_mic_next_buffer_index(unsigned current);

/* One sample's volume scaling, transcribed bit-for-bit from
 * rpMICpdm::process()'s two branches (`:94-105`) -- see the header's trap 5
 * note for why the `volume_adjustment < 5` branch amplifies instead of
 * attenuating, and why that is preserved rather than "fixed". `volume==5` is
 * the original's implicit no-op (neither branch taken): the input passes
 * through unchanged. */
int16_t pdm_scale_sample(int16_t raw_sample, int volume_adjustment);

/* Per-buffer analysis, transcribed from rpMICpdm::process()'s main loop
 * (`:90-127`), MINUS the streaming-over-console and record-to-filesystem
 * side effects (`:136-169`) -- those are app-layer concerns (a console
 * object, a file system pointer), exactly as other ports in this BSP drop
 * console formatting and GUI coupling; see mcp7940.c and lis3dh.c's headers
 * for the same rationale.
 *
 * `raw` is `count` ALREADY-DECIMATED int16 samples -- see trap 5 for why this
 * function is not fed directly from the raw PDM capture in this BSP. Writes
 * each input sample's volume-scaled value into `scaled_out[i]` (if non-NULL;
 * pass NULL to skip the scaled copy and only get detection/min/max). Returns
 * whether any sample in the buffer exceeded `threshold` in either direction
 * -- `rpMICpdm::m_bSoundDetected`'s exact condition
 * (`max_sample > threshold || min_sample < -threshold`), where max_sample and
 * min_sample are computed from the RAW (unscaled) samples, exactly as the
 * original does (the threshold check and the volume-scaled output are
 * deliberately independent in the original: turning the volume down does not
 * change whether a loud sound is "detected"). `out_max`/`out_min` (if
 * non-NULL) receive those raw extrema. Returns false without touching any
 * output if `raw` is NULL or `count` is 0. */
bool pdm_analyze_buffer(const int16_t *raw, unsigned count,
                        int volume_adjustment, int16_t threshold,
                        int16_t *scaled_out, int16_t *out_max, int16_t *out_min);

/* Peak-to-peak spread of the 1-bit density across `block_bytes`-sized
 * sub-blocks of a raw PDM buffer -- the AC-sensitive activity metric, and the
 * only one in this BSP that can actually see a tone.
 *
 * WHY THIS EXISTS. The obvious figure to compute from a raw PDM buffer is its
 * whole-buffer 1-bit density, i.e. how far the buffer sits from the 50%
 * baseline PDM idles at. That figure is DC BIAS. An acoustic tone is AC with
 * zero mean, so averaging over a whole 2048-byte buffer (about 32 ms, some 32
 * cycles of a 1 kHz tone) cancels it exactly: the metric physically cannot see
 * the thing it was being used to look for. The 2026-07-28 bench session
 * measured peak bias 15.0 with a verified-audible tone playing against 12.4
 * for silence, ranges fully overlapping, and correctly concluded that this
 * said nothing whatsoever about the driver. A metric that cannot see the thing
 * is worse than no metric, because it produces a number.
 *
 * WHAT THIS DOES INSTEAD. The 1-count of a short sub-block IS a decimated
 * sample -- a boxcar (CIC-1) decimation, the crudest honest decimation filter
 * there is, and (when this was written) the only one available without the
 * third-party OpenPDMFilter that trap 4 deliberately keeps out of this BSP.
 *
 * AMENDED: a real 3rd-order CIC now exists beside it (common/dsp/cic.h,
 * reached through pdm_mic_decode()). THIS FUNCTION STAYS. It is cheap,
 * host-tested, and proven to null exactly where boxcar theory says it must
 * (the hardware record) -- that null is one of the better pieces of evidence in this
 * repo and it does not become less true because a better filter arrived. It is
 * also the independent cross-check for the new path: the two metrics must move
 * together across a stimulus sweep, and disagreement is the signal that the
 * new path is wrong. Two measures agreeing is worth more than either alone. Taking the peak-to-peak
 * spread of those per-block counts therefore measures the waveform's actual
 * excursion rather than its mean. Choose `block_bytes` well below one period
 * of the frequency of interest: capture runs at PDM_SAMPLE_RATE_HZ *
 * PDM_DECIMATION = 512 kHz, so a 1 kHz tone is 512 bits (64 bytes) per period
 * and a 16-byte block is a quarter cycle.
 *
 * Returns max-minus-min of the per-block 1-counts, and writes the number of
 * complete blocks measured to `out_blocks` if non-NULL. A trailing PARTIAL
 * block is dropped rather than measured, because it holds fewer bits and would
 * manufacture spread that is not in the signal. Returns 0, with `*out_blocks`
 * set to 0, if `buf` is NULL, `len` or `block_bytes` is 0, or the buffer does
 * not hold at least TWO complete blocks -- one block has nothing to spread
 * against, and a number there would be indistinguishable from real silence,
 * which is the exact failure this function exists to end. */
unsigned pdm_density_spread(const uint8_t *buf, size_t len,
                            unsigned block_bytes, unsigned *out_blocks);

/* The idempotency guard for pdm_mic_init() (trap 8) -- pure and host-tested
 * because pdm_mic_init() itself is hardware-only. Returns true if init should
 * proceed (claim a DMA channel, add the PIO program, ...), false if it should
 * short-circuit as a no-op because the driver is already ready. `already_ready`
 * is `pdm_mic_init()`'s own `s_ready` at the moment it is called; see the
 * header's trap 8 note for the leaked-channel-plus-unacknowledged-IRQ hang
 * this guard exists to prevent. */
bool pdm_mic_needs_init(bool already_ready);

/* Decode one raw capture buffer to PCM -- the binding between this BSP's flat
 * PDM byte buffers and common/dsp/cic.h's bit-at-a-time filter.
 *
 * `raw` is what pdm_mic_take_raw_buffer() handed back. `out` must hold at
 * least len * 8 / PDM_DECIMATION samples: 2048 bytes in gives 256 samples out.
 * Returns the number of samples written; 0 if any pointer is NULL.
 *
 * ---- BIT ORDER: MSB FIRST WITHIN EACH BYTE. This is the thing that gets
 *      silently wrong. ----
 * pdm_mic.c configures sm_config_set_in_shift(&c, false, false, 8u) --
 * shift_right = false, i.e. shift LEFT -- so the first bit sampled ends up in
 * the MOST significant position of the pushed byte. Bit 7 is therefore the
 * EARLIEST sample and bit 0 the latest, and this function iterates 7 down to 0.
 *
 * An LSB-first loop still produces plausible-looking audio: right amplitude,
 * right rate, wrong content. That is the same failure shape recon-schematic.md
 * trap 3 warns about for the L/R strap -- it fails silently and it looks like a
 * filter bug. The order is derived from the shift configuration above, not from
 * a listening test, and tests/test_cic.c pins it.
 *
 * The related question -- which PDM clock edge carries valid data, given IC18's
 * L/R strapped to GND -- is already settled by construction (trap 3 above, and
 * the hardware record): the capture PIO is verified on hardware and this function does
 * not change it. The filter consumes what capture produces.
 *
 * ---- The state is the caller's ----
 * So consecutive buffers filter continuously across the boundary rather than
 * restarting; restarting per buffer puts a click at every seam. The geometry
 * makes that belt-and-braces rather than load-bearing: PDM_RAW_BUFFER_BYTES is
 * 2048 = 16384 bits, which at 64x is exactly 256 samples = PDM_SAMPLE_BUFFER_
 * SIZE, so one raw buffer decodes to one full sample buffer with nothing left
 * over and the phase accumulator lands back at zero at every boundary. If
 * those numbers ever stop matching, something upstream changed.
 *
 * Pure: no PIO, no DMA, no SDK, and deliberately NOT behind HOST_TEST, so the
 * bit order and the boundary continuity are host-tested like every other
 * decision half in this tree.
 *
 * DC blocking is NOT applied here -- see common/dsp/dcblock.h. */
size_t pdm_mic_decode(fwog_cic_t *st, const uint8_t *raw, size_t len,
                      int16_t *out);

/* The four raw PIO instruction words, transcribed unchanged from
 * pdm_microphone.c:28-35 / pdm_microphone.pio -- side-set polarity included.
 * Exposed here (not behind HOST_TEST -- plain data, no SDK dependency) so
 * tests/test_pdm.c can pin the exact words, the same pattern
 * ws2812_program_instructions[] uses and for the same reason: catch a future
 * "fix" before it reaches hardware. Defined in pdm_mic.c. */
extern const uint16_t pdm_mic_program_instructions[4];

#ifndef HOST_TEST
#include "hardware/pio.h"

/* Claim a DMA channel (non-panicking -- trap 1) and a state machine on the
 * given PIO block, load the capture program, configure the PIO/DMA pair
 * exactly as pdm_microphone_init()+pdm_microphone_start() do, install the
 * exclusive DMA_IRQ_0 handler (trap 2), and start capture immediately.
 *
 * `pio`/`sm` are caller-chosen, never hardcoded -- see the header's PIO note.
 * Uses PIN_MIC_CLK/PIN_MIC_DATA from board.h; this board has exactly one PDM
 * microphone on one pair of pins, so unlike the PIO block/SM that is a board
 * fact, not a caller choice.
 *
 * Returns false, with the driver left not-ready, if either the DMA channel
 * claim or the PIO program load fails. The DMA channel is claimed BEFORE the
 * PIO program is added, so a failed program load unclaims the DMA channel it
 * already took rather than leaking it -- there is no equivalent leak risk in
 * the original because its panicking claim (trap 1) never returns having
 * failed. A call that fails always leaves pdm_mic_take_raw_buffer() reporting
 * no data available, even if an EARLIER call had already succeeded, matching
 * ws2812_init()'s same not-ready-on-failure rule.
 *
 * IDEMPOTENT (trap 8): a second call while capture is already running is a
 * safe no-op that returns true and touches no hardware state -- it does NOT
 * claim another DMA channel, re-add the PIO program, or reinstall the IRQ
 * handler. See the header's trap 8 note for the hang this guards against and
 * `pdm_mic_needs_init()` for the host-tested predicate that implements it. A
 * repeat call is NOT the same as `pdm_mic_stop()` followed by a fresh
 * `pdm_mic_init()` -- that sequence is a different, still-unsupported case;
 * see `pdm_mic_stop()`'s own doc comment. */
bool pdm_mic_init(PIO pio, uint sm);

/* Hand back the most recently completed raw PDM buffer, if one is ready
 * since the last call. `*buf` receives a pointer to PDM_RAW_BUFFER_BYTES
 * bytes of raw, undecimated PDM bitstream (1 bit per sample, MSB-first within
 * each byte, 8 bits per DMA transfer -- see the PIO program's `push iffull`).
 * `*len` receives PDM_RAW_BUFFER_BYTES. Returns false (leaving `*buf`/`*len`
 * untouched) if capture never started, or if no new buffer has completed
 * since the last call.
 *
 * The returned buffer is exactly the other half of the double buffer from
 * the one the DMA channel is currently filling (trap 6): it stays valid
 * until the NEXT buffer completes, which takes PDM_RAW_BUFFER_BYTES * 8 PDM
 * clock cycles (about 32 ms at this board's 512 kHz capture rate). A caller
 * that has not finished with the buffer before then will have it overwritten
 * from underneath it -- the same contract the original's
 * `pdm_microphone_read()` has against `raw_buffer_read_index`. */
bool pdm_mic_take_raw_buffer(const uint8_t **buf, size_t *len);

/* Stop the state machine and abort the DMA channel. Leaves the DMA channel
 * and PIO program claimed (this BSP's drivers never release a claim once
 * taken -- see st7789.c's note) so a later pdm_mic_init() call is not the
 * intended use; this exists for symmetry with pdm_microphone_stop(), not
 * because anything in this BSP calls it today. */
void pdm_mic_stop(void);

#else
/* ogemu: no PIO. `pio` is ignored (callers still pass pio0). Decode is the
 * real CIC in pdm_mic.c when that TU is linked; ogemu supplies its own
 * take/decode pair so a WAV or mic_rms command can feed TalkClip/VoltPet. */
bool pdm_mic_init(void *pio, unsigned sm);
bool pdm_mic_take_raw_buffer(const uint8_t **buf, size_t *len);
void pdm_mic_stop(void);
#endif
#endif
