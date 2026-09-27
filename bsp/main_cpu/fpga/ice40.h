/* iCE40 SPI-slave configuration, from rpFPGAICE40::programFPGA /
 * programmingPrep / programmingWrapUp (Lattice FPGA-TN-02001-3.4 p.32).
 *
 * NOT declared in bsp/fwog_main.h, deliberately: there is no public
 * arbitrary-bitstream API. See fpga_bitstream.h for why.
 *
 * The NVCM/OTP paths (programFPGA_NVCM, secureFPGA_NVCM, nvcmProgramLockBits)
 * are NOT ported. They are irreversible and have no place in a BSP.
 *
 * ---- Two deliberate deviations from the reference ----
 *
 * 1. The CRESET_B low hold is 5 us, not the reference's "200 ns" loop.
 *    That loop computes
 *        int iDelay = (200 * (clock_get_hz(clk_sys) / 1000000000));
 *    and clock_get_hz()/1e9 is INTEGER division -- 200000000/1000000000 is
 *    ZERO -- so it runs exactly one nop, about 8 ns. This is not inference:
 *    the reference's own NVCM variant carries the fix and the reason
 *    ("the old iDelay integer-truncated to 0 -> ~8ns pulse, too short") and
 *    uses sleep_us(5). The short pulse survives in shipping firmware only
 *    because a cold-boot part is unconfigured; it fails when re-configuring
 *    an already-configured device, which is exactly what this BSP does every
 *    time main restarts without a power cycle.
 *
 * 2. CRESET_B is always actively driven, never released to input. The
 *    reference records "no external pull-up on board" at the same site.
 *
 * 3. CS (PIN_IO_SPI_CS) stays asserted LOW across the whole bitstream
 *    write. The reference does not do this: programmingPrep() ends with CS
 *    driven HIGH plus 8 dummy clocks, and programFPGA() then streams the
 *    bitstream with no CS change at all -- its SPI wrapper never touches
 *    CS, so on the reference CS sits wherever programmingPrep() left it
 *    (HIGH) for the entire transfer. That is not what FPGA-TN-02001-3.4
 *    p.32 describes for the SPI-slave configuration sequence; holding CS
 *    low across configuration is what the datasheet actually calls for,
 *    and this port follows the datasheet rather than the reference here.
 *    Kept anyway, deliberately, rather than silently matched to the
 *    reference: the reference is what demonstrably configures this part on
 *    real hardware, and AGENTS.md asks for register sequences to be kept as
 *    the original had them, so a divergence needs to be declared, not
 *    inferred from a diff.
 *
 *    Consequence worth stating plainly: PIN_IO_SPI_CS is a breakout header
 *    pin behind a level shifter (see fwog_ice40_spi_claim()'s comment for
 *    why it is treated as an on-board-device pin, not a user pin). Every
 *    boot therefore asserts the user's own SPI chip-select LOW for roughly
 *    170 ms while 104 KB clocks out at 5 MHz on SCLK/MOSI -- onto whatever
 *    the user has wired to that header -- not merely an internal
 *    FPGA-configuration detail confined to the FPGA net.
 *
 * PIN_FPGA_RESET's active-LOW polarity was unverified (the hardware record). CDONE
 * going high after this runs is the confirmation: wrong polarity means the
 * part never leaves reset and CDONE never asserts.
 *
 * ---- A known boot-state gap this file does NOT close ----
 *
 * fwog_ice40_spi_release() hands SCLK/MOSI/MISO back as plain, weakly-pulled
 * inputs (see that function's own comment for why CS is the one pin that
 * stays actively driven instead). The reference's restoreSPI() does
 * something this port cannot: it restores the *user's configured* pad
 * directions. ice40.c has no access to a fwog_io_cfg_t -- and reaching for
 * one would duplicate state that gpio/breakout.c's fwog_io_dir_apply()
 * already owns -- so it cannot do what restoreSPI() does.
 *
 * The result is real: after board_init() returns, main's four breakout SPI
 * pads sit as weakly-pulled-down inputs while the expander's
 * resistor-established default (the peripheral catalog's PCAL6416 note:
 * port 0 comes up 0xDA) already points those level shifters main->header.
 * So the shifters translate a floating input onto the breakout header until
 * something moves all three surfaces back into agreement. That something is
 * fwog_io_dir_apply() (gpio/breakout.h) -- it is the one function on this
 * board that owns bringing the FPGA, the pads and the expander back into
 * step -- and the gap lasts exactly as long as it takes a display
 * application to come up and a caller to invoke it. Nothing on the breakout
 * header should be assumed stable before that has happened at least once.
 *
 * ---- A surprising property of this file, worth knowing before you go
 * looking for a missing 104 KB ----
 *
 * ice40.c is a member of the fwog_main_bsp static archive, and the 104 KB
 * default bitstream (fpga_bitstream.S, fwog_fpga_bitstream[]) is pulled into
 * an app's image ONLY because ice40.c references it -- and ice40.c itself is
 * pulled into the link ONLY if something in the app's reachable call graph
 * calls fwog_ice40_load_default(), fwog_ice40_spi_claim(), or
 * fwog_ice40_spi_release(). A static-archive member is extracted solely to
 * resolve an already-undefined symbol; an app that never calls into this
 * file leaves ice40.c.obj -- and, transitively, the bitstream -- sitting
 * unused in the .a, and its own .elf comes out about 104 KB SMALLER, with no
 * link warning. This is not a bug: it is exactly the same "no consumer, no
 * corpse in your image" property that fpga_bitstream.h's own comment
 * describes as the point of keeping the loader out of bsp/fwog_main.h. It
 * only becomes surprising when you expect the bitstream to be present
 * because ice40.c "references" it -- referencing is necessary but not
 * sufficient; the reference has to be reachable too. Confirm both are
 * happening for your app with:
 *     arm-none-eabi-nm your_app.elf | grep fwog_fpga_bitstream
 * which should show two symbols 0x1969A (104090) bytes apart once
 * something calls fwog_ice40_load_default().
 *
 * That paragraph was true when written and is now UNREACHABLE for every app
 * that links fwog_main_bsp: board.c's board_init() itself now calls
 * fwog_ice40_load_default() unconditionally (bring-up needs the FPGA
 * configured before anything else can use the breakout header), so every
 * app that calls board_init() -- which is every main-CPU app -- pulls in
 * ice40.c and the 104 KB bitstream regardless of whether the app itself
 * ever mentions fpga/ice40.h. The "104 KB smaller with no consumer" case
 * described above would require a main-CPU binary that skips board_init()
 * entirely, which no app in this repo does. Left here rather than deleted:
 * the linker mechanics it describes are still correct and still worth
 * knowing, for instance if a future size-constrained context on this CPU
 * ever wants board_init() split further. */
#ifndef FWOG_ICE40_H
#define FWOG_ICE40_H
#include <stdbool.h>

/* Configure the FPGA from the embedded default bitstream. Returns the state
 * of CDONE, i.e. true when the part configured.
 *
 * Requires fwog_clocks_init() and board_init_pins(). The FPGA clock should
 * already be running (fwog_fpga_clk_start()). Takes roughly 200 ms at
 * 5 MHz for 104 KB, so main-CPU callers must widen the watchdog around it --
 * board_init() does this for you. */
bool fwog_ice40_load_default(void);

/* Put the breakout SPI into the mode FPGA register access needs, and give it
 * back. Task 7's register client brackets every transaction with these, the
 * way the reference's claimSPI()/restoreSPI() do. `hz` lets configuration
 * (5 MHz) and register access (3.25 MHz) share one implementation. */
void fwog_ice40_spi_claim(unsigned hz);
void fwog_ice40_spi_release(void);

#endif
