/* WS2812 addressable LED chain on PIN_LED_DATA (GPIO 7) -- the display CPU's
 * seven-pixel status strip. bsp/display_cpu/platform/board.h -- PIN_LED_DATA,
 * FWOG_LED_COUNT.
 *
 * Ported from rmpLib/rpNeoPixel.{h,cpp} (238 lines). This is the first
 * PIO-using driver in this BSP; the block/DMA allocation it was built against
 * is set out in pdm/pdm_mic.h.
 *
 * ---- No DMA ----
 * The original constructor takes a `bUseDMA` parameter but never reads it in
 * the function body -- process() pushes every word with
 * `obPIO.writeTxFIFO()`, i.e. `pio_sm_put_blocking()`. This port carries no
 * DMA at all, matching what the reference actually does rather than what its
 * signature implies. (A DMA-fed TX FIFO would be a reasonable follow-up, but
 * it is not built here.)
 *
 * ---- Fixed: PIO block and state machine are no longer hardcoded ----
 * rpNeoPixel::init(int iPin, int iPIOModule, int iStateMachineIndex, bool)
 * takes iPIOModule and iStateMachineIndex as parameters but never reads
 * either: `:177` calls `pio_sm_set_clkdiv(pio0, 0, div)` and `:183` calls
 * `gpio_set_function(iPin, GPIO_FUNC_PIO0)`, both hardcoding PIO block 0,
 * SM 0. Instantiating on pio1 (or any SM other than 0) would silently
 * misconfigure the wrong hardware. ws2812_init() below honours the PIO
 * instance and state machine the caller actually passes, using
 * pio_gpio_init() (which selects GPIO_FUNC_PIO0/GPIO_FUNC_PIO1 from the PIO
 * instance itself) instead of a hardcoded function select. This matters
 * directly: PDM and I2S will later share this driver's PIO block, so the
 * three drivers on that block need distinct, caller-assigned state machines.
 *
 * ---- The PIO program's side-set polarity looks backwards. It is not: DO
 *      NOT "correct" it. See the hardware record fact 33. ----
 * The four `obPIO.encode_*` calls at rpNeoPixel.cpp:203-212 assemble to
 * (delay in brackets, side value literal):
 *
 *     out  x, 1        side 1 [2]     ; T3-1
 *     jmp  !x, do_zero side 0 [1]     ; T1-1
 *     jmp  bitloop     side 0 [4]     ; T2-1
 *     nop              side 1 [4]     ; T2-1  (do_zero)
 *
 * Every side-set bit here is the exact bitwise complement of the WS2812
 * program the Pico SDK itself ships
 * (`pico-sdk/src/rp2_common/pico_status_led/ws2812.pio`, SDK 2.2.0/2.3.0):
 * `out x,1 side 0`, `jmp !x do_zero side 1`, `jmp bitloop side 1`,
 * `nop side 0`. A first pass at this port took that as a transcription bug
 * and "corrected" the polarity to match the SDK. **That was wrong, and was
 * caught in review before it reached hardware.**
 *
 * `PIN_LED_DATA` (GPIO 7) does not drive the LED chain's DIN pin directly.
 * Per the schematic (`FreeWili-Black-Blue rev5_FINAL.pdf`, sheet 4/12,
 * "LEDs" -- and identically `FreeWili_2_23.pdf`, sheet 6/24, `IC59`), GPIO 7
 * (`LED_SERIAL`) feeds **IC6, a Toshiba TC7SZ04F(T5L,F,T) single-gate CMOS
 * inverter** (VCC = `5V_USB`), whose output drives `LED1.DIN`. Every other
 * signal-translation IC on that sheet (twelve of them) is a non-inverting
 * `SN74LXC1T45`/`2T45` level shifter -- IC6 is the one deliberate exception,
 * chosen to translate 3.3 V logic to the LED chain's 5 V rail. Through IC6,
 * the polarity above produces exactly the WS2812-correct waveform **at the
 * LED**:
 *
 *     | at GPIO 7 (this program) | after IC6, at LED DIN |
 *     |---|---|
 *     | idle HIGH                | idle LOW -- the reset latch works    |
 *     | bit 1: low 7, high 3     | 875 ns high, 375 ns low  (correct T1H) |
 *     | bit 0: low 2, high 8     | 250 ns high, 1000 ns low (correct T0H) |
 *
 * This is also why `// iWord = ~iWord;` sits commented out at
 * `rpNeoPixel.cpp:231` -- someone considered inverting the *data* and
 * rejected it, because the polarity is already handled correctly, once, in
 * the side-set values, not in the packed word.
 *
 * This is live, exercised code, not a corner the original product never
 * reached: `FreeWilliDisplay.cpp:842-843` calls `obLEDs.init()` and
 * `setColor()` at boot, `process()` runs from the main loop
 * (`FreeWilliDisplay.cpp:400,492`), `rpLEDMangerDisplay.cpp` calls
 * `setColor()` at ~20 sites, and `rpProcessHostCommand.cpp:126` exposes
 * per-pixel color over the host protocol (commands 0-6). Reverting this "fix"
 * is not a stylistic call -- it is required for the LEDs to work at all.
 *
 * `ws2812_program_instructions[]` in the driver source is transcribed
 * UNCHANGED from the reference: delay values, jump targets, wrap points, and
 * side-set polarity all exactly as `rpNeoPixel.cpp:203-212` encodes them.
 * `tests/test_ws2812.c` pins the exact four words so this cannot silently
 * regress again.
 *
 * ---- getColor() implemented, not ported as a stub ----
 * rpNeoPixel::getColor() (:77-80) is empty: it takes `int &` output
 * references and never writes any of them, silently leaving the caller's
 * variables at whatever they held before the call. Porting that literally
 * would mean a "getter" that always fails silently. This port keeps its own
 * per-pixel (r, g, b) state (ws2812_set_color() already needs somewhere to
 * put it, since process() reads it back every call) and ws2812_get_color()
 * below reads that state honestly instead of resurrecting the no-op.
 *
 * ---- doPattern() dropped entirely ----
 * rpNeoPixel::doPattern() (:6-66) is almost entirely block-commented out; on
 * real hardware today it is a no-op (`g_iLEDMode` is a local that is
 * always reset to 0 and reads its own commented-out branch). There is no
 * self-driven animation to port -- only external ws2812_set_color() +
 * ws2812_process() calls do anything, exactly as in the shipped firmware.
 * Not carried forward, and no equivalent added.
 *
 * ---- No gamma correction ----
 * process() (:218-236) packs the stored bytes straight into the PIO FIFO
 * word with no scaling of any kind. There is no gamma table or brightness
 * curve anywhere in rpNeoPixel.{h,cpp}. None is added here either -- a gamma
 * curve is a real and common thing to want for perceptually-linear
 * brightness, but it is a product decision this file's own history gives no
 * evidence for, so it is a recommendation for whoever owns the "light show"
 * feature, not something to invent silently in a driver port.
 *
 * ---- Added: a pixel-index bounds check ----
 * rpNeoPixel::setColor() (:68-74) computes `iPixel * COLORS_PER_PIXEL` with
 * no bounds check at all; an out-of-range iPixel is an out-of-bounds write
 * past the end of m_btColors in the original too. In C, with a fixed-size
 * static array backing the same storage, the equivalent call would be
 * undefined behaviour rather than "merely" a latent bug, so ws2812_set_color()
 * below silently ignores an out-of-range pixel index instead of writing past
 * the array. This does not change behaviour for any pixel index the shipped
 * firmware ever passes (0-6); it only changes what happens on an input the
 * original also could not survive.
 *
 * ---- Clock divider, derived not assumed ----
 * ws2812_clkdiv() below is rpNeoPixel.cpp:159-165's divider arithmetic
 * (`clock_get_hz(clk_sys) / (freq * cycles_per_bit)`), transcribed unchanged
 * except for taking the system clock as a parameter instead of calling
 * clock_get_hz() itself, so it is host-testable (AGENTS.md: never hardcode a
 * PIO divider). This board runs clk_sys at 200 MHz, not the SDK's 125 MHz
 * default -- see the test for both.
 */
