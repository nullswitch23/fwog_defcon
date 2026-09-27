#include "lcd/st7789.h"
#include "test_util.h"

static void test_spi_rate(void) {
    /* The hardware record: from clk_peri 200 MHz the rate below the panel's
       62.5 MHz ceiling is 50 MHz, because CPSDVSR is even. */
    ASSERT_EQ(st7789_actual_spi_hz(200000000u, 62500000u), 50000000u);
    /* The legacy firmware's 125 MHz stock clock hits 62.5 exactly --
       which is why the legacy driver asked for that number. */
    ASSERT_EQ(st7789_actual_spi_hz(125000000u, 62500000u), 62500000u);
    /* Never above what was asked for. */
    ASSERT_TRUE(st7789_actual_spi_hz(200000000u, 50000000u) <= 50000000u);
    /* The hardware record's failure mode: if PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK
       is ever lost, clk_peri falls to 48 MHz and 62.5 is unreachable. Report
       0 rather than a silently wrong rate. */
    ASSERT_EQ(st7789_actual_spi_hz(48000000u, 62500000u), 0u);
    ASSERT_EQ(st7789_actual_spi_hz(200000000u, 0u), 0u);
    ASSERT_EQ(st7789_actual_spi_hz(0u, 50000000u), 0u);
}

static void test_rgb565(void) {
    ASSERT_EQ(st7789_rgb565(0, 0, 0), 0x0000u);
    ASSERT_EQ(st7789_rgb565(255, 255, 255), 0xFFFFu);
    ASSERT_EQ(st7789_rgb565(255, 0, 0), 0xF800u);
    ASSERT_EQ(st7789_rgb565(0, 255, 0), 0x07E0u);
    ASSERT_EQ(st7789_rgb565(0, 0, 255), 0x001Fu);
}

static void test_window_encoding(void) {
    /* CASET/RASET parameters go big-endian on the wire. */
    uint8_t buf[4];
    st7789_encode_window(buf, 0u, 319u);
    ASSERT_EQ(buf[0], 0x00u); ASSERT_EQ(buf[1], 0x00u);
    ASSERT_EQ(buf[2], 0x01u); ASSERT_EQ(buf[3], 0x3Fu);
    st7789_encode_window(buf, 0u, 239u);
    ASSERT_EQ(buf[2], 0x00u); ASSERT_EQ(buf[3], 0xEFu);
}

static void test_init_timing(void) {
    /* Datasheet 9.1.2: 5 ms after SWRESET before another command, and
       120 ms after SWRESET before SLPOUT. 9.1.12: 5 ms after SLPOUT.
       Nothing is required after DISPON. */
    ASSERT_EQ(st7789_init_hold_ms(ST7789_INIT_IDLE),   0u);
    ASSERT_EQ(st7789_init_hold_ms(ST7789_INIT_RESET),  5u);
    ASSERT_EQ(st7789_init_hold_ms(ST7789_INIT_CONFIG), 115u);
    ASSERT_EQ(st7789_init_hold_ms(ST7789_INIT_SLPOUT), 5u);
    ASSERT_EQ(st7789_init_hold_ms(ST7789_INIT_DISPON), 0u);

    /* The two constraints the table exists to satisfy. */
    ASSERT_EQ(st7789_init_hold_ms(ST7789_INIT_RESET) +
              st7789_init_hold_ms(ST7789_INIT_CONFIG), 120u);
    unsigned total = 0u;
    for (int s = ST7789_INIT_IDLE; s < ST7789_INIT_DONE; s++)
        total += st7789_init_hold_ms((st7789_init_state_t)s);
    ASSERT_EQ(total, 125u);
}

static void test_init_order(void) {
    st7789_init_state_t s = ST7789_INIT_IDLE;
    ASSERT_EQ(s = st7789_init_next(s), ST7789_INIT_RESET);
    ASSERT_EQ(s = st7789_init_next(s), ST7789_INIT_CONFIG);
    ASSERT_EQ(s = st7789_init_next(s), ST7789_INIT_SLPOUT);
    ASSERT_EQ(s = st7789_init_next(s), ST7789_INIT_DISPON);
    ASSERT_EQ(s = st7789_init_next(s), ST7789_INIT_DONE);
    /* Terminal: DONE never advances. */
    ASSERT_EQ(st7789_init_next(ST7789_INIT_DONE), ST7789_INIT_DONE);
}

static void test_geometry(void) {
    ASSERT_EQ(ST7789_W, 320u);
    ASSERT_EQ(ST7789_H, 240u);
}

