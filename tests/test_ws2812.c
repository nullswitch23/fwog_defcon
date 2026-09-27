#include "test_util.h"
#include "leds/ws2812_driver.h"

/* Tolerance for a float comparison; ASSERT_EQ casts through unsigned long
 * long, which is wrong for a divider that legitimately has a fractional
 * part, so these compare by hand. */
static int approx(float a, float b) {
    float d = a - b;
    if (d < 0) d = -d;
    return d < 0.001f;
}

int main(void) {
    /* ---- ws2812_pack_grb(): rpNeoPixel::process()'s
     * `iWord = byte0<<24 | byte1<<16 | byte2<<8`, where byte0/1/2 are the
     * stored G, R, B bytes (rpNeoPixel::setColor() stores green first, red
     * second, blue third). The WS2812 wire order is GRB, not RGB -- this is
     * the single easiest place a port swaps two channels, so every case
     * below uses a distinct value per channel to make a swap visible. ---- */

    /* pure red: must land in bits 23:16 (the "middle" byte of the word), not
     * bits 31:24 (which must hold green). */
    ASSERT_EQ(ws2812_pack_grb(0xFFu, 0x00u, 0x00u), 0x00FF0000u);

    /* pure green: must land in bits 31:24. */
    ASSERT_EQ(ws2812_pack_grb(0x00u, 0xFFu, 0x00u), 0xFF000000u);

    /* pure blue: must land in bits 15:8, and bits 7:0 stay zero (the
     * autopull threshold is 24, so the bottom byte is never shifted out). */
    ASSERT_EQ(ws2812_pack_grb(0x00u, 0x00u, 0xFFu), 0x0000FF00u);

    /* three distinct, non-symmetric values -- catches a swap that a
     * pure-channel test (0xFF vs 0x00) could miss if two channels were
     * swapped with each other but both happened to be the same value. */
    ASSERT_EQ(ws2812_pack_grb(0x12u, 0x34u, 0x56u), 0x34125600u);

    /* all zero and all-ones sanity, so a "shift by the wrong amount"
     * regression that only breaks for asymmetric input still has a floor. */
    ASSERT_EQ(ws2812_pack_grb(0x00u, 0x00u, 0x00u), 0x00000000u);
    ASSERT_EQ(ws2812_pack_grb(0xFFu, 0xFFu, 0xFFu), 0xFFFFFF00u);

    /* ---- ws2812_clkdiv(): rpNeoPixel.cpp:159-165's divider arithmetic,
     * `clock_get_hz(clk_sys) / (freq * cycles_per_bit)`, freq=800000,
     * cycles_per_bit = T1+T2+T3 = 2+5+3 = 10. Tested at this board's actual
     * clk_sys (200 MHz) and at the Pico SDK's default (125 MHz) side by
     * side, so the test itself documents why the divider may never be a
     * hardcoded constant: getting this board's clock wrong by using the SDK
     * default would be wrong by a factor of 1.6. ---- */

    /* 200 MHz (this board, FWOG_SYS_CLK_KHZ): 200,000,000 / 8,000,000 = 25.0 */
    ASSERT_TRUE(approx(ws2812_clkdiv(200000000u), 25.0f));

    /* 125 MHz (Pico SDK default): 125,000,000 / 8,000,000 = 15.625 */
    ASSERT_TRUE(approx(ws2812_clkdiv(125000000u), 15.625f));

    /* the ratio between the two must be exactly 200/125 = 1.6 -- pins down
     * that the function is linear in its argument (no hidden rounding to an
     * integer divider inside ws2812_clkdiv() itself; the PIO hardware's own
     * clkdiv register does that rounding, not this function). */
    ASSERT_TRUE(approx(ws2812_clkdiv(200000000u) / ws2812_clkdiv(125000000u), 1.6f));

    /* ---- ws2812_program_instructions[]: pins the four raw PIO words
     * exactly, side-set polarity included. This looks backwards against any
     * stock WS2812 PIO example -- it is not a bug. The hardware record
     * fact 33 records IC6 (a TC7SZ04F inverter between PIN_LED_DATA and the
     * LED chain's DIN) inverting this signal in hardware, so THIS polarity
     * is the one that produces a correct WS2812 waveform at the LED. A
     * review round already caught one attempt to "fix" this to match the
     * SDK's ws2812.pio; this test exists so that regresses loudly instead of
     * silently if it is ever tried again. ---- */
    ASSERT_EQ(ws2812_program_instructions[0], 0x7221u);
    ASSERT_EQ(ws2812_program_instructions[1], 0x0123u);
    ASSERT_EQ(ws2812_program_instructions[2], 0x0400u);
    ASSERT_EQ(ws2812_program_instructions[3], 0xb442u);

    TEST_RETURN();
}
