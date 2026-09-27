#include "lcd/st7789.h"

uint32_t st7789_actual_spi_hz(uint32_t clk_hz, uint32_t want_hz) {
    /* Replicates SDK spi_set_baudrate(): CPSDVSR is an even 2..254 and SCR
       is 0..255, so the achievable set is coarse and the SDK rounds DOWN.
       Asking above clk_hz is invalid rather than clamped. */
    if (clk_hz == 0u || want_hz == 0u || want_hz > clk_hz) return 0u;

    uint32_t prescale = 256u;
    for (uint32_t p = 2u; p <= 254u; p += 2u) {
        if ((uint64_t)clk_hz < (uint64_t)(p + 2u) * 256u * want_hz) {
            prescale = p;
            break;
        }
    }
    if (prescale > 254u) return 0u;

    uint32_t postdiv = 1u;
    for (uint32_t pd = 256u; pd > 1u; pd--) {
        if (clk_hz / (prescale * (pd - 1u)) > want_hz) {
            postdiv = pd;
            break;
        }
    }
    return clk_hz / (prescale * postdiv);
}

uint16_t st7789_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((uint16_t)(r & 0xF8u) << 8) |
                      ((uint16_t)(g & 0xFCu) << 3) |
                      ((uint16_t)(b) >> 3));
}

void st7789_encode_window(uint8_t *out, uint16_t start, uint16_t end) {
    out[0] = (uint8_t)(start >> 8); out[1] = (uint8_t)(start & 0xFFu);
    out[2] = (uint8_t)(end   >> 8); out[3] = (uint8_t)(end   & 0xFFu);
}

bool st7789_clip_rect(uint16_t x, uint16_t y, uint16_t *w, uint16_t *h) {
    if (!w || !h || *w == 0u || *h == 0u) return false;
    if (x >= ST7789_W || y >= ST7789_H) return false;
    if ((uint32_t)x + *w > ST7789_W) *w = (uint16_t)(ST7789_W - x);
    if ((uint32_t)y + *h > ST7789_H) *h = (uint16_t)(ST7789_H - y);
    return true;
}

uint32_t st7789_dma_xfer_count(uint16_t w, uint16_t h) {
    return (uint32_t)w * (uint32_t)h;
}

bool st7789_fill_retired(bool chan_busy, bool fifo_empty, bool shifter_busy) {
    return !chan_busy && fifo_empty && !shifter_busy;
}

uint32_t st7789_init_hold_ms(st7789_init_state_t s) {
    switch (s) {
    case ST7789_INIT_IDLE:   return 0u;    /* send SWRESET at once      */
    case ST7789_INIT_RESET:  return 5u;    /* 9.1.2: 5 ms before others */
    case ST7789_INIT_CONFIG: return 115u;  /* 5 + 115 = 120 before SLPOUT */
    case ST7789_INIT_SLPOUT: return 5u;    /* 9.1.12: 5 ms, NOT 120     */
    case ST7789_INIT_DISPON: return 0u;    /* nothing required after    */
    default:                 return 0u;
    }
}

st7789_init_state_t st7789_init_next(st7789_init_state_t s) {
    return (s >= ST7789_INIT_DONE) ? ST7789_INIT_DONE
                                   : (st7789_init_state_t)(s + 1);
}

#ifndef HOST_TEST
#include "common/diag.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/time.h"

/* Register names, transcribed from the legacy driver. */
#define REG_SWRESET  0x01u
#define REG_SLPOUT   0x11u
#define REG_INVON    0x21u
#define REG_DISPON   0x29u
#define REG_CASET    0x2Au
#define REG_RASET    0x2Bu
#define REG_RAMWR    0x2Cu
#define REG_MADCTL   0x36u
#define REG_COLMOD   0x3Au
#define REG_PORCTRL  0xB2u
#define REG_GCTRL    0xB7u
#define REG_VCOMS    0xBBu
#define REG_LCMCTRL  0xC0u
#define REG_VDVVRHEN 0xC2u
#define REG_VRHS     0xC3u
#define REG_VDVS     0xC4u
#define REG_FRCTRL2  0xC6u
#define REG_PWCTRL1  0xD0u
#define REG_GMCTRP1  0xE0u
#define REG_GMCTRN1  0xE1u

/* 320x240 branch: MADCTL 0x70, 16 bits per pixel. */
#define MADCTL_320x240 0x70u
#define COLMOD_16BPP   0x05u

