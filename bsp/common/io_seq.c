#include "common/io_seq.h"

fwog_io_result_t fwog_io_sequence(const fwog_io_ops_t *ops,
                                  const fwog_io_cfg_t *cfg) {
    /* 1. Open the window. Nothing else can take effect without it. */
    if (!ops->config_enable(ops->ctx, true)) return FWOG_IO_ERR_ENABLE;

    fwog_io_result_t r = FWOG_IO_OK;
    uint8_t want[2];
    fwog_io_pack_fpga(cfg, want);

    /* 2. Write the FPGA. */
    if (!ops->fpga_write(ops->ctx, want)) {
        r = FWOG_IO_ERR_FPGA_WRITE;
        goto close;
    }

    /* 3. Read it back and compare. Everything after this point assumes the
          FPGA is routing what we asked for.
          Seeded with the COMPLEMENT of what we expect, never zero: a
          fpga_read() that returns true but leaves a byte unwritten must not
          be able to look like a match. Zero is a legitimate value of both
          bytes -- an all-input configuration packs to {0,0} -- so a
          zero-seeded buffer would silently pass the verify that is this
          whole function's reason to exist. */
    uint8_t got[2] = { (uint8_t)~want[0], (uint8_t)~want[1] };
    if (!ops->fpga_read(ops->ctx, got)) {
        r = FWOG_IO_ERR_FPGA_READ;
        goto close;
    }
    if (got[0] != want[0] || got[1] != want[1]) {
        r = FWOG_IO_ERR_FPGA_VERIFY;
        goto close;
    }

    /* 4. Our own pads. */
    ops->pads_apply(ops->ctx, cfg);

    /* 5. The expander's shifter directions and antennas. */
    if (!ops->expander_apply(ops->ctx, cfg)) r = FWOG_IO_ERR_EXPANDER;

close:
    /* 6. Always close the window, on every path. Leaving it open would let
          a later unrelated expander write disturb directions. */
    (void)ops->config_enable(ops->ctx, false);
    return r;
}
