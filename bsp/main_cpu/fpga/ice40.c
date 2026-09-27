#include "fpga/ice40.h"
#include "fpga/fpga_bitstream.h"
#include "platform/board.h"
#include "common/diag.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/time.h"

#define ICE40_CONFIG_HZ 5000000u

/* Dummy-clock burst lengths in BYTES (8 bits/byte at SPI_CPOL_0/CPHA_0), per
   FPGA-TN-02001-3.4 p.32: 8 clocks before CS asserts, ~104 trailing clocks
   after the bitstream, >=49 more to release the user I/O. Named so the
   _Static_assert on dummy[] below has something to check itself against
   instead of a bare 1/13/7. */
#define ICE40_PRE_CLOCK_BYTES      1u
#define ICE40_TRAILING_CLOCK_BYTES 13u
#define ICE40_RELEASE_CLOCK_BYTES  7u

void fwog_ice40_spi_claim(unsigned hz) {
    /* CS is plain SIO, not an SPI-block CS: configuration holds it low
       across the whole bitstream, which the block would not do. */
    gpio_init(PIN_IO_SPI_CS);
    gpio_set_dir(PIN_IO_SPI_CS, GPIO_OUT);
    gpio_put(PIN_IO_SPI_CS, 1);

    spi_init(FWOG_FPGA_SPI, hz);
    spi_set_format(FWOG_FPGA_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_IO_SPI_SCLK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_IO_SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_IO_SPI_MISO, GPIO_FUNC_SPI);
}

void fwog_ice40_spi_release(void) {
    /* Hand SCLK/MOSI/MISO back as plain inputs. The breakout header is a
       user bus; leaving them muxed to SPI would fight whatever the user
       wired.

       PIN_IO_SPI_CS is deliberately NOT released here -- this is a decision,
       not the asymmetric oversight it looks like. Unlike SCLK/MOSI/MISO,
       which the user's own wiring can legitimately want back, CS is shared
       with a permanently-attached on-board device (the FPGA), not something
       the user repurposes. Releasing it to a floating input would let noise
       on the breakout header -- or the user's own SPI traffic once
       SCLK/MOSI/MISO are released -- spuriously assert chip-select and
       desync the FPGA's SPI-slave state machine. Keeping CS actively driven
       high keeps the FPGA reliably deselected, its safe resting state, at
       the cost of that one header pin never being available to the user.
       That trade favours a stable on-board part over header pin count. */
    gpio_set_function(PIN_IO_SPI_SCLK, GPIO_FUNC_SIO);
    gpio_set_function(PIN_IO_SPI_MOSI, GPIO_FUNC_SIO);
    gpio_set_function(PIN_IO_SPI_MISO, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_IO_SPI_SCLK, GPIO_IN);
    gpio_set_dir(PIN_IO_SPI_MOSI, GPIO_IN);
    gpio_set_dir(PIN_IO_SPI_MISO, GPIO_IN);
}

bool fwog_ice40_load_default(void) {
    /* Sized for the LARGEST dummy-clock burst below (ICE40_TRAILING_CLOCK_
       BYTES = 13). Every byte here is actually sent on the wire, so this
       must stay zero-initialised and must never be shorter than any length
       passed to spi_write_blocking() below -- an under-sized buffer clocks
       uninitialised stack out of the breakout header's MOSI pin, which is
       exactly the bug this array used to have at 8 bytes against a 13-byte
       call. The _Static_assert makes a future burst that outgrows this
       silently impossible: it fails the build instead of failing on a
       scope. */
    uint8_t dummy[16] = { 0 };
    _Static_assert(sizeof(dummy) >= ICE40_PRE_CLOCK_BYTES &&
                   sizeof(dummy) >= ICE40_TRAILING_CLOCK_BYTES &&
                   sizeof(dummy) >= ICE40_RELEASE_CLOCK_BYTES,
                   "dummy[] must be at least as long as the largest "
                   "ICE40_*_CLOCK_BYTES burst");

    /* FWOG_FPGA_BITSTREAM_SIZE is a #define, checked at CMake configure time
     * against the .bin on disk (bsp/CMakeLists.txt) -- that catches a
     * regenerated bitstream whose size changed without the constant being
     * updated, but it says nothing about what actually got linked into THIS
     * .elf. fwog_fpga_bitstream_end exists for exactly this: a runtime check
     * that the linked symbol pair spans exactly FWOG_FPGA_BITSTREAM_SIZE
     * bytes, so a build-system mismatch (stale object, wrong .S, a linker
     * script surprise) is caught here rather than by clocking a truncated or
     * over-long stream at the FPGA, which configures nothing while looking
     * exactly like a wiring fault. */
    if ((size_t)(fwog_fpga_bitstream_end - fwog_fpga_bitstream) !=
        FWOG_FPGA_BITSTREAM_SIZE) {
        DIAG("[fpga] bitstream size mismatch: linked %u bytes, expected %u\n",
             (unsigned)(fwog_fpga_bitstream_end - fwog_fpga_bitstream),
             (unsigned)FWOG_FPGA_BITSTREAM_SIZE);
        return false;
    }

    fwog_ice40_spi_claim(ICE40_CONFIG_HZ);

    /* CS high, then CRESET_B low. */
    gpio_put(PIN_IO_SPI_CS, 1);
    gpio_put(PIN_FPGA_RESET, 0);

    /* CS LOW while CRESET_B is still LOW selects SPI slave mode. */
    gpio_put(PIN_IO_SPI_CS, 0);
    sleep_us(5);            /* deviation 1 -- see the header */

    gpio_put(PIN_FPGA_RESET, 1);
    sleep_us(1300);         /* housekeeping, >=1200 us per the datasheet */

    gpio_put(PIN_IO_SPI_CS, 1);
    spi_write_blocking(FWOG_FPGA_SPI, dummy, ICE40_PRE_CLOCK_BYTES);

    /* Stream the bitstream with CS low. */
    gpio_put(PIN_IO_SPI_CS, 0);
    spi_write_blocking(FWOG_FPGA_SPI, fwog_fpga_bitstream,
                       FWOG_FPGA_BITSTREAM_SIZE);
    gpio_put(PIN_IO_SPI_CS, 1);

    /* ~100 trailing clocks, then CDONE. */
    spi_write_blocking(FWOG_FPGA_SPI, dummy, ICE40_TRAILING_CLOCK_BYTES);
    bool done = gpio_get(PIN_FPGA_DONE);
    if (done) {
        /* >=49 more clocks to release the user I/O cleanly. */
        spi_write_blocking(FWOG_FPGA_SPI, dummy, ICE40_RELEASE_CLOCK_BYTES);
    }

    fwog_ice40_spi_release();
    DIAG("[fpga] config %s (cdone=%d, %u bytes)\n",
         done ? "ok" : "FAILED", (int)done,
         (unsigned)FWOG_FPGA_BITSTREAM_SIZE);
    return done;
}