static st7789_init_state_t s_state = ST7789_INIT_IDLE;
static absolute_time_t     s_due;

/* The fill path's DMA state.
 *
 * s_dma_ch is claimed once at init and never released: claiming per
 * transfer would start failing unpredictably as WS2812, PDM, I2S and IR
 * arrive and want channels of their own. -1 means the claim failed, which
 * is not fatal -- the fallback below is the old chunk loop, and a
 * bootloader that draws slowly is strictly better than one that panics.
 *
 * s_fill_px is the DMA's entire source: one halfword, read without
 * incrementing, w*h times. It is static because the transfer outlives the
 * call that started it. */
static int      s_dma_ch = -1;
static uint16_t s_fill_px;
static bool     s_fill_active;

static void ramwr_end(void);

static void cmd(uint8_t c, const uint8_t *data, size_t len) {
    /* Implicit, and the reason asynchrony cannot be got wrong here: this
       runs before CS is touched, so a command issued during a fill waits
       for the wire rather than interleaving with it. */
    st7789_dma_wait();
    gpio_put(PIN_LCD_CS, 0);
    gpio_put(PIN_LCD_DC, 0);              /* command */
    spi_write_blocking(FWOG_LCD_SPI, &c, 1);
    if (data && len) {
        gpio_put(PIN_LCD_DC, 1);          /* data */
        spi_write_blocking(FWOG_LCD_SPI, data, len);
    }
    gpio_put(PIN_LCD_CS, 1);
}

/* Every register write that is legal in the 5 ms window after SWRESET.
 * Values are byte-for-byte from the legacy 320x240 branch. */
static void send_config(void) {
    static const uint8_t porctrl[] = {0x0c, 0x0c, 0x00, 0x33, 0x33};
    static const uint8_t gamma_p[] = {0xD0, 0x08, 0x11, 0x08, 0x0C, 0x15, 0x39,
                                      0x33, 0x50, 0x36, 0x13, 0x14, 0x29, 0x2D};
    static const uint8_t gamma_n[] = {0xD0, 0x08, 0x10, 0x08, 0x06, 0x06, 0x39,
                                      0x44, 0x51, 0x0B, 0x16, 0x14, 0x2F, 0x31};
    const uint8_t one = 0x01u, colmod = COLMOD_16BPP, madctl = MADCTL_320x240;
    const uint8_t gctrl = 0x35u, vcoms = 0x1fu, lcmctrl = 0x2cu;
    const uint8_t vrhs = 0x12u, vdvs = 0x20u, frctrl2 = 0x0fu;
    static const uint8_t pwctrl1[] = {0xa4, 0xa1};
    uint8_t win[4];

    cmd(REG_COLMOD,   &colmod, 1);
    cmd(REG_PORCTRL,  porctrl, sizeof porctrl);
    cmd(REG_GCTRL,    &gctrl, 1);
    cmd(REG_VCOMS,    &vcoms, 1);
    cmd(REG_LCMCTRL,  &lcmctrl, 1);
    cmd(REG_VDVVRHEN, &one, 1);
    cmd(REG_VRHS,     &vrhs, 1);
    cmd(REG_VDVS,     &vdvs, 1);
    cmd(REG_FRCTRL2,  &frctrl2, 1);
    cmd(REG_PWCTRL1,  pwctrl1, sizeof pwctrl1);
    cmd(REG_GMCTRP1,  gamma_p, sizeof gamma_p);
    cmd(REG_GMCTRN1,  gamma_n, sizeof gamma_n);
    cmd(REG_INVON,    NULL, 0);

    st7789_encode_window(win, 0u, ST7789_W - 1u); cmd(REG_CASET, win, 4);
    st7789_encode_window(win, 0u, ST7789_H - 1u); cmd(REG_RASET, win, 4);
    cmd(REG_MADCTL, &madctl, 1);
}