#ifndef FWOG_WS2812_H
#define FWOG_WS2812_H
#include <stdint.h>
#include <stdbool.h>

/* PIO cycles per bit, transcribed from rpNeoPixel.cpp:159-161 (`ws2812_T1`,
 * `ws2812_T2`, `ws2812_T3`) -- WS2812 timing baked into the PIO program's
 * delay values, not board-specific. T1 is the pulse every bit shares; T2 is
 * the segment that differs between a 1 and a 0 bit; T3 is the segment before
 * the sampled bit is asserted onto the pin at all. See the header's PIO
 * program note for how these turn into instruction delays. */
#define WS2812_T1 2
#define WS2812_T2 5
#define WS2812_T3 3

/* Target WS2812 bit rate, rpNeoPixel.cpp:162 (`float freq = 800000`). */
#define WS2812_FREQ_HZ 800000u

/* ---- Pure: host-tested, no PIO ---- */

/* Pack one pixel's R, G, B into the 24-bit GRB word the WS2812 shift
 * register expects, left-justified into a 32-bit word's top 24 bits (bits
 * 31:8). The PIO program's autopull threshold is 24 (rpNeoPixel.cpp:180,
 * `obPIO.setupFIFOs(..., true, 24, false)` -- see ws2812_init()), shifting
 * MSB-first, so only the top 24 bits are ever shifted out; the bottom 8 are
 * "don't care" padding, exactly like rpNeoPixel::process()'s
 * `iWord = byte0<<24 | byte1<<16 | byte2<<8` (:226-228). GRB, not RGB:
 * rpNeoPixel::setColor() (:68-74) stores green first, red second, blue third
 * -- the WS2812's own wire order, not an arbitrary choice. A port that
 * quietly reorders this swaps red and green on every pixel; see the test for
 * why this function exists specifically to catch that. */
