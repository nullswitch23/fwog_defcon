/* Handing the CPU to the application.
 *
 * The bootloader jumps PAST the app's boot2, so the app inherits whatever
 * peripheral state the bootloader left behind -- XIP config, PWM dividers,
 * SPI, I2C, USB -- not reset defaults. Two consequences, both load-bearing:
 *
 *  1. bl_jump_to_app() must quiesce everything it touched. This is the
 *     classic source of "works from UF2, fails from the bootloader" bugs.
 *  2. Both binaries take PICO_FLASH_SPI_CLKDIV from bsp/boards/freewili_og.h,
 *     because the XIP timing the bootloader's boot2 established stays in
 *     force for the app. They must not diverge. */
#ifndef FWOG_BL_JUMP_H
#define FWOG_BL_JUMP_H
#include <stdbool.h>
#include "common/app_meta.h"

/* A last sanity check on the vector table before trusting it: the initial
 * MSP must point into RAM and the reset vector into the app's flash slot
 * with the Thumb bit set. A metadata record can be valid while the image it
 * describes is nonsense -- for instance a main-CPU binary sent to the wrong
 * slot -- and jumping to that hangs with no diagnostic. */
bool bl_app_image_ok(const fwog_app_meta_t *m);

/* Quiesce, set VTOR and MSP, branch to FWOG_APP_XIP_ADDR + 0x100.
 * Never returns. */
void bl_jump_to_app(void) __attribute__((noreturn));

#endif