void st7789_init_begin(void) {
    if (s_state != ST7789_INIT_IDLE) return;

    /* Rate is derived, never hardcoded: clk_peri tracks clk_sys only
       because the board header sets PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK
       (the hardware record). At 200 MHz this lands on 50 MHz (the hardware record). */
    const uint32_t clk  = (uint32_t)clock_get_hz(clk_peri);
    const uint32_t want = st7789_actual_spi_hz(clk, FWOG_LCD_MAX_HZ);
    if (want == 0u) {
        DIAG("[lcd] clk_peri %u Hz cannot reach the panel rate\n",
             (unsigned)clk);
        return;
    }
    const uint32_t got = spi_init(FWOG_LCD_SPI, want);
    DIAG("[lcd] spi %u Hz (clk_peri %u Hz)\n", (unsigned)got, (unsigned)clk);

    gpio_set_function(PIN_LCD_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_LCD_MOSI, GPIO_FUNC_SPI);
    /* CS and DC are already parked as outputs by board_init_pins(). */

    /* Non-panicking: dma_claim_unused_channel(true) panics on exhaustion,
       and a panic in the one binary this board cannot re-flash without
       physical BOOTSEL access it does not have is the wrong failure. */
    s_dma_ch = dma_claim_unused_channel(false);
    if (s_dma_ch < 0) {
        DIAG("[lcd] no free DMA channel; fills stay synchronous\n");
    } else {
        DIAG("[lcd] dma ch %d\n", s_dma_ch);
    }

    cmd(REG_SWRESET, NULL, 0);
    s_state = ST7789_INIT_RESET;
    s_due   = make_timeout_time_ms(st7789_init_hold_ms(s_state));
}

void st7789_init_step(void) {
    if (s_state == ST7789_INIT_IDLE || s_state == ST7789_INIT_DONE) return;
    if (!time_reached(s_due)) return;

    switch (s_state) {
    case ST7789_INIT_RESET:  send_config();               break;
    case ST7789_INIT_CONFIG: cmd(REG_SLPOUT, NULL, 0);    break;
    case ST7789_INIT_SLPOUT: cmd(REG_DISPON, NULL, 0);    break;
    default: break;
    }
    s_state = st7789_init_next(s_state);
    s_due   = make_timeout_time_ms(st7789_init_hold_ms(s_state));
    if (s_state == ST7789_INIT_DONE) DIAG("[lcd] ready\n");
}

bool st7789_ready(void) { return s_state == ST7789_INIT_DONE; }

bool st7789_busy(void) {
    /* s_fill_active implies s_dma_ch >= 0 -- only the DMA path sets the
       flag -- but say so rather than rely on it: an edit that set the flag
       on the fallback path would turn dma_channel_is_busy((uint)-1) into a
       wild read into peripheral space, silent in a release build with
       parameter assertions compiled out. */
    if (!s_fill_active || s_dma_ch < 0) return false;
    /* Sampled in completion order: channel idle, then FIFO empty, then BSY
       clear. Completion is monotone in that order, so each earlier
       observation is still true when the later one is taken. Reading sr
       first inverts it -- the channel could go idle after the read, and the
       function would report retired for a transfer whose last halfword
       entered the FIFO afterwards, deasserting CS early and truncating the
       bottom-right corner of the fill. */
    const bool     chan_busy = dma_channel_is_busy((uint)s_dma_ch);
    const uint32_t sr        = spi_get_hw(FWOG_LCD_SPI)->sr;
    return !st7789_fill_retired(chan_busy,
                                (sr & SPI_SSPSR_TFE_BITS) != 0u,
                                (sr & SPI_SSPSR_BSY_BITS) != 0u);
}

void st7789_dma_wait(void) {
    if (!s_fill_active) return;

    /* Bounded, never unbounded, and the bound must not be removed. No
       software path reaches a permanent hang today; the consequence decides
       this. The display CPU has no watchdog by design (the hardware record) and
       bl_display cannot be re-flashed without BOOTSEL access this CPU does
       not have, so a hardware-induced stall here repeats deterministically
       on every reset -- including the one main drives with GUI_NRESET --
       and the board never boots again.
     *
     * 200 ms against a longest legitimate fill of a full screen:
     * 320 * 240 * 2 = 153600 bytes at 50 MHz = 24.6 ms of wire time. That
     * is roughly 8x margin, far enough above the legitimate case that
     * expiry means something is genuinely wrong rather than merely slow.
     *
     * On expiry, recover rather than latch: abort the channel and fall into
     * the same restore tail a normal completion takes. The panel is then
     * wrong, but the bootloader keeps running -- the right trade in this
     * binary. Note that dma_channel_abort() itself spins, so the bound is
     * not airtight; it is the SDK's own recovery primitive and is what
     * bl_jump.c already relies on, so using it here is consistent. */
    const absolute_time_t deadline = make_timeout_time_ms(200);
    while (st7789_busy()) {
        if (time_reached(deadline)) {
            /* Reached only when st7789_busy() was true, which requires
               s_dma_ch >= 0. */
            dma_channel_abort((uint)s_dma_ch);
            DIAG("[lcd] fill did not retire in 200 ms; aborted\n");
            break;
        }
        tight_loop_contents();
    }

    /* Back to 8-bit frames before any command or blit can run. */
    spi_set_format(FWOG_LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    /* The RX FIFO filled with halfwords nobody read (there is no MISO on
       this board, so they are meaningless) and the PL022 set a sticky
       overrun flag. Harmless -- RORIM is not enabled and the part keeps
       shifting -- but drain it so the next 8-bit transfer starts clean. */
    while (spi_is_readable(FWOG_LCD_SPI)) (void)spi_get_hw(FWOG_LCD_SPI)->dr;

    ramwr_end();                 /* CS high, only now that the wire is idle */
    s_fill_active = false;
}

void st7789_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!st7789_ready() || w == 0u || h == 0u) return;
    st7789_dma_wait();
    uint8_t win[4];
    st7789_encode_window(win, x, (uint16_t)(x + w - 1u));
    cmd(REG_CASET, win, 4);
    st7789_encode_window(win, y, (uint16_t)(y + h - 1u));
    cmd(REG_RASET, win, 4);
}

