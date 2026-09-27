/* 3rd-order CIC decimator: 1-bit PDM in, 16-bit PCM out.
 *
 * ---- Provenance ----
 * Adapted from wilibsp (github.com/freewili/wilibsp), bsp/dsp/cic.{c,h} -- the
 * FreeWili 2 BSP, MIT licensed, same copyright holder, and already verified on
 * FW2 hardware (its own end-to-end PDM findings record four channels alive
 * with room-noise events tracking across all of them). MIT requires the notice
 * retained; see THIRD-PARTY-NOTICES.md.
 *
 * The core arithmetic is transcribed UNCHANGED, in the same spirit as every
 * rmpLib port in this repo: a working filter's constants are hard-won. The
 * deviations are only the fwog_ prefix, the decimation-factor binding below,
 * and this header's droop table.
 *
 * This was adapted rather than written fresh because wilibsp's decimation
 * factor is 64 and this BSP's PDM_DECIMATION is 64 -- not a coincidence, both
 * derive from the same 64x PDM oversampling convention. Writing a third
 * implementation, or importing ST's Apache-2.0 OpenPDMFilter.c that this BSP
 * already declined once (pdm_mic.h trap 4), would both be worse.
 *
 * The adaptation is real work rather than a copy because the capture paths
 * differ: wilibsp interleaves four microphones into 32-bit words and pushes
 * bits one at a time from a ring; this BSP DMAs a SINGLE microphone into flat
 * byte buffers. That binding is pdm_mic_decode(), not this file.
 *
 * ---- Scaling: carried over as-is ----
 * Gain is FWOG_CIC_DECIMATE^FWOG_CIC_ORDER = 64^3 = 2^18. The shift is 20,
 * i.e. `>> (20 - 16)` = `>> 4`, landing a full-scale DC input at 2^18 >> 4 =
 * 16384 -- about 6 dB of headroom below int16 clip. That is a judgement call
 * already validated against real microphone levels on FW2; re-deriving it here
 * would be inventing a new one for no reason.
 *
 * ---- Passband droop: the number, corrected ----
 *
 * The P4 spec says the droop is "roughly 3.9 dB at Nyquist for this order and
 * rate". THAT IS THE PER-STAGE FIGURE, NOT THE 3rd-ORDER TOTAL. For R=64,
 * N=3, referred to the 512 kHz capture rate:
 *
 *     output freq | total droop (N=3) | per stage
 *        500 Hz   |    -0.17 dB       |  -0.06 dB
 *       1000 Hz   |    -0.67 dB       |  -0.22 dB
 *       2000 Hz   |    -2.74 dB       |  -0.91 dB
 *       3500 Hz   |    -8.79 dB       |  -2.93 dB
 *       4000 Hz   |   -11.76 dB       |  -3.92 dB
 *
 * from H(f) = [ sin(pi*R*f/fs) / (R * sin(pi*f/fs)) ]^N.
 *
 * 11.8 dB at the top of the band is three times what the spec states, and it
 * is not a rounding difference. tests/test_cic.c pins these ratios against
 * what the filter ACTUALLY does, so the figure that decides the
 * compensating-FIR question is produced by code rather than asserted in prose
 * -- including a bound that a single-stage filter would fail, because 3.9 dB
 * vs 11.8 dB is exactly the confusion the spec's number invites.
 *
 * The spec's DECISION still stands and is followed here: accept the droop for
 * v1, add a compensating FIR only if a measurement demands one. wilibsp ships
 * without compensation and its hardware findings are clean, and tuning a FIR
 * before anyone has measured this board's real acoustic chain would be tuning
 * against arithmetic rather than against a microphone. If it is wanted later
 * it is a short symmetric FIR at the decimated rate -- cheap, and a clean
 * separate increment.
 *
 * ---- DC blocking is NOT done here ----
 * See common/dsp/dcblock.h. It belongs to the caller's analysis path, exactly
 * as wilibsp has it. Putting it inside the CIC would make the decimator lossy
 * for every consumer, including ones that want the DC term.
 */
#ifndef FWOG_CIC_H
#define FWOG_CIC_H
#include <stdint.h>

#define FWOG_CIC_ORDER 3

/* Must equal PDM_DECIMATION (display_cpu/pdm/pdm_mic.h), which is 64.
 *
 * The spec asked for this to BE PDM_DECIMATION, "one definition, not two".
 * It is not written that way, deliberately: this file lives under
 * bsp/common/, which has no business including a display-CPU header -- and
 * pdm_mic.h pulls in hardware/pio.h outside HOST_TEST, so the include would
 * drag PIO into every translation unit that wants a filter, on both CPUs.
 *
 * The spec's real requirement is that the two cannot silently drift, so
 * pdm_mic.h carries a _Static_assert that they agree. That is the same
 * belt-and-braces this repo already uses for the flash offset, where
 * app_meta.h's C constant and the CMake side assert against each other rather
 * than one including the other. A mismatch is a compile error, not a wrong
 * sample rate discovered at a bench. */
#define FWOG_CIC_DECIMATE 64u

/* log2(64^3) = 18 bits of CIC growth; shift by 20 (not 18) to leave ~6 dB of
 * headroom: full-scale DC -> 2^18 >> 4 = 16384, well below int16 clip. */
#define FWOG_CIC_SHIFT 20

typedef struct {
    int32_t  integ[FWOG_CIC_ORDER];   /* integrator accumulators           */
    int32_t  comb[FWOG_CIC_ORDER];    /* comb delay regs (decimated rate)  */
    uint32_t phase;                   /* 0..FWOG_CIC_DECIMATE-1            */
} fwog_cic_t;

void fwog_cic_init(fwog_cic_t *c);

/* Feed one PDM bit (0 or 1, mapped to -1/+1 internally). Returns 1 and writes
 * a PCM sample to *out exactly once per FWOG_CIC_DECIMATE bits; otherwise
 * returns 0 and leaves *out untouched.
 *
 * The state is the CALLER'S so that consecutive capture buffers filter
 * continuously across the boundary. Re-initialising per buffer puts a click at
 * every seam. */
int fwog_cic_push_bit(fwog_cic_t *c, int bit, int16_t *out);

#endif
