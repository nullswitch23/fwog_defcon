/* Ship mode: the one BQ25896 write that powers this board off.
 *
 * CTRL1 (register 0x09) bit 5 is BATFET_DIS. Setting it disconnects the
 * battery FET and the board goes dark (the hardware record). It is a ONE-WAY DOOR
 * WHILE THE BOARD IS DARK -- nothing on this board, in any firmware, can undo
 * it, because nothing is running to try. The BIT is not permanent: any
 * application that calls bq25896_start_charging() clears it on its next boot,
 * because that function begins with bq25896_reset(). That is not automatic --
 * nothing in board_init() does it, the application must -- but bench_display
 * does (apps/bench/display/main.c), and the hardware record records the result: CTRL1
 * read back 0x00 after the gray wake. So the door is one-way until something
 * gets the board powered again; there are exactly two ways to do that, both
 * outside software:
 *
 *   - Attach USB. VBUS re-enables the FET. Datasheet behaviour, and the board
 *     owner reports it works on this hardware. That is OPERATIONAL EXPERIENCE,
 *     the same evidence class as item 2 below -- NOT a bench measurement. The
 *     2026-07-28 run did not test it: the cable went back on while the board
 *     was already awake, so nothing in the hardware record bears on it either way.
 *   - Hold the GRAY button past 2 s. A hardware circuit, so there is nothing
 *     to implement and nothing that can go wrong in firmware. It is the only
 *     thing that distinguishes "ship mode" from "bricked until you find a
 *     cable", which is why the evidence for it is spelled out here.
 *
 *     This bullet used to say the wake had "never been verified on a board".
 *     That was true when written and is now CORRECTED -- not dropped -- by
 *     three things of three different weights:
 *
 *       1. The hardware record traces the gray net to the BQ25896's /QON pin, and
 *          closes the far end of that trace on the sheet: the schematic's own
 *          `Grey Button` legend sits beside the GPIO14_RP1 port on sheet 2/12,
 *          so "GPIO 14 is gray" no longer rests only on legacy C++. That is
 *          still a schematic read (pdfminer over the rev5 PDF) with ZERO
 *          boards, and the sheet-12 polyline geometry is still a single
 *          unreplicated extraction. It says the wire exists.
 *       2. The board owner reports this wake path has worked on this hardware
 *          for years. That is OPERATIONAL EXPERIENCE, not a measurement, and
 *          it is not this file's evidence -- it is why the old framing was
 *          worth re-examining.
 *       3. The hardware record is the BENCH RUN, and it is TWO evidence classes, not
 *          one: 2026-07-28, ONE board, USB detached, a 2-3 s gray hold brought
 *          the board back -- HUMAN-OBSERVED, no instrument was watching, and
 *          that GRAY rather than something else did it rests on the operator.
 *          Both CPUs' up_ms had restarted from a running board, and THAT is
 *          the instrumented half: proof of a real power cycle, because the
 *          display CPU has no watchdog and no firmware path can restart it.
 *
 *     The 2 s THRESHOLD in this file is still UNSOURCED. Nothing above bounds
 *     it: the hardware record traced the wire and not the timing, and the hardware record's
 *     hold was 2-3 s, which shows a hold in that range wakes the board and
 *     says nothing about where the boundary is. Do not read the correction
 *     above as a citation for the number.
 *
 *     Also unmeasured: what the board draws once the FET is open. Nothing on
 *     this board can measure it, so "it powered off" is not "it is down to a
 *     shelf-safe leakage". A meter on the battery is the only real check.
 *
 * ---- Why this file exists, and its boundary with bl_ship.c ----
 *
 * bsp/display_cpu/bootloader/bl_ship.c performs the SAME read-modify-write on
 * the same bit. This file does not call it, and it does not call this file.
 * They live in two binaries that never link together -- the bootloader cannot
 * link application code -- so there is no sharing available even if the
 * boundary permitted it. Six duplicated lines is the correct answer here; do
 * not "unify" them.
 *
 * bq25896.h's ship-mode boundary note says that driver deliberately does not
 * port shutDown()/setBattFETEnable(). That still holds: the dangerous write
 * stays out of the general-purpose charger driver, where a caller reaching for
 * telemetry could find it by accident, and lives here, in the file named after
 * the dangerous operation. That relocation is the whole point of this file.
 *
 * ---- The 6 s hold is INVENTED HERE, not ported ----
 *
 * FWOG_SHIP_HOLD_MS does not come from the classic firmware. Every caller of
 * rpBatteryChargeBQ25896::shutDown() in freewiliclassicdisplay is something
 * else: a menu action (rpPanelManager.cpp:756), an under-voltage trip after 4
 * consecutive readings below 3.0 V (rpPowerManager.cpp:69), and a
 * panel-manager shutdown request (FreeWilliDisplay.cpp:405). Searching that
 * tree for a 6000 ms constant finds nothing. The red button is already the
 * power-on BOOTSEL button for the main CPU, which makes it the natural "power"
 * button on this board and keeps the convention coherent rather than
 * arbitrary -- but the timing is a convention this BSP is SPECIFYING, not
 * transcribing. Do not let a future reader assume it came from somewhere
 * authoritative.
 *
 * The under-voltage trip path is deliberately NOT implemented. The legacy rule
 * (4 consecutive readings below 3.0 V) is a policy an application can build on
 * bq25896_read_all(); baking it into the BSP would mean the BSP can power the
 * board off on its own, without an application ever asking.
 *
 * ---- Interaction with the main CPU's red-button lockout (GPIO 8) ----
 *
 * The red button is not only a display-CPU input. Through an SN74LVC1G126
 * tri-state buffer it also reaches the MAIN CPU's QSPI_SS pin -- main's
 * BOOTSEL strap at power-on and its FLASH CHIP SELECT while running. The
 * display CPU's GPIO 8 is that buffer's active-high OE, and board_init_pins()
 * drives it LOW, which should disconnect the red button from main entirely.
 * See display_cpu/platform/board.h's "Main CPU boot control" block and
 * the hardware record.
 *
 * That chain was reasoned from the schematic with zero boards. It is now
 * MEASURED AT RESET -- the hardware record, 2026-07-28, ONE board, WITH a negative
 * control: GPIO 8 LOW and red held across a main reset boots main normally,
 * GPIO 8 HIGH and the same press puts main in BOOTSEL.
 *
 * READ THAT SCOPE CAREFULLY. The measurement is of the BOOTSEL STRAP at reset.
 * The worry a 6 s hold actually raises is the OTHER half of that pin -- main's
 * FLASH CHIP SELECT while running -- and no measurement here touches it.
 * The hardware record closes the runtime case by ARGUMENT, not by measurement: at
 * runtime main drives QSPI_SS with a 4 mA pad and R15's 1 kOhm pull can draw
 * only ~3.3 mA, never crossing the flash's threshold, so the button cannot
 * assert chip select whether or not the lockout is engaged. (That same
 * arithmetic is why fact 38's proposed `flashcrc` test could never have worked
 * and must not be attempted: it would have printed `stable=yes` in its own
 * negative control.) So: strap case measured, runtime case reasoned.
 *
 * It mattered more here than anywhere else in the tree, because a 6 s hold is
 * by a wide margin the longest red press this board has ever been asked to
 * sustain; had the lockout reasoning been wrong, this feature would assert
 * main's flash chip select for six continuous seconds every time someone
 * tested it.
 *
 * P1's lockout is still the PREREQUISITE for this package's 6 s hold -- it has
 * simply been satisfied. The hardware record's bench run read `bootoe level=0` before
 * pressing red, and that check belongs in any future procedure that holds red
 * for six seconds.
 *
 * ---- What has, and has not, run on a board ----
 * VERIFIED on hardware 2026-07-28, ONE board (the hardware record): the 6 s hold
 * fires; fwog_ship_enter()'s write lands (REG09 00 -> 20, the only byte that
 * changed AGAINST THE IMMEDIATELY-PRE-WRITE DUMP -- not against the one taken
 * at the start of the session, which differs in three further bytes the
 * charger moved on its own; the hardware record prints both); the board goes dark when
 * USB is then removed and does not come back on its own; and a 2-3 s gray hold
 * wakes it with USB still detached, both CPUs' up_ms restarting.
 *
 * The sub-6 s guard held too, but weigh its two halves separately. The
 * INSTRUMENTED half is an absence: no `ship` line appeared anywhere in a 300 s,
 * 151-line transcript. That the four ~3 s holds and the brush actually happened
 * inside that window is the OPERATOR's observation, which is what makes the
 * silence a negative control rather than nothing at all -- corroborated by the
 * operator watching the WS2812 bar fill and then revert on each hold, which is
 * direct evidence the press reached fwog_ship_step() and armed it and that the
 * release discarded the countdown.
 *
 * NOT verified, and a reader must not assume otherwise:
 *   - Ship-mode current draw. Unmeasurable on this board; see above.
 *   - The read-modify-write in fwog_ship_enter(). CTRL1 read 0x00 on the day,
 *     so there were no other bits to preserve and a blind write would have
 *     produced an identical result. The RMW is still correct; it was not
 *     exercised.
 *   - The gray-wake THRESHOLD. See the bullet at the top of this file.
 *   - That a USB re-attach wakes it FROM ship mode -- unverified BY THIS RUN.
 *     The cable went back on while the board was already awake. The board owner
 *     reports the path works (see the bullet at the top); that is operational
 *     experience standing in for a measurement nobody has taken here.
 *   - The dark, the wake, and phase 2's holds were HUMAN-OBSERVED, not
 *     instrument-logged. The machine-readable half is the up_ms restart, the
 *     register dumps, and the absence of a `ship` line. That evidence does rule
 *     out ONE alternative largely on its own: up_ms read 63470 at the first
 *     heartbeat after the cable went back on, so the board had been running
 *     ~63 s already -- and unless a full minute passed between the reattach and
 *     that read, the boot preceded the reattach and the USB attach was not the
 *     wake. That GRAY specifically did it rests on the operator.
 *   - The under-voltage trip path, deliberately not implemented (above).
 *   - A second board. Everything above is ONE board.
 *
 * The hold detector remains host-tested (tests/test_ship_mode.c): that proves
 * the arithmetic, and the bench run proves the binding around it.
 */
