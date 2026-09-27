/* ST7789 320x240 LCD on the display CPU's SPI1.
 *
 * Ported from freewiliclassicdisplay/rmpLib/st7789.cpp, the 320x240 branch.
 * Every register value below came out of that file; do not "tidy" them.
 *
 * Deviations from the legacy driver are deliberate. The two that matter: the
 * legacy mySleepa() spin loops are replaced by real deadlines taken from the
 * datasheet, and the backlight is left to board_backlight() so the
 * clock-derived PWM divider survives -- never hardcode that divider, since
 * this BSP runs clk_sys at 200 MHz rather than the stock 125 MHz.
 *
 * There is NO hardware reset pin on this board and NO MISO, so SWRESET is
 * the only way to reach a known state and there is no read-back: the driver
 * cannot tell a working panel from a dead one. */
#ifndef FWOG_ST7789_H
#define FWOG_ST7789_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "platform/board.h"

#define ST7789_W 320u
#define ST7789_H 240u

/* ---- Pure: host-tested, no SDK ---- */

/* The SPI rate a PL022 clocked at clk_hz actually produces when asked for
 * `want_hz`, replicating the SDK's spi_set_baudrate() arithmetic exactly.
 * Returns 0 when the rate is unreachable -- including want_hz > clk_hz,
 * which is what a degraded clk_peri looks like (the hardware record). */
uint32_t st7789_actual_spi_hz(uint32_t clk_hz, uint32_t want_hz);

/* Pack 8-bit components into RGB565. */
uint16_t st7789_rgb565(uint8_t r, uint8_t g, uint8_t b);

/* Encode a CASET/RASET parameter pair. Both are big-endian on the wire;
 * `out` must have room for 4 bytes. */
void st7789_encode_window(uint8_t *out, uint16_t start, uint16_t end);

/* Clip a rect to the panel, in place. Returns false when there is nothing
 * left to draw -- an origin at or past an edge, or a zero extent. A zero
 * that reached CASET/RASET would underflow x+w-1 to 0xFFFF and program a
 * garbage window, so this is a correctness guard, not a convenience. */
bool st7789_clip_rect(uint16_t x, uint16_t y, uint16_t *w, uint16_t *h);

/* DMA transfers a w*h fill needs. The fill runs the SPI in 16-bit frames
 * so one transfer is one pixel: a full screen is 76800, not 153600. */
uint32_t st7789_dma_xfer_count(uint16_t w, uint16_t h);

/* True when a fill has fully retired and CS may be deasserted.
 *
 * All three must hold. An idle DMA channel means the TX FIFO is *full*,
 * not that the wire is idle -- checking only the channel truncates the last
 * pixels of every fill. `fifo_empty` is SSPSR.TFE and `shifter_busy` is
 * SSPSR.BSY. */
bool st7789_fill_retired(bool chan_busy, bool fifo_empty, bool shifter_busy);

/* Initialization is staged so no call blocks the bootloader's link loop:
 * main's per-chunk ACK_WAIT_MS is 200 ms and BL_PHASE_VISIBLE lands at
 * 500 ms, so a blocking init would sit inside a transfer. */
typedef enum {
    ST7789_INIT_IDLE = 0,   /* nothing sent yet          */
    ST7789_INIT_RESET,      /* SWRESET has been sent     */
    ST7789_INIT_CONFIG,     /* register block  sent      */
    ST7789_INIT_SLPOUT,     /* SLPOUT has been sent      */
    ST7789_INIT_DISPON,     /* DISPON has been sent      */
    ST7789_INIT_DONE
} st7789_init_state_t;

/* Milliseconds that must elapse after entering `s` before the next step may
 * run. Datasheet 9.1.2 and 9.1.12: 5 ms after SWRESET before any other
 * command, 120 ms after SWRESET before SLPOUT specifically (so 5 + 115),
 * 5 ms after SLPOUT, and nothing after DISPON. */
uint32_t st7789_init_hold_ms(st7789_init_state_t s);

/* The next state in the sequence. DONE is terminal. */
st7789_init_state_t st7789_init_next(st7789_init_state_t s);

/* Drawing and bring-up. On hardware these talk SPI1; under HOST_TEST they
 * paint a 320x240 RGB565 RAM framebuffer (see st7789_host_fb()). */
void st7789_init_begin(void);

/* Advance the sequence if the current step's hold time has elapsed.
 * Cheap to call every loop iteration. */
void st7789_init_step(void);

/* True once DISPON has been sent. Every draw below is a no-op before this. */
bool st7789_ready(void);

/* Set the active window (CASET/RASET). Does not itself start a pixel write
 * -- RAMWR is issued inside st7789_blit()/st7789_fill_rect(), so a caller
 * must follow this with one of those. No-op before st7789_ready(), and
 * when w or h is 0 (which would otherwise underflow x+w-1 / y+h-1 to
 * 0xFFFF and program a garbage CASET/RASET). */
void st7789_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/* Stream `n` pixels into the current window. */
void st7789_blit(const uint16_t *px, size_t n);

/* Fill a rectangle with one color.
 *
 * ASYNCHRONOUS. Returns when the transfer has been *started*, not when the
 * panel has been painted: a full-screen clear is 153600 bytes, about 25 ms
 * at 50 MHz, and that is 25 ms the bootloader's link loop needs back. CS
 * stays low until the fill retires.
 *
 * Nothing has to be done about that. Every other entry point in this driver
 * begins with st7789_dma_wait(), so a caller that simply keeps drawing
 * blocks exactly as it did when this was synchronous. Only a caller that
 * wants the time back has to do anything, and what it does is call
 * st7789_busy() before drawing again.
 *
 * Falls back to a synchronous chunk loop when no DMA channel could be
 * claimed. Out-of-range and zero-size rects are clipped away by
 * st7789_clip_rect() and draw nothing. */
void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint16_t color);
void st7789_clear(uint16_t color);

/* True while a fill is still on the wire. False when no fill is in flight,
 * and false on the synchronous fallback path, where fills are already
 * complete by the time they return. */
bool st7789_busy(void);

/* Block until any in-flight fill has fully retired, restore 8-bit SPI
 * frames and deassert CS. Bounded: on expiry the transfer is aborted and
 * the wire is restored anyway, leaving the panel wrong rather than the CPU
 * spinning (see st7789.c).
 *
 * The gating that matters is in three places, and between them they cover
 * every touch of CS and of the SPI data register: the internal command
 * writer (so CASET/RASET and every other register write wait),
 * st7789_blit(), and st7789_fill_rect(). st7789_set_window() calls this too,
 * but that call is redundant -- it touches neither CS nor the SPI itself,
 * and the command writer it delegates to has already waited. Public so a
 * caller that wants the old synchronous behavior can just ask for it. */
void st7789_dma_wait(void);

#ifdef HOST_TEST
/* Row-major RGB565 panel contents. ST7789_W * ST7789_H pixels. Never NULL. */
const uint16_t *st7789_host_fb(void);
/* Idle + black panel. Tests that draw more than one picture call this. */
void st7789_host_reset(void);
#endif

#endif
