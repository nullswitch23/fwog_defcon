/* Umbrella include for display-CPU applications. */
#ifndef FWOG_DISPLAY_H
#define FWOG_DISPLAY_H

/* "platform/board.h" below is a relative include: it resolves against
 * whichever library's include dirs are active, not against "the display
 * CPU's board.h" specifically. A main-CPU app (linking fwog_main_bsp, which
 * puts main_cpu/ on the include path) that mistakenly includes THIS umbrella
 * header still has "platform/board.h" resolve to main_cpu/platform/board.h
 * -- its own correct file for that TU. That file's own guard only fires on
 * FWOG_CPU_DISPLAY, which isn't defined here, so it says nothing about
 * having arrived via the wrong umbrella header and compiles clean, silently
 * binding the main pin map (e.g. PIN_I2C_SDA == 16, not 26) in a source file
 * that asked for the display one. This guard catches that mismatch directly,
 * before the relative include even runs. */
#if defined(FWOG_CPU_MAIN) && !defined(HOST_TEST)
#error "fwog_display.h included in a main-CPU translation unit"
#endif

#include "platform/board.h"
#include "common/app_meta.h"
#include "common/link/bl_proto.h"
#include "common/link/link_uart.h"
#include "common/clocks.h"
#include "common/diag.h"
#include "common/i2c_bus.h"
#include "common/crc.h"
#include "common/link/link_frame.h"
#include "common/dsp/cic.h"
#include "common/dsp/dcblock.h"
#include "common/dsp/spectrum.h"
#include "lcd/st7789.h"
#include "lcd/lcd_text.h"
#include "lcd/fwog_t9.h"
#include "lcd/fwog_splash.h"
#include "io_expander/pcal6416.h"
#include "io_expander/ioexp_link.h"
#include "input/buttons.h"
#include "power/bq25896.h"
#include "power/ship_mode.h"
#include "power/power_poll.h"
#include "sensors/mcp7940.h"
#include "sensors/lis3dh.h"
#include "leds/ws2812_driver.h"
#include "pdm/pdm_mic.h"
#include "audio/i2s_audio.h"
#include "ir/ir_comm.h"
#endif