#ifndef FWOG_SHIP_MODE_H
#define FWOG_SHIP_MODE_H
#include <stdbool.h>
#include <stdint.h>

/* ---- Pure: host-tested, no I2C, no GPIO, no SDK ---- */

typedef enum {
    FWOG_SHIP_IDLE = 0,   /* not held                              */
    FWOG_SHIP_ARMING,     /* held, countdown running               */
    FWOG_SHIP_FIRED       /* threshold reached; fires exactly once */
} fwog_ship_phase_t;

/* Zero-initialise (`fwog_ship_state_t s = {0};`) and the machine starts IDLE. */
typedef struct {
    fwog_ship_phase_t phase;
    uint32_t          press_ms;   /* when the current hold began */
} fwog_ship_state_t;

#define FWOG_SHIP_HOLD_MS 6000u

/* Feed the red button's DEBOUNCED level once per loop -- the `down` bit from
 * fwog_buttons_poll(), or fwog_btn_step()'s event. This never samples
 * PIN_BTN_RED itself, so it composes with the existing debouncer rather than
 * racing it. (fwog_btn_held_ms() already existed and was exercised by nothing;
 * this is the first driver in the tree built on that same debounced level.)
 *
 * ANY release returns the machine to IDLE and DISCARDS the countdown. There is
 * no accumulation across presses: six one-second presses cannot power the
 * board off, and neither can a brush against the button. That guard is the
 * only thing standing between a pocket and a dark board.
 *
 * `now_ms` is any monotonic millisecond clock; elapsed time is measured with
 * unsigned subtraction, which is correct across the ~49.7-day uint32_t wrap.
 * The caller may poll at any rate, including an irregular one, because the
 * elapsed time is measured rather than assumed.
 *
 * ---- The return value is NOT always s->phase. Read this. ----
 *
 * The return is what happened ON THIS STEP. `s->phase` is the latched state.
 * They differ in exactly one situation: after firing, while the button is
 * still held, `s->phase` stays FWOG_SHIP_FIRED but the return is
 * FWOG_SHIP_ARMING. That is what makes FWOG_SHIP_FIRED come back exactly once
 * per continuous hold.
 *
 * It matters because fwog_ship_enter() can FAIL (a charger that does not
 * answer). Without the once-only guarantee, a caller looping on
 * `s.phase == FWOG_SHIP_FIRED` would retry the I2C write every iteration
 * forever. Test the RETURN VALUE, never the field:
 *
 *     if (fwog_ship_step(&s, red_down, now_ms) == FWOG_SHIP_FIRED)
 *         (void)fwog_ship_enter();
 */
