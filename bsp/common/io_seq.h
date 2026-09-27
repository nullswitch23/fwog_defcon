/* The one safe order for changing breakout I/O direction.
 *
 * Transcribed from FreeWilliMain.cpp:592-680 (UpdateIOAndAntennas):
 *
 *   1. open the IO_CONFIG window   (display, via the expander)
 *   2. write the FPGA's 0x02 register
 *   3. READ IT BACK AND COMPARE    <-- the safety property
 *   4. set the RP2040's own pads
 *   5. write the expander's shifter directions + antennas
 *   6. close the IO_CONFIG window
 *
 * Step 1 is not merely a bus gate: rpProcessHostCommand.cpp:65 shows the
 * display only latches direction bits while the flag is set, so skipping it
 * makes step 5 silently do nothing.
 *
 * Step 3 is why this file exists. A mismatch means the FPGA is not holding
 * what we think it holds, so proceeding to step 5 would point the level
 * shifters at a routing that no longer matches -- an output driving an
 * output. On mismatch this function performs steps 4 and 5 NOT AT ALL and
 * still closes the window, leaving the previous known-consistent state.
 *
 * A non-OK return must never be ignored. FWOG_IO_ERR_EXPANDER in particular
 * is not "nothing happened": by the time the expander can fail, the FPGA and
 * the RP2040's own pads have already moved to the new configuration, so that
 * path returns with the three-way agreement genuinely broken -- only the
 * shifters may still disagree.
 *
 * Pure, with the hardware injected, so that property is host-tested with no
 * FPGA present. */
#ifndef FWOG_IO_SEQ_H
#define FWOG_IO_SEQ_H
#include <stdbool.h>
#include <stdint.h>
#include "common/io_cfg.h"

typedef enum {
    FWOG_IO_OK = 0,
    FWOG_IO_ERR_ENABLE,        /* could not open the window */
    FWOG_IO_ERR_FPGA_WRITE,
    FWOG_IO_ERR_FPGA_READ,
    FWOG_IO_ERR_FPGA_VERIFY,   /* readback disagreed -- nothing else touched */
    FWOG_IO_ERR_EXPANDER,
    /* Appended at the END of the enum, not sorted in with the rest: the
       bench console prints these as bare integers (cmd_iodir), so an
       existing numeric value must never shift. Returned by
       fwog_io_dir_apply() itself, before fwog_io_sequence() runs at all --
       the requested fwog_io_cfg_t cannot be expressed on this board (see
       gpio25_out's doc in io_cfg.h). Nothing was touched. */
    FWOG_IO_ERR_UNSUPPORTED
} fwog_io_result_t;

typedef struct {
    bool (*config_enable)(void *ctx, bool on);
    bool (*fpga_write)(void *ctx, const uint8_t d[2]);
    /* Contract: fpga_read may return true ONLY after writing BOTH bytes of
       d[]. The caller's readback buffer is seeded so a true return with an
       unwritten byte cannot be mistaken for a verified match. */
    bool (*fpga_read)(void *ctx, uint8_t d[2]);
    void (*pads_apply)(void *ctx, const fwog_io_cfg_t *cfg);
    bool (*expander_apply)(void *ctx, const fwog_io_cfg_t *cfg);
    void *ctx;
} fwog_io_ops_t;

fwog_io_result_t fwog_io_sequence(const fwog_io_ops_t *ops,
                                  const fwog_io_cfg_t *cfg);

#endif
