/* Ship mode: one BATFET_DIS write to the BQ25896 at 0x6B on I2C1.
 *
 * Not a charger driver -- the bootloader is size-constrained and needs
 * exactly this one register. Verified against the legacy implementation
 * (rmpLib/rpBatteryChargeBQ25896.cpp setBattFETEnable): read REG09, set
 * bit 5, write it back. It is a read-modify-write because REG09 also
 * carries FORCE_ICO, the safety-timer settings, and the PUMPX bits, and
 * clobbering those on the way to powering down would be a poor trade.
 *
 * UNVERIFIED ON HARDWARE: the wake path. BATFET_DIS cuts battery power and
 * the design assumes the board wakes on USB or the charger's own
 * condition. If that is wrong, gray powers a board off with no way back
 * on. Confirm at bring-up BEFORE this is offered to users. */
#ifndef FWOG_BL_SHIP_H
#define FWOG_BL_SHIP_H
#include <stdbool.h>

/* Brings up I2C1 lazily -- the bootloader must not touch the charger
 * unless it is asked to. Returns false if the bus did not answer, in which
 * case the board is still on and the caller should say so. On success this
 * usually does not return, because power goes away. */
bool bl_ship_mode(void);

#endif