/* RAMWR then a raw data stream, so a caller can push more than one buffer
 * into the same window without re-issuing the command. */
static void ramwr_begin(void) {
    gpio_put(PIN_LCD_CS, 0);
    gpio_put(PIN_LCD_DC, 0);
    const uint8_t c = REG_RAMWR;
    spi_write_blocking(FWOG_LCD_SPI, &c, 1);
    gpio_put(PIN_LCD_DC, 1);
}
static void ramwr_end(void) { gpio_put(PIN_LCD_CS, 1); }

void st7789_blit(const uint16_t *px, size_t n) {
    if (!st7789_ready() || !px || !n) return;
    /* Blits stay synchronous on purpose. The glyph buffer in lcd_text.c is
       static -- it was moved off the stack to fix a 74%-full stack -- and
       its safety rests on this function draining it before returning. An
       async blit would let the next glyph overwrite a buffer DMA is still
       reading. A scale-3 glyph is ~138 us and has never blocked anything. */
    st7789_dma_wait();
    /* Pixels go big-endian. Convert a chunk at a time rather than calling
       spi_write_blocking() per pixel -- one scale-3 glyph is 432 pixels,
       and per-pixel calls would pay that overhead 432 times. */
    enum { CHUNK_PX = 64u };
    uint8_t buf[CHUNK_PX * 2u];
    ramwr_begin();
    size_t i = 0u;
    while (i < n) {
        const size_t m = ((n - i) > CHUNK_PX) ? (size_t)CHUNK_PX : (n - i);
        for (size_t k = 0u; k < m; k++) {
            buf[k * 2u]      = (uint8_t)(px[i + k] >> 8);
            buf[k * 2u + 1u] = (uint8_t)(px[i + k] & 0xFFu);
        }
        spi_write_blocking(FWOG_LCD_SPI, buf, m * 2u);
        i += m;
    }
    ramwr_end();
}

/* The pre-DMA path, kept as the fallback for a failed channel claim. */
static void fill_blocking(uint16_t w, uint16_t h, uint16_t color) {
    enum { CHUNK_PX = 64u };
    uint8_t chunk[CHUNK_PX * 2u];
    for (unsigned i = 0; i < CHUNK_PX; i++) {
        chunk[i * 2u]      = (uint8_t)(color >> 8);
        chunk[i * 2u + 1u] = (uint8_t)(color & 0xFFu);
    }
    ramwr_begin();
    uint32_t left = st7789_dma_xfer_count(w, h);
    while (left) {
        const uint32_t n = (left > CHUNK_PX) ? CHUNK_PX : left;
        spi_write_blocking(FWOG_LCD_SPI, chunk, n * 2u);
        left -= n;
    }
    ramwr_end();
}

