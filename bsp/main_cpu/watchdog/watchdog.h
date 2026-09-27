/* The watchdog is the ONLY way to recover a hung main CPU on the FreeWili OG.
 * Nothing else can reset it — a hang means disconnecting the battery or
 * waiting for it to die. board_init() enables it; every main-CPU application
 * must kick it.
 *
 * The display CPU deliberately has no watchdog: main can already reset it via
 * GUI_NRESET, and the display bootloader waits for main by design. */
#ifndef FWOG_WATCHDOG_H
#define FWOG_WATCHDOG_H
#include <stdbool.h>
#include <stdint.h>

/* RP2040's watchdog counter is 24 bits ticking at 2 per microsecond, so the
 * hardware maximum window is a little over 8.3 s. The SDK's own bound is
 * WATCHDOG_LOAD_BITS / (1000 * 2) = 8388 ms; the 2 is errata RP2040-E1, which
 * makes the counter decrement twice per tick. 8300 sits just inside it. */
#define FWOG_WATCHDOG_LONG_MS 8300u

/* The window is the hardware maximum, deliberately.
 *
 * This watchdog is a worst-case lockup backstop, not a liveness monitor.
 * Nothing on this CPU needs a stall noticed sooner, and a short window buys
 * nothing except an obligation for every legitimately slow operation to
 * reason about it. A hung main CPU now recovers in ~8.3 s rather than ~2 s,
 * which is the whole cost.
 *
 * Because this EQUALS FWOG_WATCHDOG_LONG_MS, board_watchdog_pause() widens
 * nothing -- see the note above it before assuming it buys headroom. */
#ifndef FWOG_WATCHDOG_MS
#define FWOG_WATCHDOG_MS FWOG_WATCHDOG_LONG_MS
#endif

/* Enabled by board_init() unless FWOG_WATCHDOG_MS is 0. */
void board_watchdog_init(void);
void board_watchdog_kick(void);

/* ---- What the link error proves, and what it does NOT ----
 *
 * board_init() references fwog_watchdog_policy_declared, which only
 * FWOG_WATCHDOG_DEFAULT or FWOG_WATCHDOG_CUSTOM defines. A main-CPU
 * application that declares NEITHER fails to link.
 *
 * That proves a policy was DECLARED. It does NOT prove board_watchdog_kick()
 * is reached on every path -- no linker can see that. This mirrors
 * fwog_power_policy_declared on the display CPU; power_poll.h states the same
 * limit, for the same reason.
 *
 * The reference in board_init() is UNCONDITIONAL -- it is not guarded on
 * FWOG_WATCHDOG_MS being non-zero. A build with the watchdog compiled out is
 * exactly the case where the author most needs to have said so out loud, and
 * FWOG_WATCHDOG_CUSTOM is the declaration for it. */
extern const unsigned char fwog_watchdog_policy_declared;

/* This app calls board_watchdog_kick() from its main loop. What almost every
 * main app should say. */
#define FWOG_WATCHDOG_DEFAULT() \
    const unsigned char fwog_watchdog_policy_declared = 1

/* This app owns the watchdog itself -- it builds with FWOG_WATCHDOG_MS 0, or
 * kicks from somewhere other than a main loop. Legitimate, but it is a claim
 * the declarer is making, not one anything checks. */
#define FWOG_WATCHDOG_CUSTOM() \
    const unsigned char fwog_watchdog_policy_declared = 2

/* The nesting arithmetic behind board_watchdog_pause()/resume(), split out
 * so it is host-testable (see AGENTS.md's Conventions section on host
 * tests). Each returns true when the
 * caller must actually change the hardware window -- true only at depth
 * transitions 0->1 and 1->0. A resume at depth 0 is a no-op and must not
 * underflow: a wrapped depth would make the next pause do nothing. While
 * FWOG_WATCHDOG_MS is the hardware maximum that is harmless -- pause and
 * resume re-arm the same window either way -- but the arithmetic is what
 * makes LOWERING the base window safe again, so it stays correct and tested.
 *
 * Not part of the app-facing API; exposed for the test and for anything
 * that needs the same nesting rule. */
bool fwog_wdt_pause_step(uint32_t *depth);
bool fwog_wdt_resume_step(uint32_t *depth);

/* Widen the window to FWOG_WATCHDOG_LONG_MS around a long blocking operation
 * such as flashing the display. This does NOT disable protection — callers
 * should still kick from inside the operation where they can. Nests.
 *
 * INERT AS BUILT, and that is not a bug. FWOG_WATCHDOG_MS already IS
 * FWOG_WATCHDOG_LONG_MS, so these re-arm the window they were given and widen
 * nothing. They do still reload the counter, so a call acts as a kick.
 *
 * They are kept, and still called, for two reasons: a call site that exists
 * is what makes lowering FWOG_WATCHDOG_MS safe later, and their presence
 * marks which operations are the slow ones. Do NOT start relying on them for
 * headroom -- there is none left to give. Every long write path kicks partway
 * through and must continue to; see fs/fs_geom.h. */
void board_watchdog_pause(void);
void board_watchdog_resume(void);

/* True when the last reset came from the watchdog rather than power-on. */
bool board_watchdog_caused_reboot(void);

#endif
