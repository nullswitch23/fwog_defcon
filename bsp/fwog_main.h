/* Umbrella include for main-CPU applications. */
#ifndef FWOG_MAIN_H
#define FWOG_MAIN_H

/* Mirror of the guard in fwog_display.h: "platform/board.h" below resolves
 * against whichever library's include dirs are active. A display-CPU app
 * that mistakenly includes this umbrella header would have it resolve to
 * display_cpu/platform/board.h -- its own correct file for that TU, whose
 * own guard only fires on FWOG_CPU_MAIN and so says nothing about the wrong
 * umbrella header having been used -- and silently bind the main pin map. */
#if defined(FWOG_CPU_DISPLAY) && !defined(HOST_TEST)
#error "fwog_main.h included in a display-CPU translation unit"
#endif

#include "platform/board.h"
#include "display_update/display_update.h"
#include "watchdog/watchdog.h"
#include "radio/cc1101.h"
#include "gpio/breakout.h"
#include "bottlenose/c6_image.h"
#ifndef HOST_TEST
#include "bottlenose/esp_rom_flash.h"
#include "bottlenose/bn_flash.h"
#endif
#include "common/app_meta.h"
#include "common/link/bl_proto.h"
#include "common/link/link_uart.h"
#include "common/clocks.h"
#include "common/diag.h"
#include "common/i2c_bus.h"
#include "common/crc.h"
#include "common/link/link_frame.h"
/* CPU-agnostic DSP. Nothing on main uses these today; they are here because
 * bsp/common/ is by definition available to both CPUs and leaving one umbrella
 * header out of step with the other is how a main-CPU app ends up including a
 * display header to reach a filter. --gc-sections drops what is unused. */
#include "common/dsp/cic.h"
#include "common/dsp/dcblock.h"
#include "common/dsp/spectrum.h"
/* The filesystem is NOT included here. fwog_main_fs is a separate, opt-in
 * library (bsp/CMakeLists.txt) and an app that does not link it must not get
 * declarations for functions it cannot call -- the link error is clearer than
 * a compile that succeeds and fails at the end. Apps that link fwog_main_fs
 * include "fs/fwog_fs.h" and "fs/fs_geom.h" themselves. */
#endif