void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint16_t color) {
    if (!st7789_ready()) return;
    st7789_dma_wait();
    if (!st7789_clip_rect(x, y, &w, &h)) return;

    /* One window for the whole rect. The legacy clearScreen() issued 2400
       windowed commands to paint the same pixels. */
    st7789_set_window(x, y, w, h);

    if (s_dma_ch < 0) { fill_blocking(w, h, color); return; }

    s_fill_px = color;
    ramwr_begin();
    /* 16-bit frames for the duration of the fill: one halfword is one
       pixel, and the PL022 shifts MSB first, so the halfword lands on the
       wire big-endian with no byte-swapping buffer anywhere. Restored to 8
       bits in st7789_dma_wait(). spi_set_format() disables and re-enables
       the SSP around the change, and the RAMWR write above already waited
       for BSY to clear, so this cannot truncate a byte in flight. */
    spi_set_format(FWOG_LCD_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    dma_channel_config c = dma_channel_get_default_config((uint)s_dma_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, spi_get_dreq(FWOG_LCD_SPI, true));
    /* Nothing here services a DMA interrupt, and DMA_INTR is write-to-clear
       with nothing clearing it: left noisy, every fill would set this
       channel's sticky bit and hand it across the app handoff, where a
       different binary would see a completion for a transfer that retired
       in the bootloader. bl_jump.c clears the register too; this keeps the
       flag from being set in the first place. */
    channel_config_set_irq_quiet(&c, true);

    /* Set before the channel starts: an interrupt between the two would
       otherwise see a running transfer that st7789_busy() reports idle. */
    s_fill_active = true;
    dma_channel_configure((uint)s_dma_ch, &c,
                          &spi_get_hw(FWOG_LCD_SPI)->dr,   /* write, fixed */
                          &s_fill_px,                      /* read,  fixed */
                          st7789_dma_xfer_count(w, h), true);
}

void st7789_clear(uint16_t color) {
    st7789_fill_rect(0u, 0u, ST7789_W, ST7789_H, color);
}
#else
#include <string.h>

static uint16_t s_fb[ST7789_W * ST7789_H];
static bool s_ready;
static st7789_init_state_t s_state = ST7789_INIT_IDLE;
static uint16_t s_wx, s_wy, s_ww, s_wh;
static uint32_t s_blit_i;

const uint16_t *st7789_host_fb(void) { return s_fb; }

void st7789_host_reset(void) {
    memset(s_fb, 0, sizeof s_fb);
    s_ready = false;
    s_state = ST7789_INIT_IDLE;
    s_wx = 0u;
    s_wy = 0u;
    s_ww = ST7789_W;
    s_wh = ST7789_H;
    s_blit_i = 0u;
}

void st7789_init_begin(void) {
    if (s_state != ST7789_INIT_IDLE) return;
    s_state = ST7789_INIT_RESET;
}

void st7789_init_step(void) {
    if (s_state == ST7789_INIT_IDLE || s_state == ST7789_INIT_DONE) return;
    /* Datasheet holds are for the wire. On a RAM panel they are nothing. */
    s_state = st7789_init_next(s_state);
    if (s_state == ST7789_INIT_DONE) s_ready = true;
}

bool st7789_ready(void) { return s_ready; }
bool st7789_busy(void) { return false; }
void st7789_dma_wait(void) {}

void st7789_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!s_ready || w == 0u || h == 0u) return;
    if (x >= ST7789_W || y >= ST7789_H) {
        s_ww = 0u;
        s_wh = 0u;
        return;
    }
    if ((uint32_t)x + w > ST7789_W) w = (uint16_t)(ST7789_W - x);
    if ((uint32_t)y + h > ST7789_H) h = (uint16_t)(ST7789_H - y);
    s_wx = x;
    s_wy = y;
    s_ww = w;
    s_wh = h;
    s_blit_i = 0u;
}

void st7789_blit(const uint16_t *px, size_t n) {
    if (!s_ready || !px || !n || s_ww == 0u || s_wh == 0u) return;
    const uint32_t span = (uint32_t)s_ww * (uint32_t)s_wh;
    for (size_t k = 0; k < n; k++) {
        const uint32_t i = s_blit_i++;
        if (i >= span) break;
        const uint16_t col = (uint16_t)(i % s_ww);
        const uint16_t row = (uint16_t)(i / s_ww);
        s_fb[(uint32_t)(s_wy + row) * ST7789_W + (uint32_t)(s_wx + col)] = px[k];
    }
}

void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint16_t color) {
    if (!s_ready) return;
    if (!st7789_clip_rect(x, y, &w, &h)) return;
    for (uint16_t row = 0u; row < h; row++) {
        uint16_t *dst = &s_fb[(uint32_t)(y + row) * ST7789_W + x];
        for (uint16_t col = 0u; col < w; col++) dst[col] = color;
    }
    st7789_set_window(x, y, w, h);
}

void st7789_clear(uint16_t color) {
    st7789_fill_rect(0u, 0u, ST7789_W, ST7789_H, color);
}
#endif