fwog_ship_phase_t fwog_ship_step(fwog_ship_state_t *s, bool red_pressed,
                                 uint32_t now_ms);

/* 0..100 progress through the hold, for LED feedback. 0 whenever IDLE, 0 on
 * the step the press begins, 100 at and after the threshold (clamped, never
 * wrapped). Monotonic within one hold; a release resets it to 0.
 *
 * The countdown is rendered by the APPLICATION, not by this driver -- see
 * apps/bench/display/main.c, which maps this onto the seven-pixel WS2812
 * chain. Keeping that mapping out of here is what lets this whole half be
 * host-tested with no dependency on ws2812_driver.h. It also mirrors what the
 * legacy firmware does immediately before shutDown(): FreeWilliDisplay.cpp:
 * 392-400 writes a distinct colour to each of the seven pixels, from the
 * application layer. */
unsigned fwog_ship_progress(const fwog_ship_state_t *s, uint32_t now_ms);

/* The single BATFET_DIS write, and the only function in this BSP outside the
 * bootloader that can power the board off. Under HOST_TEST it logs
 * "ship would run" and returns true without opening the pack FET, so the
 * process stays up for a frame dump.
 *
 * Returns false if the charger does not answer or the write fails. Returns
 * true having powered the board off -- which the caller will not observe,
 * because it stops executing. Treat a `true` return as unreachable.
 *
 * Read-modify-write, not a blind write: CTRL1 carries five other live bits
 * (bq25896_setup_boost() writes 0x83 into this same register during
 * bq25896_start_charging()) and clobbering them on the way out would be a
 * silent behaviour change on a board that is about to stop running anyway --
 * but the read is also what proves the charger is there at all, which is the
 * only failure this function can usefully report. */
bool fwog_ship_enter(void);

#endif
