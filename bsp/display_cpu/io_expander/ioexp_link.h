/* Display-side handler for the 0x20-0x22 breakout I/O messages.
 *
 * The expander lives on this CPU's I2C1 but nearly everything it drives
 * belongs to MAIN -- so main initiates and this side executes. See
 * common/link/io_proto.h.
 *
 * This is APPLICATION-side only. The display serial bootloader must NOT
 * link it: it cannot reach the expander without bringing up I2C1, which
 * also carries the BQ25896 where one write powers the board off
 * (the hardware record), and it deliberately does not bring up I2C for that reason.
 * See io_expander/pcal6416.h. */
#ifndef FWOG_IOEXP_LINK_H
#define FWOG_IOEXP_LINK_H
#include <stdbool.h>
#include <stddef.h>
#include "io_expander/pcal6416.h"

/* Handle one decoded link payload. Returns true if it was a 0x20-0x22
 * message (in which case an ack has been sent), false if it belongs to
 * someone else and the caller should keep looking. */
bool fwog_ioexp_link_handle(const void *payload, size_t len);

/* ---- THE display CPU's single copy of the expander's state ----
 *
 * The PCAL6416 has one state and fwog_ioexp_apply() writes all sixteen bits
 * at once, so anything that keeps its OWN fwog_ioexp_cfg_t and applies it
 * silently overwrites everyone else's bits. That is not hypothetical: until
 * 2026-07-28 apps/bench/display's `ant` command built a fresh
 * fwog_ioexp_default() and applied it, resetting io_config and all nine
 * shifter directions -- the ones main sets over the link and is actively
 * relying on -- while its comment claimed it "changes ONLY the two antenna
 * fields".
 *
 * So the config this file maintains is the only one, and everything else goes
 * through these. Do not add another writer of the expander. */

/* Read-only view of what this CPU believes the expander holds. Never NULL. */
const fwog_ioexp_cfg_t *fwog_ioexp_link_cfg(void);

/* Change ONLY the two antenna paths and write the result, leaving the
 * directions, the pull-up and io_config exactly as they are. Returns the I2C
 * result. */
bool fwog_ioexp_link_set_antennas(fwog_ant_t radio1, fwog_ant_t radio2);

#endif
