#include "test_util.h"
#include "ir/ir_comm.h"
#include <string.h>

/* Tolerance for a float/double comparison -- same helper as
 * test_ws2812.c/test_pdm.c/test_i2s.c. */
static int approx(double a, double b) {
    double d = a - b;
    if (d < 0) d = -d;
    return d < 0.01;
}

int main(void) {
    /* ---- IR_NEC_FRAME_ENTRIES: 2 header + 32*2 bit-times + 1 trailing mark
     * = 67, transcribed from BuildIRDataFromCode()'s own
     * `iIRBitTimeCount = 67;` (rpIRComm.cpp:236). This is the number THE
     * BUG got wrong (IR_MEASUREMENTMENT_MAX was 1) -- pinning it here is
     * pinning the fix. ---- */
    ASSERT_EQ(IR_NEC_FRAME_ENTRIES, 67u);

    /* ---- ir_nec_encode(): capacity/NULL guards -- the replacement for the
     * original's one-element class member (see the header's THE BUG note).
     * ---- */
    {
        int small[66];
        ASSERT_EQ(ir_nec_encode(0x12345678u, small, 66u), 0u);
        ASSERT_EQ(ir_nec_encode(0x12345678u, NULL, 67u), 0u);
    }

    /* ---- ir_nec_encode(): exact bit-time sequence for a known code,
     * transcribed from BuildIRDataFromCode() (rpIRComm.cpp:214-238).
     * code = 0x00000001 (bit 0 set, all others clear) -- LSB-first, so the
     * FIRST bit pair must be the logic-1 pattern (563,1688) and every
     * other bit pair the logic-0 pattern (563,563). ---- */
    {
        int bt[IR_NEC_FRAME_ENTRIES];
        size_t n = ir_nec_encode(0x00000001u, bt, IR_NEC_FRAME_ENTRIES);
        ASSERT_EQ(n, 67u);
        ASSERT_EQ((unsigned)bt[0], 9000u);   /* leading mark */
        ASSERT_EQ((unsigned)bt[1], 4500u);   /* leading space */
        /* bit 0 (LSB, sent first): logic 1 */
        ASSERT_EQ((unsigned)bt[2], 563u);
        ASSERT_EQ((unsigned)bt[3], 1688u);
        /* bit 1: logic 0 */
        ASSERT_EQ((unsigned)bt[4], 563u);
        ASSERT_EQ((unsigned)bt[5], 563u);
        /* bit 31 (last data bit) occupies bt[64]/bt[65]; also logic 0 here. */
        ASSERT_EQ((unsigned)bt[64], 563u);
        ASSERT_EQ((unsigned)bt[65], 563u);
        /* trailing mark */
        ASSERT_EQ((unsigned)bt[66], 563u);
    }

    /* ---- Round trip: encode then decode must recover the original code
     * and all 32 bits, for several codes including the all-zero and
     * all-one extremes and a real code from rpIRComm.h's own Roku table
     * (IR_ROKU_OK = 0xD52AC7EA). ---- */
    {
        const uint32_t codes[] = { 0x00000000u, 0xFFFFFFFFu, 0xD52AC7EAu,
                                   0xA5A5A5A5u, 0x00000001u, 0x80000000u };
        for (unsigned c = 0; c < sizeof(codes) / sizeof(codes[0]); c++) {
            int bt[IR_NEC_FRAME_ENTRIES];
            size_t n = ir_nec_encode(codes[c], bt, IR_NEC_FRAME_ENTRIES);
            ASSERT_EQ(n, 67u);

            uint32_t out_code = 0xDEADBEEFu;
            int out_bits = -1;
            bool ok = ir_nec_decode(bt, IR_NEC_FRAME_ENTRIES, &out_code, &out_bits);
            ASSERT_TRUE(ok == true);
            ASSERT_EQ(out_bits, 32);
            ASSERT_EQ(out_code, codes[c]);
        }
    }

    /* ---- Bit-length discrimination boundary: ProcessIRData() (rpIRComm.cpp
     * :195) checks `iIRBitTimes[iCount+1] > 1000 && < 2000` for logic 1 --
     * BOTH bounds EXCLUSIVE. Exactly 1000 or exactly 2000 must decode as 0,
     * not 1; 1001 and 1999 (just inside) must decode as 1. Pins the boundary
     * against an off-by-one (>=  / <=) mutation. ---- */
    {
        int bt[16] = { 9000, 4500,
                       563, 1000,  /* bit0: space == 1000 (exclusive lower bound) -> 0 */
                       563, 2000,  /* bit1: space == 2000 (exclusive upper bound) -> 0 */
                       563, 1001,  /* bit2: space == 1001 (just inside)          -> 1 */
                       563, 1999,  /* bit3: space == 1999 (just inside)          -> 1 */
                       563, 1500,  /* bit4: comfortably inside                   -> 1 */
                       563, 999,   /* bit5: just below the window                -> 0 */
                       563, 2001 };/* bit6: just above the window                -> 0 */
        uint32_t out_code = 0;
        int out_bits = 0;
        /* count=16: exactly 7 bit-pairs (indices 2..15) -- the 18-slot
         * array above holds one entry more than that, which was the
         * original version of this test's own bug: passing count past the
         * 7th pair's space entry pulls in a padding element as an 8th mark
         * and desyncs the expected bit count. */
        bool ok = ir_nec_decode(bt, 16u, &out_code, &out_bits);
        ASSERT_TRUE(ok == true);
        ASSERT_EQ(out_bits, 7);
        /* LSB-first: bit0=0, bit1=0, bit2=1, bit3=1, bit4=1, bit5=0, bit6=0 */
        ASSERT_EQ(out_code, (uint32_t)((1u << 2) | (1u << 3) | (1u << 4)));
    }

    /* ---- Malformed input ---- */

    /* count < 16: rejected immediately, output zeroed. */
    {
        int bt[15];
        for (int i = 0; i < 15; i++) bt[i] = 9000;
        uint32_t out_code = 0xDEADBEEFu;
        int out_bits = -1;
        bool ok = ir_nec_decode(bt, 15u, &out_code, &out_bits);
        ASSERT_TRUE(ok == false);
        ASSERT_EQ(out_code, 0u);
        ASSERT_EQ(out_bits, 0);
    }

    /* NULL bit_times: rejected, output zeroed (no crash). */
    {
        uint32_t out_code = 0xDEADBEEFu;
        int out_bits = -1;
        bool ok = ir_nec_decode(NULL, 67u, &out_code, &out_bits);
        ASSERT_TRUE(ok == false);
        ASSERT_EQ(out_code, 0u);
        ASSERT_EQ(out_bits, 0);
    }

    /* Bad leading mark (out of (7000,9500)): rejected. */
    {
        int bt[16];
        bt[0] = 6000; /* too short */
        bt[1] = 4500;
        for (int i = 2; i < 16; i++) bt[i] = 563;
        uint32_t out_code = 0xDEADBEEFu;
        int out_bits = -1;
        bool ok = ir_nec_decode(bt, 16u, &out_code, &out_bits);
        ASSERT_TRUE(ok == false);
        ASSERT_EQ(out_code, 0u);
        ASSERT_EQ(out_bits, 0);
    }

    /* Bad leading space (out of (3500,5000)): rejected. */
    {
        int bt[16];
        bt[0] = 9000;
        bt[1] = 2000; /* too short */
        for (int i = 2; i < 16; i++) bt[i] = 563;
        uint32_t out_code = 0xDEADBEEFu;
        int out_bits = -1;
        bool ok = ir_nec_decode(bt, 16u, &out_code, &out_bits);
        ASSERT_TRUE(ok == false);
        ASSERT_EQ(out_code, 0u);
        ASSERT_EQ(out_bits, 0);
    }

    /* A malformed MARK partway through stops decoding immediately, keeping
     * whatever bits were already accumulated -- transcribed from
     * ProcessIRData()'s own unconditional `return` on a bad mark
     * (rpIRComm.cpp:192-193), not a "skip this one bit and continue". */
    {
        /* count must be >= 16 to pass the header-length guard at all (the
         * original's own `if (iIRBitTimeCount < 16) return;`) -- the bad
         * mark sits at index 6, well before the buffer ends, so the
         * remaining filler entries (indices 8-15) are never read. */
        int bt[16] = { 9000, 4500,
                       563, 1500,  /* bit0: 1 */
                       563, 1500,  /* bit1: 1 */
                       9999, 1500, /* bad mark (way outside 400-650): stop here */
                       0, 0, 0, 0, 0, 0, 0 };
        uint32_t out_code = 0;
        int out_bits = 0;
        bool ok = ir_nec_decode(bt, 16u, &out_code, &out_bits);
        ASSERT_TRUE(ok == true);      /* header was valid, so processing "ran" */
        ASSERT_EQ(out_bits, 2);        /* only the two good bits before the bad mark */
        ASSERT_EQ(out_code, (uint32_t)((1u << 0) | (1u << 1)));
    }

    /* Deliberate bounds check (the one addition beyond the original -- see
     * the header): a trailing mark with no paired space entry must not read
     * past the caller's buffer. count=17 ends right after a valid mark at
     * index 16, with no index 17 to hold its space. */
    {
        int bt[17] = { 9000, 4500,
                       563, 200,   /* bit0: 0 (space well under 1000) */
                       563, 200,   /* bit1: 0 */
                       563, 200,   /* bit2: 0 */
                       563, 200,   /* bit3: 0 */
                       563, 200,   /* bit4: 0 */
                       563, 200,   /* bit5: 0 */
                       563, 200,   /* bit6: 0 */
                       563 };      /* dangling mark, index 16, no space to pair with */
        uint32_t out_code = 0xDEADBEEFu;
        int out_bits = -1;
        bool ok = ir_nec_decode(bt, 17u, &out_code, &out_bits);
        ASSERT_TRUE(ok == true);
        ASSERT_EQ(out_bits, 7);   /* the dangling mark contributes no 8th bit */
        ASSERT_EQ(out_code, 0u);
    }

    /* This driver's protocol carries no repeat-frame handling: unlike full
     * NEC (which defines a distinct ~9ms/2.25ms repeat marker),
     * BuildIRDataFromCode()/ProcessIRData() only ever build or expect a full
     * 32-bit frame (a 4500us leading space is REQUIRED by the header check
     * above) -- there is no shorter "repeat" encoding anywhere in
     * rpIRComm.{h,cpp} to port. A would-be repeat frame (a short leading
     * space around 2250us, as real NEC remotes send) is simply rejected by
     * the same leading-space check as any other malformed header. */
    {
        int bt[16];
        bt[0] = 9000;
        bt[1] = 2250; /* NEC's own repeat-frame space -- not in (3500,5000) */
        for (int i = 2; i < 16; i++) bt[i] = 563;
        uint32_t out_code = 0xDEADBEEFu;
        int out_bits = -1;
        bool ok = ir_nec_decode(bt, 16u, &out_code, &out_bits);
        ASSERT_TRUE(ok == false);
        ASSERT_EQ(out_code, 0u);
        ASSERT_EQ(out_bits, 0);
    }

    /* ---- ir_nec_carrier_burst_clkdiv() / ir_nec_carrier_control_clkdiv() /
     * ir_nec_rx_clkdiv(): PIO timing arithmetic, tested at this board's
     * actual clk_sys (200 MHz) and the Pico SDK's default (125 MHz) side by
     * side -- an assumed 125 MHz divider would be wrong by 1.6x on this
     * board (AGENTS.md: never hardcode a PIO divider). ---- */

    /* carrier burst: clk_sys / (38.222e3 * 4) -- the LIVE 38.222 kHz, not
     * the reference comment's 38.000 kHz. */
    ASSERT_TRUE(approx(ir_nec_carrier_burst_clkdiv(200000000u), 1308.147140390351));
    ASSERT_TRUE(approx(ir_nec_carrier_burst_clkdiv(125000000u), 817.5919627439695));
    ASSERT_TRUE(approx(
        (double)ir_nec_carrier_burst_clkdiv(200000000u) /
        (double)ir_nec_carrier_burst_clkdiv(125000000u), 1.6));

    /* carrier control: clk_sys / (2 * (1/562.5e-6)) -- the REFERENCE
     * 562.5 us (NOT the RX-only tuned 556.00 us -- see the header's trap 3
     * note for why these must stay two separate functions). */
    ASSERT_TRUE(approx(ir_nec_carrier_control_clkdiv(200000000u), 56250.0));
    ASSERT_TRUE(approx(ir_nec_carrier_control_clkdiv(125000000u), 35156.25));
    ASSERT_TRUE(approx(
        (double)ir_nec_carrier_control_clkdiv(200000000u) /
        (double)ir_nec_carrier_control_clkdiv(125000000u), 1.6));

    /* rx: clk_sys / (10.0/556.00e-6) -- the LIVE, tuned 556.00 us, not the
     * reference comment's 562.5 us. */
    ASSERT_TRUE(approx(ir_nec_rx_clkdiv(200000000u), 11120.0));
    ASSERT_TRUE(approx(ir_nec_rx_clkdiv(125000000u), 6950.0));
    ASSERT_TRUE(approx(
        ir_nec_rx_clkdiv(200000000u) / ir_nec_rx_clkdiv(125000000u), 1.6));

    /* ---- ir_comm_needs_init(): the idempotency guard, mirroring
     * pdm_mic_needs_init()/pdm's own trap 8. ---- */
    ASSERT_TRUE(ir_comm_needs_init(false) == true);   /* not ready: proceed */
    ASSERT_TRUE(ir_comm_needs_init(true)  == false);  /* already ready: skip */

    /* ---- PIO instruction-word tables: pin every word exactly. Two of the
     * three (burst, control) are transcribed unchanged from the original's
     * own compiled arrays; nec_receive was hand-encoded from the original's
     * runtime encode_*() call sequence -- see the header's PIO-programs
     * note for the derivation and cross-check. Any future "fix" to any of
     * these words must fail here first. ---- */
    ASSERT_EQ(ir_nec_carrier_burst_program_instructions[0], 0xe034u);
    ASSERT_EQ(ir_nec_carrier_burst_program_instructions[1], 0x20c7u);
    ASSERT_EQ(ir_nec_carrier_burst_program_instructions[2], 0xe001u);
    ASSERT_EQ(ir_nec_carrier_burst_program_instructions[3], 0xe100u);
    ASSERT_EQ(ir_nec_carrier_burst_program_instructions[4], 0x0042u);

    ASSERT_EQ(ir_nec_carrier_control_program_instructions[0], 0x80a0u);
    ASSERT_EQ(ir_nec_carrier_control_program_instructions[1], 0xe02fu);
    ASSERT_EQ(ir_nec_carrier_control_program_instructions[2], 0xc007u);
    ASSERT_EQ(ir_nec_carrier_control_program_instructions[3], 0x0042u);
    ASSERT_EQ(ir_nec_carrier_control_program_instructions[4], 0xaf42u);
    ASSERT_EQ(ir_nec_carrier_control_program_instructions[5], 0xc107u);
    ASSERT_EQ(ir_nec_carrier_control_program_instructions[6], 0x6021u);
    ASSERT_EQ(ir_nec_carrier_control_program_instructions[7], 0x0029u);
    ASSERT_EQ(ir_nec_carrier_control_program_instructions[8], 0xa342u);
    ASSERT_EQ(ir_nec_carrier_control_program_instructions[9], 0xc007u);
    ASSERT_EQ(ir_nec_carrier_control_program_instructions[10], 0x00e6u);

    ASSERT_EQ(ir_nec_receive_program_instructions[0], 0xe03fu);
    ASSERT_EQ(ir_nec_receive_program_instructions[1], 0x2020u);
    ASSERT_EQ(ir_nec_receive_program_instructions[2], 0x00c7u);
    ASSERT_EQ(ir_nec_receive_program_instructions[3], 0x0042u);
    ASSERT_EQ(ir_nec_receive_program_instructions[4], 0xa0c3u);
    ASSERT_EQ(ir_nec_receive_program_instructions[5], 0x20a0u);
    ASSERT_EQ(ir_nec_receive_program_instructions[6], 0x0000u);
    ASSERT_EQ(ir_nec_receive_program_instructions[7], 0xae42u);
    ASSERT_EQ(ir_nec_receive_program_instructions[8], 0x4001u);

    /* ---- NEC frame integrity, against REAL CAPTURED FRAMES.
     *
     * Every word below was received by this board from a household remote on
     * 2026-07-28 (the hardware record) -- 92 frames, of which THREE carried a command
     * byte that did not match its own complement. `ir_comm_read()` returned
     * those exactly like the good ones, because nothing in this BSP applied
     * NEC's built-in check, which is the entire reason the protocol carries a
     * complement in the first place.
     *
     * Frame layout, MSB first: [~cmd][cmd][addr_hi][addr_lo]. ---- */

    /* The four codes that arrived most often, all self-consistent. */
    ASSERT_TRUE(ir_nec_command_valid(0x55aac7eau));   /* cmd 0xAA, ~cmd 0x55 */
    ASSERT_TRUE(ir_nec_command_valid(0xd52ac7eau));   /* cmd 0x2A, ~cmd 0xD5 */
    ASSERT_TRUE(ir_nec_command_valid(0x54abc0e0u));   /* cmd 0xAB, ~cmd 0x54 */
    ASSERT_TRUE(ir_nec_command_valid(0xaa55e0e0u));   /* cmd 0x55, ~cmd 0xAA */

    /* The three that were genuinely corrupt in flight. 0xEA is not the
     * complement of 0x95 (that is 0x6A) nor of 0x2A (that is 0xD5). A caller
     * acting on these would act on a command the remote never sent. */
    ASSERT_TRUE(ir_nec_command_valid(0xea9547eau) == false);
    ASSERT_TRUE(ir_nec_command_valid(0xea9563f4u) == false);
    ASSERT_TRUE(ir_nec_command_valid(0xea2ac7eau) == false);

    /* A frame this board TRANSMITTED, round-tripped through its own receiver:
     * classic NEC, so the address halves are complements too. */
    ASSERT_TRUE(ir_nec_command_valid(0xcb34ed12u));
    ASSERT_TRUE(ir_nec_address_is_classic(0xcb34ed12u));
    ASSERT_EQ(ir_nec_command(0xcb34ed12u), 0x34u);
    ASSERT_EQ(ir_nec_address16(0xcb34ed12u), 0xed12u);

    /* The remote is EXTENDED NEC: 0xC7EA is a 16-bit address, not an
     * address-plus-complement (~0xEA is 0x15, not 0xC7). Both forms are legal,
     * so a valid command must NOT be rejected just because the address halves
     * are not complements -- that distinction is what this predicate exists to
     * make, and conflating the two would discard every extended remote. */
    ASSERT_TRUE(ir_nec_address_is_classic(0x55aac7eau) == false);
    ASSERT_EQ(ir_nec_address16(0x55aac7eau), 0xc7eau);
    ASSERT_EQ(ir_nec_command(0x55aac7eau), 0xaau);

    /* All-zero and all-ones: neither has a valid complement, and both are
     * what a stuck receive pin produces. */
    ASSERT_TRUE(ir_nec_command_valid(0x00000000u) == false);
    ASSERT_TRUE(ir_nec_command_valid(0xFFFFFFFFu) == false);

    ASSERT_TRUE(strcmp(ir_phy_name(IR_PHY_NEC), "NEC") == 0);
    ASSERT_TRUE(strcmp(ir_phy_name(IR_PHY_SIRC12), "SIRC12") == 0);
    ASSERT_TRUE(strcmp(ir_phy_name(IR_PHY_RC5), "RC5") == 0);
    ASSERT_EQ(ir_sirc_nbits(IR_PHY_NEC), 0u);
    ASSERT_EQ(ir_sirc_nbits(IR_PHY_SIRC12), 12u);
    ASSERT_EQ(ir_sirc_nbits(IR_PHY_SIRC20), 20u);
    ASSERT_TRUE(ir_phy_carrier_hz(IR_PHY_NEC) > 38000.0f);
    ASSERT_TRUE(ir_phy_carrier_hz(IR_PHY_SIRC12) == 40000.0f);
    ASSERT_TRUE(ir_phy_carrier_hz(IR_PHY_RC5) == 36000.0f);

    {
        int us[64];
        size_t n = ir_sirc_encode(0x95u, 12u, us, 64u);
        ASSERT_EQ(n, 26u); /* header + 12 bits * 2 */
        ASSERT_EQ(us[0], 2400);
        ASSERT_EQ(us[1], 600);
        ASSERT_EQ(us[2], 1200); /* LSB of 0x95 is 1 */
        ASSERT_EQ(us[3], 600);
        ASSERT_EQ(ir_sirc_encode(0u, 12u, us, 10u), 0u);
    }
    {
        int us[64];
        size_t n = ir_rc5_encode(0x2000u, us, 64u); /* bit 13 set */
        ASSERT_EQ(n, 56u);
        ASSERT_EQ(us[0], 0);
        ASSERT_EQ(us[1], 889);
        ASSERT_EQ(ir_rc5_encode(0u, us, 20u), 0u);
    }

    TEST_RETURN();
}