static void test_clip_rect(void) {
    uint16_t w, h;
    /* Fully inside: unchanged. */
    w = 80u; h = 60u;
    ASSERT_TRUE(st7789_clip_rect(0u, 0u, &w, &h));
    ASSERT_EQ(w, 80u); ASSERT_EQ(h, 60u);

    /* Full screen: unchanged. */
    w = ST7789_W; h = ST7789_H;
    ASSERT_TRUE(st7789_clip_rect(0u, 0u, &w, &h));
    ASSERT_EQ(w, ST7789_W); ASSERT_EQ(h, ST7789_H);

    /* Overhanging right and bottom: clamped to the panel edge. */
    w = 100u; h = 100u;
    ASSERT_TRUE(st7789_clip_rect(300u, 200u, &w, &h));
    ASSERT_EQ(w, 20u); ASSERT_EQ(h, 40u);

    /* Origin at or past the edge: nothing to draw. */
    w = 10u; h = 10u;
    ASSERT_TRUE(!st7789_clip_rect(ST7789_W, 0u, &w, &h));
    w = 10u; h = 10u;
    ASSERT_TRUE(!st7789_clip_rect(0u, ST7789_H, &w, &h));

    /* Zero extent: nothing to draw. A zero that reached CASET would
       underflow x+w-1 to 0xFFFF and program a garbage window. */
    w = 0u; h = 10u;
    ASSERT_TRUE(!st7789_clip_rect(0u, 0u, &w, &h));
    w = 10u; h = 0u;
    ASSERT_TRUE(!st7789_clip_rect(0u, 0u, &w, &h));
}

static void test_dma_xfer_count(void) {
    /* One 16-bit transfer per pixel: the SPI runs 16-bit frames for the
       fill, so a transfer is a pixel, not a byte. */
    ASSERT_EQ(st7789_dma_xfer_count(1u, 1u), 1u);
    ASSERT_EQ(st7789_dma_xfer_count(80u, 60u), 4800u);
    /* Full screen. 76800 pixels = 153600 bytes = ~24.6 ms at 50 MHz, the
       stall this whole design exists to give back to the link loop. */
    ASSERT_EQ(st7789_dma_xfer_count(ST7789_W, ST7789_H), 76800u);
    /* Wider than 16 bits when multiplied -- must not truncate. */
    ASSERT_TRUE(st7789_dma_xfer_count(ST7789_W, ST7789_H) > 0xFFFFu);
    /* Degenerate rects never reach the hardware, but the count must still
       be zero rather than something that would arm a runaway channel. */
    ASSERT_EQ(st7789_dma_xfer_count(0u, 60u), 0u);
    ASSERT_EQ(st7789_dma_xfer_count(80u, 0u), 0u);
}

static void test_fill_retired(void) {
    /* All three conditions, and only all three, mean the wire is idle and
       CS may be deasserted. */
    ASSERT_TRUE(st7789_fill_retired(false, true, false));

    /* The case that matters and the one a naive implementation gets wrong:
       the channel has issued its last transfer, so it reports idle, while
       up to eight halfwords are still in the FIFO or in the shifter.
       Deasserting CS here truncates the bottom-right corner of every fill
       -- a defect that looks like a panel or addressing fault, not a
       timing one. */
    ASSERT_TRUE(!st7789_fill_retired(false, false, false));
    ASSERT_TRUE(!st7789_fill_retired(false, true, true));
    ASSERT_TRUE(!st7789_fill_retired(false, false, true));

    /* A busy channel is never retired, whatever the SPI says. */
    ASSERT_TRUE(!st7789_fill_retired(true, true, false));
    ASSERT_TRUE(!st7789_fill_retired(true, false, true));
}

static void test_host_fb(void) {
    st7789_host_reset();
    ASSERT_TRUE(!st7789_ready());
    st7789_init_begin();
    for (int i = 0; i < 8 && !st7789_ready(); i++) st7789_init_step();
    ASSERT_TRUE(st7789_ready());
    const uint16_t red = st7789_rgb565(255, 0, 0);
    const uint16_t grn = st7789_rgb565(0, 255, 0);
    st7789_clear(red);
    ASSERT_EQ(st7789_host_fb()[0], red);
    ASSERT_EQ(st7789_host_fb()[ST7789_W * ST7789_H - 1u], red);
    st7789_fill_rect(10u, 20u, 2u, 2u, grn);
    ASSERT_EQ(st7789_host_fb()[20u * ST7789_W + 10u], grn);
    ASSERT_EQ(st7789_host_fb()[21u * ST7789_W + 11u], grn);
    ASSERT_EQ(st7789_host_fb()[20u * ST7789_W + 12u], red);
}

int main(void) {
    test_spi_rate();
    test_rgb565();
    test_window_encoding();
    test_init_timing();
    test_init_order();
    test_clip_rect();
    test_dma_xfer_count();
    test_fill_retired();
    test_geometry();
    test_host_fb();
    TEST_RETURN();
}