uint32_t ws2812_pack_grb(uint8_t r, uint8_t g, uint8_t b);

/* PIO clock divider for an 800 kHz WS2812 bit rate at cycles_per_bit
 * (WS2812_T1+T2+T3 = 10) cycles per bit, given the live system clock in Hz.
 * Transcribed from rpNeoPixel.cpp:164-165
 * (`clock_get_hz(clk_sys) / (freq * cycles_per_bit)`), expressed against a
 * parameter instead of calling clock_get_hz() directly so it is
 * host-testable. Never hardcode this: this board's clk_sys is 200 MHz, not
 * the Pico SDK's 125 MHz default -- an assumed constant would be wrong by a
 * factor of 1.6 (see the test for both clocks). */
float ws2812_clkdiv(uint32_t sys_clk_hz);

/* The four raw PIO instruction words, transcribed unchanged (including
 * side-set polarity) from rpNeoPixel.cpp:203-212 -- see the header's long
 * note above for why the polarity is correct as shipped, once IC6's
 * inversion downstream of PIN_LED_DATA is accounted for. Exposed here (not
 * behind HOST_TEST -- it is plain data, no SDK dependency) so
 * tests/test_ws2812.c can pin the exact words and catch a future "fix"
 * before it reaches hardware. Defined in ws2812_driver.c. */
extern const uint16_t ws2812_program_instructions[4];

#ifndef HOST_TEST
#include "hardware/pio.h"

/* Load the PIO program and start the state machine on the given PIO block
 * and state machine index -- both caller-chosen, never hardcoded (see the
 * header's PIO-block-and-SM note). Uses PIN_LED_DATA from board.h; the OG has
 * exactly one WS2812 chain, wired to one pin, so unlike the PIO block/SM this
 * is a board fact, not a caller choice.
 *
 * Returns false (without touching `pio`/`sm`'s state further) if the program
 * does not fit in `pio`'s instruction memory -- checked with
 * pio_can_add_program() first rather than letting pio_add_program() assert,
 * because this PIO block is meant to be shared with PDM and I2S (see
 * recon-dma.md) and a future integration mistake putting too much on one
 * block should be a reported failure, not a panic. rpNeoPixel::init() is
 * `void` and has no equivalent failure path -- there is nothing for a bool
 * return to match the "shape" of, the same situation lis3dh_configure()'s
 * header note describes for its own added bool return.
 *
 * A call that fails always leaves the driver not-ready (ws2812_process()
 * becomes a no-op), even if an EARLIER call had already succeeded -- so a
 * second, failed ws2812_init() cannot leave ws2812_process() silently
 * driving whatever pio/sm the first, possibly now-irrelevant call set up. */
bool ws2812_init(PIO pio, uint sm);

/* Set one pixel's color in this driver's own retained state. Out-of-range
 * `pixel` (>= FWOG_LED_COUNT) is silently ignored -- see the header's
 * bounds-check note. Takes effect on the wire at the next ws2812_process()
 * call, exactly like the original's setColor() + process() split. */
void ws2812_set_color(unsigned pixel, uint8_t r, uint8_t g, uint8_t b);

/* Read back one pixel's last-set color from THIS DRIVER'S OWN retained
 * state -- not from the LED. WS2812 is a write-only, one-way protocol (no
 * data line back to the host); there is no way to read a pixel's actual
 * displayed color off the wire, on this board or any other WS2812 chain.
 * Out-of-range `pixel` leaves `*r`/`*g`/`*b` untouched and returns false.
 * See the header's getColor() note for why this is a real implementation,
 * not the original's empty stub. */
bool ws2812_get_color(unsigned pixel, uint8_t *r, uint8_t *g, uint8_t *b);

/* Push every pixel's current color out over the PIO TX FIFO, blocking if it
 * is full. Ported from rpNeoPixel::process() (:218-236) unchanged in
 * structure: one 24-bit GRB word per pixel, FWOG_LED_COUNT pixels, in order.
 * No-op if ws2812_init() has not (yet) succeeded. */
void ws2812_process(void);

/* True once ws2812_init() has succeeded. Exposed because fwog_power_poll()
 * renders the ship-mode countdown on this chain and must not clobber a
 * caller's pixel state on a board where the chain never came up -- the same
 * s_init.ws2812 guard apps/bench/display used to keep privately. */
bool ws2812_ready(void);

#else
/* Host / ogemu: same names. PIO is an opaque pointer so existing host
 * tests do not need hardware/pio.h. */
bool ws2812_init(void *pio, unsigned sm);
void ws2812_set_color(unsigned pixel, uint8_t r, uint8_t g, uint8_t b);
bool ws2812_get_color(unsigned pixel, uint8_t *r, uint8_t *g, uint8_t *b);
void ws2812_process(void);
bool ws2812_ready(void);
#endif
#endif
