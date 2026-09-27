/* TI CC1101 sub-GHz transceiver -- two of them, sharing the main CPU's SPI0.
 *
 * Ported from `freewilimain/rmpLib/rpCC1101.{h,cpp}` (745 lines) and its
 * `rpCC1101Registers.h` (93 lines, transcribed directly below). The register
 * READ/WRITE/STROBE primitives that rpCC1101 actually calls do not live in
 * rpCC1101.cpp itself -- they are in a second file rpCC1101.cpp includes
 * transitively, `ELECHOUSE_CC1101_SRC_DRV.cpp` (a vendored, modified copy of
 * the "ELECHOUSE_CC1101" Arduino library, class `ELECHOUSE_CC1101`,
 * `obRadioLib` in rpCC1101). Both files are the ground truth this was ported
 * from. Datasheet citations below are to TI's CC1101 datasheet SWRS061I, page
 * numbers as printed in that PDF. The board schematic (rev 5, the
 * "RP2040 - CC1101s" block) was used to confirm the crystal frequency -- see
 * RADIO_CRYSTAL_HZ below.
 *
 * ---- Two radios, one bus: how they are actually selected ----
 * `bsp/main_cpu/platform/board.h` already names the pins:
 *   PIN_CC1101_MISO 4, PIN_CC1101_SCLK 6, PIN_CC1101_MOSI 7  (shared, spi0)
 *   PIN_CC1101_CS0 5, PIN_CC1101_CS1 18                      (per-radio)
 *   PIN_CC1101_GDO0_1 20, PIN_CC1101_GDO2_1 22                (per-radio)
 *   PIN_CC1101_GDO0_2 21, PIN_CC1101_GDO2_2 19                (per-radio)
 * The suffix ("_1"/"_2") is the only thing tying a GDO pair to a chip
 * select: `freewilimain/FreeWilliMain_pin_definitions.h` and
 * `targets/fwclassic/FreeWilliMain.cpp` construct exactly two bundles --
 * { RADIO_SPI_CS0, RADIO_GDO0_1, RADIO_GDO2_1 } and
 * { RADIO_SPI_CS1, RADIO_GDO0_2, RADIO_GDO2_2 } -- confirmed by
 * `rpGPIO obRadio2GDO0(RADIO_GDO0_1); rpGPIO obRadio2GDO2(RADIO_GDO2_1);`
 * paired with `rpGPIO obRadio2SPICS(RADIO_SPI_CS0);`, and the "_2" analogue
 * for CS1. This BSP names them CC1101_RADIO_CS0 / CC1101_RADIO_CS1 rather
 * than "radio 1" / "radio 2" on purpose -- see the trap below.
 *
 * ---- A naming trap in the reference, not repeated here ----
 * `FreeWilliMain.cpp` declares `rpCC1101 obRadio1; //radio 2 in schematics`
 * and `rpCC1101 obRadio2; //radio 1 in schematics` -- the C++ *variable*
 * names are the reverse of the *schematic's* radio numbering, and
 * `obRadio1` (the variable) is wired to the CS1/"_2" pin bundle while
 * `obRadio2` is wired to the CS0/"_1" bundle. Three different numbering
 * schemes (variable name, schematic silkscreen, pin-suffix) disagree with
 * each other in the reference. Rather than pick a fourth ordinal and add a
 * fourth disagreement, this port keys everything off the one fact that is
 * unambiguous in board.h: which chip select pin is asserted. Callers who
 * need to match the schematic's "radio 1"/"radio 2" labels must do that
 * translation themselves, deliberately, at the call site.
 *
 * ---- Chip-ready (MISO/SO) handshake: ported, and extended ----
 * Datasheet section 10, "4-wire Serial Configuration and Data Interface"
 * (p.29): "When CSn is pulled low, the MCU must wait until CC1101 SO pin
 * goes low before starting to transfer the header byte. This indicates that
 * the crystal is running." This is not conditional on which kind of SPI
 * access follows. `ELECHOUSE_CC1101::Reset()`, `SpiWriteReg()`,
 * `SpiWriteBurstReg()`, `SpiReadReg()`, `SpiReadBurstReg()` and
 * `SpiReadStatus()` all do this (`while(m_pSpiMISO->get());` after taking
 * CSn low) and are ported faithfully, bounded (see CC1101_READY_TIMEOUT_US).
 *
 * `ELECHOUSE_CC1101::SpiStrobe()` is the one exception in the reference --
 * it asserts CSn and clocks the strobe byte with no MISO wait at all. That
 * is a real gap against the datasheet text above, not a deliberate
 * optimization the reference documents anywhere. This port closes it:
 * cc1101_strobe() waits for chip-ready like every other primitive. This
 * makes every access path uniform, and is the only place this port adds a
 * wait the reference does not have -- everywhere else, waits are ported
 * as-is. In practice this rarely changes behaviour (the datasheet notes SO
 * "will always go low immediately after taking CSn low" unless the chip was
 * in SLEEP/XOFF, which is exactly when a strobe-only access would matter
 * most), but it removes a real, if narrow, false-ready window on wake.
 *
 * Every one of these waits is now BOUNDED (CC1101_READY_TIMEOUT_US), which
 * the reference's `while(m_pSpiMISO->get());` is not. The main CPU has a
 * watchdog and no other recovery path (the hardware record) -- an unbounded spin here
 * would trade "radio doesn't respond" for "whole board reboots blind", which
 * is a worse failure. `bsp/common/i2c_bus.h` bounds the same class of wait
 * for I2C for the identical reason and is the precedent this follows.
 *
 * ---- The SPI transfer itself is also bounded, on the reference's own
 * authority ----
 * `rpSPI::spiWriteAndReadBlocking()` (rpSPI.cpp) does NOT call the Pico
 * SDK's `spi_write_read_blocking()` directly. It has its own
 * `rpspi_write_read_bounded()`, with a comment explaining why: the SDK call
 * "spins forever if the peripheral never becomes readable/writable, so one
 * unresponsive device wedges the whole main loop with no watchdog and no
 * error -- observed on hardware as MAIN freezing mid-transfer to the CAN
 * controller". That is this port's own rule, already applied by the
 * reference to a different SPI peripheral on the same bus family. This port
 * carries the same bounded transfer loop (same FIFO-depth-aware shape, same
 * idea) rather than calling `spi_write_read_blocking()` directly.
 *
 * ---- Reset and power-up sequencing ----
 * `ELECHOUSE_CC1101::Reset()` is the datasheet's own manual reset procedure
 * (section 19.1.2, Figure 27): pull CSn low/high/low (>=40 us hold, the
 * reference uses a generous 1 ms), wait for SO low, strobe SRES, wait for SO
 * low again. Ported unchanged, now bounded. `Init()` = Reset() +
 * RegConfigSettings() (fixed register bank) + setCCMode(true); ported as
 * cc1101_bringup().
 *
 * ---- Two bugs found in the reference, fixed here (proven, not assumed) ----
 * 1. `rpCC1101::getLQI()`: `return (int)((lqi_reg >> 1) & 0x7F);` The LQI
 *    register (0x33, datasheet p.92, "0x33 (0xF3): LQI") is bit 7 = CRC_OK,
 *    bits 6:0 = LQI_EST[6:0] -- no shift. The `>>1` pulls CRC_OK into the
 *    top bit of the returned "LQI" value and drops LQI_EST's own bit 0.
 *    Fixed: cc1101_lqi_value() returns `lqi_reg & 0x7Fu`, no shift.
 * 2. `rpCC1101::receive_Packet()` declares `int iActualLength = 0;`, never
 *    assigns it, and `return 1;` unconditionally -- the documented "actual
 *    number of bytes read" is neither `iActualLength` nor the requested
 *    length, just the constant 1. Since `SpiReadBurstReg()` always clocks
 *    exactly the requested count (no short-read path exists), the only
 *    number that can be true is the requested length itself. Fixed:
 *    cc1101_receive_packet() returns the byte count it was asked to
 *    transfer.
 * 3. `rpCC1101::setFrequency()` writes the real FREQ2/FREQ1/FREQ0 registers
 *    directly and strobes SCAL -- it never calls
 *    `ELECHOUSE_CC1101::setMHZ()`. `ELECHOUSE::MHz` (the field `setPA()`'s
 *    band lookup and `Calibrate()`'s FSCTRL0/TEST0/FSCAL2 tuning both key
 *    off) is therefore only ever set to the class's compile-time default,
 *    433.92 MHz, by `Init()`'s one call to `setMHZ()` -- confirmed by grep,
 *    nothing in `FreeWilliMain.cpp`'s only call path
 *    (`updateRadioFromSettings()`) calls `setMHZ()` again after that.
 *    Net effect in the reference: retuning to any band other than
 *    387-464 MHz reprograms the real carrier correctly, but silently keeps
 *    the PA table AND the synthesizer calibration constants tuned for
 *    433.92 MHz. Fixed here: cc1101_set_frequency() updates `radio->mhz`
 *    and folds in the equivalent of `Calibrate()`'s per-band FSCTRL0/TEST0/
 *    FSCAL2/PA-table logic for the frequency actually being programmed --
 *    see cc1101.c.
 * 4. `rpCC1101::send_Packet()` polls GDO0 for "sync transmitted" and "end of
 *    packet" inside two 100 ms `rpTimer` windows, but each window's `while`
 *    loop condition is only the timer, not the GDO0 edge -- a timeout just
 *    falls through to the next stage, and the function `return true;`s
 *    unconditionally at the end regardless of whether either edge was ever
 *    actually observed. A caller cannot tell a completed transmission from
 *    one where the radio never toggled GDO0 at all (e.g. absent or
 *    wedged). Fixed here: cc1101_send_packet() tracks whether each edge was
 *    actually seen and returns false if either wait timed out first.
 * `cc1101_get_rssi()`'s dead branch is NOT one of these -- see cc1101.c.
 *
 * ---- Antenna path / PCAL6416 dependency (the hardware record) -- NOT in this
 * driver ----
 * `rpCC1101::setFrequency()` calls `updateAntennaPathFromFreq()`, which on a
 * band change invokes an `m_AntCircuitChange` callback wired in
 * `FreeWilliMain.cpp` to `UpdateIOAndAntennas()`. That function calls
 * `obGUI.setIODirections(..., radio1_ant_path, radio2_ant_path, ...)` --
 * `obGUI` is the main CPU's proxy to the *display* CPU over the inter-CPU
 * link, which is what actually writes the PCAL6416 I/O expander
 * (`freewiliclassicdisplay/rmpLib/fwIOExpand.cpp`) that steers the RF
 * front-end switches (see `the peripheral catalog`'s PCAL6416 note: port 1
 * bits V1_1/V1_2/V2_1/V2_2 are "antenna" selects, one pair per radio).
 *
 * That whole path -- the callback, the antenna-path index, and the write it
 * ultimately causes -- is cross-CPU application protocol and GUI plumbing,
 * not CC1101 register logic, so it is deliberately NOT ported here (see
 * "Not ported" below). **This means bring-up of an actual RF link on this
 * board needs a second piece this driver does not provide**: something that
 * tells the display CPU, over the link, to steer the PCAL6416 to the band
 * this driver just tuned to. `the hardware record` fact 31 already
 * records that the PCAL6416 answers on I2C and accepts configuration, but
 * that nobody has confirmed the bits actually switch anything -- this
 * driver is necessary but not sufficient to check that; it needs the
 * display-side write to exist and be exercised too.
 *
 * ---- Not ported (app-layer, dead, or a distinct feature) ----
 * - Antenna-path selection (`getAntennaPathIndex`, `updateAntennaPathFromFreq`,
 *   the `AntCircuitChange` callback) -- see above.
 * - `async_SetTxDMA`/`async_StartTx`/`async_StopTx`/`async_IsTxComplete`/
 *   `async_SetRxDMA`/`async_StartRx`/`async_StopRx`/`async_ReadFIFO`/
 *   `async_IsRxDMAComplete`: a distinct PIO+DMA engine that bit-bangs raw
 *   OOK/ASK pulse *durations* on GDO0 (a different job from packetized
 *   TX/RX -- more like this BSP's own `ir/ir_comm.c` PIO engine than like
 *   the register driver here). A candidate for its own follow-up port, the
 *   same way IR's PIO engine was scoped separately from its protocol code.
 * - GUI/console logging (`setLogIndex`, `obGUI.setLogDataText(...)` inside
 *   `send_Packet`/`receive_Packet`) -- console formatting, dropped like
 *   `lis3dh`'s `printAccel()`.
 * - 25 one-line debug register getters (`getSYNC0/1`, `getMCSM1`,
 *   `getPKTLEN`, `getPKTCTRL0/1`, `getMDMCFG0-4`, `getAGCCTRL0-2`,
 *   `getDEVIATN`, `getFREND0/1`, `getFSCAL0-3`, `getTEST0-2`, `getIOCFG0`)
 *   and the `setFSCAL1`/`setFSCAL2` debug setters: each is a direct,
 *   unmodified wrapper around `SpiReadReg`/`SpiWriteReg` with a named
 *   register constant already public in this header. `cc1101_read_reg()` /
 *   `cc1101_write_reg()` below cover every one of them with no loss of
 *   fidelity -- dropped as boilerplate, not as a feature cut.
 * - `ELECHOUSE_CC1101::SendData()` (both `char*` and `byte*` overloads,
 *   both with and without a `t` delay), `ReceiveData()`, `CheckReceiveFlag()`,
 *   `CheckCRC()`, `CheckRxFifo()`, `getMode()`, `setModul`/`addSpiPin`/
 *   `addGDO`/`addGDO0`/`setGDO`/`setGDO0`/`GDO_Set`/`GDO0_Set`, `setClb()`:
 *   confirmed by grep that none of these are ever called from `rpCC1101` or
 *   from `FreeWilliMain.cpp` -- `rpCC1101::send_Packet()`/`receive_Packet()`
 *   reimplement equivalent logic directly against the SPI primitives
 *   instead of calling `SendData()`/`ReceiveData()`. This is dead library
 *   surface in the actual product, not a cut of anything reachable.
 * - `rpCC1101::addDuration()` is declared in rpCC1101.h with no definition
 *   anywhere in the .cpp -- a dead prototype, not ported.
 * - The many small packet-format setters ARE ported (setCrc, setWhiteData,
 *   setPktFormat, setLengthConfig, setAddr, setAdrChk, setManchester,
 *   setSyncMode, setFEC, setPRE, setPQT, setCRC_AF, setAppendStatus,
 *   setDcFilterOff, setChannel, setChsp, setSyncWord, setPacketLength):
 *   every one of these is exercised by the one real caller,
 *   `updateRadioFromSettings()` in `FreeWilliMain.cpp`, so "configuration"
 *   in this port's scope includes them.
 *
 * ---- Host-testable pure logic (`#ifndef HOST_TEST`, see the .c file) ----
 * Frequency-word computation, data-rate/deviation/channel-spacing/RX-bandwidth
 * mantissa-exponent encoding, PATABLE band/level lookup, and status-byte /
 * RSSI / LQI decoding are all pure arithmetic with no SPI call inside them --
 * split out and host-tested in tests/test_cc1101.c, the same split
 * lis3dh.c/bq25896.c use.
 */
#ifndef FWOG_CC1101_H
#define FWOG_CC1101_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * Register map -- transcribed directly from rpCC1101Registers.h (93 lines).
 * ============================================================ */
#define CC1101_REG_IOCFG2      0x00u   /* GDO2 output pin configuration */
#define CC1101_REG_IOCFG1      0x01u   /* GDO1 output pin configuration */
#define CC1101_REG_IOCFG0      0x02u   /* GDO0 output pin configuration */
#define CC1101_REG_FIFOTHR     0x03u   /* RX FIFO and TX FIFO thresholds */
#define CC1101_REG_SYNC1       0x04u   /* Sync word, high byte */
#define CC1101_REG_SYNC0       0x05u   /* Sync word, low byte */
#define CC1101_REG_PKTLEN      0x06u   /* Packet length */
#define CC1101_REG_PKTCTRL1    0x07u   /* Packet automation control */
#define CC1101_REG_PKTCTRL0    0x08u   /* Packet automation control */
#define CC1101_REG_ADDR        0x09u   /* Device address */
#define CC1101_REG_CHANNR      0x0Au   /* Channel number */
#define CC1101_REG_FSCTRL1     0x0Bu   /* Frequency synthesizer control */
#define CC1101_REG_FSCTRL0     0x0Cu   /* Frequency synthesizer control */
#define CC1101_REG_FREQ2       0x0Du   /* Frequency control word, high byte */
#define CC1101_REG_FREQ1       0x0Eu   /* Frequency control word, middle byte */
#define CC1101_REG_FREQ0       0x0Fu   /* Frequency control word, low byte */
#define CC1101_REG_MDMCFG4     0x10u   /* Modem configuration */
#define CC1101_REG_MDMCFG3     0x11u   /* Modem configuration */
#define CC1101_REG_MDMCFG2     0x12u   /* Modem configuration */
#define CC1101_REG_MDMCFG1     0x13u   /* Modem configuration */
#define CC1101_REG_MDMCFG0     0x14u   /* Modem configuration */
#define CC1101_REG_DEVIATN     0x15u   /* Modem deviation setting */
#define CC1101_REG_MCSM2       0x16u   /* Main Radio Ctrl State Machine config */
#define CC1101_REG_MCSM1       0x17u   /* Main Radio Ctrl State Machine config */
#define CC1101_REG_MCSM0       0x18u   /* Main Radio Ctrl State Machine config */
#define CC1101_REG_FOCCFG      0x19u   /* Frequency Offset Compensation config */
#define CC1101_REG_BSCFG       0x1Au   /* Bit Synchronization config */
#define CC1101_REG_AGCCTRL2    0x1Bu   /* AGC control */
#define CC1101_REG_AGCCTRL1    0x1Cu   /* AGC control */
#define CC1101_REG_AGCCTRL0    0x1Du   /* AGC control */
#define CC1101_REG_WOREVT1     0x1Eu   /* High byte Event 0 timeout */
#define CC1101_REG_WOREVT0     0x1Fu   /* Low byte Event 0 timeout */
#define CC1101_REG_WORCTRL     0x20u   /* Wake On Radio control */
#define CC1101_REG_FREND1      0x21u   /* Front end RX configuration */
#define CC1101_REG_FREND0      0x22u   /* Front end TX configuration */
#define CC1101_REG_FSCAL3      0x23u   /* Frequency synthesizer calibration */
#define CC1101_REG_FSCAL2      0x24u   /* Frequency synthesizer calibration */
#define CC1101_REG_FSCAL1      0x25u   /* Frequency synthesizer calibration */
#define CC1101_REG_FSCAL0      0x26u   /* Frequency synthesizer calibration */
#define CC1101_REG_RCCTRL1     0x27u   /* RC oscillator configuration */
#define CC1101_REG_RCCTRL0     0x28u   /* RC oscillator configuration */
#define CC1101_REG_FSTEST      0x29u   /* Frequency synthesizer calib. control */
#define CC1101_REG_PTEST       0x2Au   /* Production test */
#define CC1101_REG_AGCTEST     0x2Bu   /* AGC test */
#define CC1101_REG_TEST2       0x2Cu   /* Various test settings */
#define CC1101_REG_TEST1       0x2Du   /* Various test settings */
#define CC1101_REG_TEST0       0x2Eu   /* Various test settings */

/* Strobe commands (single header byte, no data; addresses 0x30-0x3D with the
 * burst bit clear -- see CC1101_HDR_* below). */
#define CC1101_STROBE_SRES     0x30u   /* Reset chip */
#define CC1101_STROBE_SFSTXON  0x31u   /* Enable/calibrate freq synth */
#define CC1101_STROBE_SXOFF    0x32u   /* Turn off crystal oscillator */
#define CC1101_STROBE_SCAL     0x33u   /* Calibrate freq synth, turn off */
#define CC1101_STROBE_SRX      0x34u   /* Enable RX */
#define CC1101_STROBE_STX      0x35u   /* Enable TX */
#define CC1101_STROBE_SIDLE    0x36u   /* Exit RX/TX, turn off synth */
#define CC1101_STROBE_SAFC     0x37u   /* AFC adjustment */
#define CC1101_STROBE_SWOR     0x38u   /* Start WOR polling sequence */
#define CC1101_STROBE_SPWD     0x39u   /* Enter power down on CSn high */
#define CC1101_STROBE_SFRX     0x3Au   /* Flush RX FIFO */
#define CC1101_STROBE_SFTX     0x3Bu   /* Flush TX FIFO */
#define CC1101_STROBE_SWORRST  0x3Cu   /* Reset real time clock */
#define CC1101_STROBE_SNOP     0x3Du   /* No operation */

/* Status registers -- same addresses as the strobes above, disambiguated by
 * the burst bit (set = status register, clear = strobe). Read-only, single
 * access only (datasheet 10.2: "burst access is not available for status
 * registers"). */
#define CC1101_STATUS_PARTNUM    0x30u
#define CC1101_STATUS_VERSION    0x31u
#define CC1101_STATUS_FREQEST    0x32u
#define CC1101_STATUS_LQI        0x33u
#define CC1101_STATUS_RSSI       0x34u
#define CC1101_STATUS_MARCSTATE  0x35u
#define CC1101_STATUS_WORTIME1   0x36u
#define CC1101_STATUS_WORTIME0   0x37u
#define CC1101_STATUS_PKTSTATUS  0x38u
#define CC1101_STATUS_VCO_VC_DAC 0x39u
#define CC1101_STATUS_TXBYTES    0x3Au
#define CC1101_STATUS_RXBYTES    0x3Bu

#define CC1101_PATABLE 0x3Eu
#define CC1101_TXFIFO  0x3Fu
#define CC1101_RXFIFO  0x3Fu

/* SPI header-byte control bits (datasheet 10.2/10.5/10.6). ORed with a
 * register/FIFO address to form the header byte. */
#define CC1101_HDR_WRITE_BURST  0x40u
#define CC1101_HDR_READ_SINGLE  0x80u
#define CC1101_HDR_READ_BURST   0xC0u

/* Crystal on both CC1101 modules, confirmed on the schematic
 * (`FreeWili-Black-Blue rev5_FINAL.pdf`, "RP2040 - CC1101s" block, "26MHz"
 * silkscreen next to both XOSC_Q1/Q2 pairs) and matching
 * rpCC1101.cpp's RADIO_CRYSTAL_HZ. */
#define CC1101_CRYSTAL_HZ 26000000u

/* Valid operating bands, datasheet p.1/p.9 ("...frequencies in the
 * 300-348 MHz, 387-464 MHz and 779-928 MHz bands") and rpCC1101.cpp's
 * RADIO_LOW/MID/HIGH_BAND_MIN/MAX. Note ELECHOUSE_CC1101_SRC_DRV.cpp's
 * Calibrate() uses a DIFFERENT low threshold for the middle band (378 MHz,
 * not 387) -- that is an internal calibration-constant selection point
 * inside an already-validated band, not a second definition of the
 * operating range, and both are transcribed unchanged where each appears. */
#define CC1101_BAND_LOW_MIN_HZ   300000000u
#define CC1101_BAND_LOW_MAX_HZ   348000000u
#define CC1101_BAND_MID_MIN_HZ   387000000u
#define CC1101_BAND_MID_MAX_HZ   464000000u
#define CC1101_BAND_HIGH_MIN_HZ  779000000u
#define CC1101_BAND_HIGH_MAX_HZ  928000000u

/* rpCC1101.cpp's RADIO_CHANGE_STATE_TIMEOUT -- bound for waitForStateChange()
 * (already a bounded, timer-based loop in the reference; ported as-is). */
#define CC1101_STATE_CHANGE_TIMEOUT_MS 50u

/* Bound for the chip-ready (MISO/SO low) spin the reference leaves
 * unbounded -- see the header comment above. Generous relative to the
 * datasheet's worst case (150 us crystal start-up from power-down, Table
 * 22); this is for a wedged/absent chip, not normal operation. */
#define CC1101_READY_TIMEOUT_US 10000u

/* Modulation formats -- MDMCFG2.MOD_FORMAT, datasheet p.77. Matches
 * ELECHOUSE_CC1101::setModulation()'s `byte m` parameter (0..4) exactly. */
typedef enum {
    CC1101_MOD_2FSK = 0,
    CC1101_MOD_GFSK = 1,
    CC1101_MOD_ASK  = 2,
    CC1101_MOD_4FSK = 3,
    CC1101_MOD_MSK  = 4,
} cc1101_modulation_t;

/* Which of the two on-board radios: see the header comment above for why
 * this is keyed off chip-select pin rather than an ordinal "radio 1/2". */
typedef enum {
    CC1101_RADIO_CS0 = 0,
    CC1101_RADIO_CS1 = 1,
} cc1101_radio_t;

/* Chip status byte's STATE[2:0] field, Table 23 -- also
 * ELECHOUSE_CC1101_SRC_DRV.h's `MainStateMachineMode`. */
typedef enum {
    CC1101_STATE_IDLE = 0,
    CC1101_STATE_RX = 1,
    CC1101_STATE_TX = 2,
    CC1101_STATE_FSTXON = 3,
    CC1101_STATE_CALIBRATE = 4,
    CC1101_STATE_SETTLING = 5,
    CC1101_STATE_RXFIFO_OVERFLOW = 6,
    CC1101_STATE_TXFIFO_UNDERFLOW = 7,
} cc1101_radio_state_t;

/* Per-instance state. Two of these exist (one per physical chip); nothing
 * in this driver is a hidden singleton -- see AGENTS.md's instruction to
 * "design the API so a caller names which radio it means". `pa_dbm`,
 * `pa_band`, `mhz` and `ask_ook` are the only fields genuinely carried
 * across calls (see cc1101.c's "why so little persistent state" note);
 * everything else ELECHOUSE cached in class members is instead re-read
 * fresh from the chip at each call site that needs it, matching the
 * reference's own Split_MDMCFG1/2/4, Split_PKTCTRL0/1 behaviour (they
 * already re-read hardware, not a stale shadow). */
typedef struct {
    uint8_t cs_pin;
    uint8_t gdo0_pin;
    uint8_t gdo2_pin;
    int pa_dbm;      /* last requested output power, dBm; ELECHOUSE's `pa` */
    int pa_band;     /* 0=none yet, 1=315/2=433/3=868/4=915 MHz PA table last
                       * applied; ELECHOUSE's `last_pa` */
    float mhz;        /* current carrier, MHz; ELECHOUSE's `MHz`, kept in
                       * sync with the real programmed carrier -- see bug 3
                       * above */
    bool ask_ook;     /* true when the current modulation is ASK/OOK, so
                       * cc1101_set_power() places the PATABLE entry at the
                       * index ELECHOUSE::setPA() would (index 1, not 0) */
} cc1101_t;

/* ============================================================
 * Pure / host-testable (tests/test_cc1101.c)
 * ============================================================ */

/* Chip status byte (datasheet 10.1, Table 23), decoded from masks rather
 * than the reference's `cc1101_chip_status_t` bitfield-over-a-raw-byte cast
 * (ELECHOUSE_CC1101_SRC_DRV.h): C bitfield packing order is
 * implementation-defined, so reinterpreting a wire byte through a bitfield
 * struct is not portable even though it happens to work on the reference's
 * one compiler. Same bits, same meaning, extracted with shifts/masks
 * instead -- not a behaviour change. `ok` is not part of the wire byte: it
 * is false when the transaction that produced this status timed out
 * waiting for chip-ready (see cc1101_strobe()). */
typedef struct {
    bool chip_rdy;       /* CHIP_RDYn bit, ACTIVE LOW: true means NOT ready */
    uint8_t state;        /* STATE[2:0], Table 23 -- 0=IDLE .. 7=TXFIFO_UNDERFLOW */
    uint8_t fifo_bytes;    /* FIFO_BYTES_AVAILABLE[3:0], 0-15 (15 means >=15) */
    bool ok;              /* false if the SPI transaction itself timed out */
} cc1101_status_t;

cc1101_status_t cc1101_decode_status(uint8_t raw);

/* True if freq_hz falls in one of the three legal operating bands (see
 * CC1101_BAND_* above). Ported from rpCC1101::validFrequency(). */
bool cc1101_freq_in_band(uint32_t freq_hz);

/* Carrier frequency -> FREQ2/FREQ1/FREQ0. Datasheet 0x0D/0x0E/0x0F register
 * description: fcarrier = (fXOSC/2^16) * FREQ[23:0]; solved for FREQ:
 * FREQ = freq_hz * 65536 / xtal_hz. Ported from rpCC1101::setFrequency()
 * (`(uint64_t)iFreq * 65536 / RADIO_CRYSTAL_HZ`), parameterized on xtal_hz
 * rather than hardcoding CC1101_CRYSTAL_HZ so the pure math is testable
 * independent of this board's crystal. */
void cc1101_freq_word(uint32_t freq_hz, uint32_t xtal_hz,
                       uint8_t *freq2, uint8_t *freq1, uint8_t *freq0);

/* RSSI status-register byte -> dBm. Datasheet 17.3: RSSI_dec>=128 (as an
 * UNSIGNED byte) means RSSI_dBm=(RSSI_dec-256)/2-offset, else RSSI_dec/2-
 * offset. ELECHOUSE_CC1101::getRssi() stores the raw byte into an int8_t
 * BEFORE that comparison (`int8_t raw_rssi = SpiReadStatus(...)`), which
 * sign-extends exactly the RSSI_dec-256 case automatically -- so its
 * `if (raw_rssi >= 128)` branch is UNREACHABLE (int8_t's range tops out at
 * 127) and only the `else` branch, `raw_rssi/2 - 74`, ever executes. That
 * surviving branch already computes the correct dBm value for every byte
 * (verified: 0x00 -> 0/2-74=-74; 0x80(-128) -> -128/2-74=-138, matching the
 * datasheet's (128-256)/2-74=-138 for RSSI_dec=128). Not a bug: a dead
 * branch, ported here as the single expression that actually runs. */
int16_t cc1101_rssi_dbm(int8_t raw_reg);

/* LQI status-register byte -> (LQI estimate, CRC-OK flag). Datasheet 0x33
 * register map: bit 7 = CRC_OK, bits 6:0 = LQI_EST[6:0] -- no shift needed
 * for the LQI value. See the "bugs found" note above: the reference's
 * `getLQI()` does `(lqi_reg >> 1) & 0x7F`, which is wrong (folds CRC_OK
 * into the result and drops LQI_EST bit 0). Fixed here. */
uint8_t cc1101_lqi_value(uint8_t lqi_reg);
bool cc1101_lqi_crc_ok(uint8_t lqi_reg);

/* Read-modify-write a register field by mask, without a live SPI read --
 * pure so the merge arithmetic itself is testable. The hardware-bound
 * setters below call this after an actual SpiReadReg. */
uint8_t cc1101_reg_merge(uint8_t old_value, uint8_t mask, uint8_t new_bits);

/* Data rate (kBaud) -> MDMCFG4.DRATE_E (return) and MDMCFG3.DRATE_M
 * (*drate_m out). Datasheet 0x11 register description gives the closed-form
 * RDATA = (256+DRATE_M) * 2^DRATE_E * fXOSC / 2^28; ELECHOUSE::setDRate()
 * instead SEARCHES for DRATE_E by repeated halving with 26 MHz-crystal-
 * specific literal constants (0.0247955, 0.00009685, 1621.83, 0.0494942--
 * all derived from that same closed form at fXOSC=26 MHz). Ported as the
 * same search, same constants, same 20-iteration bound -- not
 * xtal-parameterized, because the reference is not either, and this board's
 * crystal is confirmed 26 MHz (see CC1101_CRYSTAL_HZ). Clamped to the same
 * [0.0247955, 1621.83] kBaud range as the reference.
 *
 * NOTE (found, not silently absorbed): at the exact top of that range,
 * 1621.83 kBaud, the search returns 16 -- one past DRATE_E's 4-bit range
 * (0-15). ELECHOUSE::setDRate() writes it with `SpiWriteReg(16,
 * m4RxBw+m4DaRa)` -- plain ADDITION, not a masked merge -- so that 16 can
 * carry out of the DRATE_E nibble into the adjacent CHANBW_M bits of the
 * SAME register (MDMCFG4), silently perturbing RX bandwidth for anyone who
 * requests the maximum data rate. Almost certainly unintentional, not a
 * documented feature. This port keeps cc1101_drate_encode() faithful (it
 * returns 16 here too, same search, same constants) but
 * cc1101_set_data_rate() ORs the result in by mask rather than adding it,
 * so the same extreme input masks the overflow bit away (DRATE_E reads
 * back 0) instead of corrupting RX bandwidth -- a safer, bounded failure
 * mode for an input this far into the corner, not a hidden behavior
 * change. tests/test_cc1101.c exercises this exact boundary. */
uint8_t cc1101_drate_encode(float kbaud, uint8_t *drate_m);

/* Frequency deviation (kHz) -> DEVIATN register byte. Ported from
 * ELECHOUSE::setDeviation()'s search loop, same constants, same 255-count
 * bound. Clamped to the same [1.586914, 380.859375] kHz range.
 *
 * NOTE (found, not silently absorbed -- same class as cc1101_drate_encode()'s
 * NOTE above): the low clamp returns 1, not 0 (the loop body's trailing
 * `c++` always runs, even on the iteration the `f>=d` branch fires -- there
 * is no early exit in the reference). The high clamp returns 0x80, one past
 * DEVIATN's valid 0x00-0x77 range (DEVIATION_E/DEVIATION_M are each 3 bits;
 * 0x80 sets bit 7, datasheet-reserved "Not used"). This function stays
 * faithful to the search (returns 0x80 here too); cc1101_set_deviation()
 * masks bit 7 off before writing. tests/test_cc1101.c pins both clamp
 * values exactly. */
uint8_t cc1101_deviation_encode(float khz);

/* RX channel filter bandwidth (kHz) -> MDMCFG4.CHANBW_E/CHANBW_M bits
 * (7:4), pre-shifted into byte position so the caller can OR it directly
 * with a DRATE_E nibble. Ported from ELECHOUSE::setRxBW(). */
uint8_t cc1101_rxbw_encode(float khz);

/* Channel spacing (kHz) -> MDMCFG1.CHANSPC_E[1:0] (return) and
 * MDMCFG0.CHANSPC_M[7:0] (*chanspc_m out). Ported from
 * ELECHOUSE::setChsp(). */
uint8_t cc1101_chsp_encode(float khz, uint8_t *chanspc_m);

/* PATABLE output-power lookup. Ported from ELECHOUSE::setPA(): which of the
 * four band-specific 8/10-entry tables applies is selected by `mhz` (the
 * CURRENTLY TUNED carrier, not the target power) exactly as the reference
 * does, then `dbm` is thresholded against that table's published dBm
 * breakpoints. Returns the single PATABLE byte and reports which band (1=
 * 315,2=433,3=868,4=915 MHz, matching ELECHOUSE's `last_pa`; 0 if `mhz` is
 * outside all four ranges, in which case the returned byte is 0 and the
 * caller should not write PATABLE) via *out_band. */
uint8_t cc1101_patable_lookup(float mhz, int dbm, int *out_band);

/* Places `entry` into an 8-byte PATABLE image at the slot ASK/OOK
 * modulation needs (index 0 for a transmitted '0', index `entry`'s own
 * value... no -- see cc1101.c) or at index 0 for every other modulation.
 * Ported from ELECHOUSE::setPA()'s `if(modulation==2){PATABLE[0]=0;
 * PATABLE[1]=a;}else{PATABLE[0]=a;PATABLE[1]=0;}` tail. `out[8]` is zeroed
 * first. */
void cc1101_patable_build(uint8_t entry, bool ask_ook, uint8_t out[8]);

/* Integer linear interpolation, transcribed from the reference's bare
 * global `map(x,in_min,in_max,out_min,out_max)` (ELECHOUSE_CC1101_SRC_DRV.cpp
 * :23-26), just namespaced -- the original's unqualified `map` is a
 * generic-enough name to risk colliding with something else in a C
 * translation unit; the arithmetic is unchanged. */
int cc1101_map(int x, int in_min, int in_max, int out_min, int out_max);

/* Ported from ELECHOUSE_CC1101::Calibrate()'s band dispatch: for `mhz` in
 * one of the four PA-table bands (see cc1101_patable_lookup), returns the
 * FSCTRL0 register value (`cc1101_map()` against that band's calibration
 * offsets, clb1..clb4 in the reference, folded in as the fixed default
 * values the reference itself never overrides via setClb()) and the TEST0
 * register value (0x0B below the band's crystal-startup threshold, 0x09
 * at or above it -- see the datasheet's power-on/wake timing notes,
 * section 19.1). `out_band` matches cc1101_patable_lookup's numbering; 0
 * if `mhz` is outside all four bands, in which case neither output should
 * be written by the caller. */
void cc1101_calib_lookup(float mhz, uint8_t *fsctrl0, uint8_t *test0, int *out_band);

#ifndef HOST_TEST
/* ============================================================
 * Hardware-bound. Every wait is bounded (CC1101_READY_TIMEOUT_US,
 * CC1101_STATE_CHANGE_TIMEOUT_MS, or the packet-TX 100 ms window ported
 * from rpCC1101::send_Packet()) -- a wedged radio must not hang the main
 * CPU's watchdog-protected loop.
 * ============================================================ */

/* One-time SPI0 bring-up for BOTH radios (they share MOSI/MISO/SCLK).
 * `baud_hz` is passed straight to spi_init(); the reference's
 * `obSpiRadio.init(1'000'000, 8, 0, 0)` -- 1 MHz, mode 0 -- comfortably
 * inside the datasheet's 10 MHz (single access) / 6.5 MHz (burst) ceiling
 * (Table 22), so 1 MHz is a deliberately conservative reference choice,
 * preserved rather than "optimized". Call once, before cc1101_bind(). */
void cc1101_bus_init(uint32_t baud_hz);

/* Binds `radio`'s pins for `which` (CC1101_RADIO_CS0 or _CS1, from
 * board.h). Configures GDO0/GDO2 as inputs with a pull-up (ELECHOUSE's
 * `obRadioXGDOx.init(false,false,rpGPIOPullResistor::up)`). Does NOT touch
 * the chip-select pin's direction: board_init_pins() already parks BOTH
 * CC1101 chip selects HIGH as a whole-CPU invariant (the hardware record, "before
 * any main-CPU SPI traffic") -- board_init() (or at least board_init_pins())
 * must have already run. */
void cc1101_bind(cc1101_t *radio, cc1101_radio_t which);

/* Datasheet manual reset (section 19.1.2 / Figure 27), ELECHOUSE::Reset().
 * Returns false if either chip-ready wait timed out -- the chip did not
 * respond. */
bool cc1101_reset(cc1101_t *radio);

/* ELECHOUSE::Init(): Reset() + RegConfigSettings() (fixed register bank,
 * transcribed verbatim) + setCCMode(true) (packet mode -- the only mode
 * this product ever uses; ccmode=false is never reached by
 * FreeWilliMain.cpp and not exposed here). See cc1101.c for why
 * MDMCFG4's initial write can safely hardcode the RX-bandwidth field as 0
 * rather than re-reading hardware. Returns false on a reset failure. */
bool cc1101_bringup(cc1101_t *radio);

/* ELECHOUSE::getCC1101() / rpCC1101::commCheck(): PARTNUM (0x30) must read
 * 0 and VERSION (0x31) must read 0x14 for this to be a CC1101. */
bool cc1101_probe(cc1101_t *radio);

/* ---- SPI primitives -- exposed so a caller can reach any register this
 * driver does not name a dedicated setter for (see "not ported": the 25
 * debug getters). Every one waits for chip-ready first (bounded) and
 * returns false on timeout without touching *out. ---- */
bool cc1101_read_reg(cc1101_t *radio, uint8_t addr, uint8_t *out);
bool cc1101_read_burst(cc1101_t *radio, uint8_t addr, uint8_t *out, uint8_t n);
bool cc1101_write_reg(cc1101_t *radio, uint8_t addr, uint8_t value);
bool cc1101_write_burst(cc1101_t *radio, uint8_t addr, const uint8_t *data, uint8_t n);
/* Status registers (0x30-0x3D, burst bit set, single access only --
 * datasheet 10.2). ELECHOUSE::SpiReadStatus(). */
bool cc1101_read_status(cc1101_t *radio, uint8_t addr, uint8_t *out);
/* Strobe command. See the header comment above: this is the one primitive
 * that ADDS a chip-ready wait relative to the reference's SpiStrobe(). */
cc1101_status_t cc1101_strobe(cc1101_t *radio, uint8_t strobe);

/* ---- Configuration. Each is a thin wrapper: compute (pure fn above) or
 * pick a literal, then write. ---- */

/* rpCC1101::setFrequency(), PLUS ELECHOUSE::Calibrate()'s band-specific
 * FSCTRL0/TEST0/FSCAL2/PA-table tuning folded in for the frequency actually
 * being programmed -- see "bug 3" above for why the reference does not do
 * this itself. Rejects freq_hz outside the three legal bands
 * (cc1101_freq_in_band()) without touching any register. Strobes SCAL and
 * waits (bounded, CC1101_STATE_CHANGE_TIMEOUT_MS) for the CALIBRATE state,
 * exactly like the reference. */
bool cc1101_set_frequency(cc1101_t *radio, uint32_t freq_hz);
bool cc1101_set_modulation(cc1101_t *radio, cc1101_modulation_t mod);
bool cc1101_set_data_rate(cc1101_t *radio, float kbaud);
bool cc1101_set_deviation(cc1101_t *radio, float khz);
bool cc1101_set_rx_bandwidth(cc1101_t *radio, float khz);
bool cc1101_set_channel(cc1101_t *radio, uint8_t channel);
bool cc1101_set_channel_spacing(cc1101_t *radio, float khz);
bool cc1101_set_sync_word(cc1101_t *radio, uint8_t hi, uint8_t lo);
bool cc1101_set_power(cc1101_t *radio, int dbm);
bool cc1101_set_packet_length(cc1101_t *radio, uint8_t len);
bool cc1101_set_length_config(cc1101_t *radio, uint8_t mode);
bool cc1101_set_crc(cc1101_t *radio, bool enable);
bool cc1101_set_white_data(cc1101_t *radio, bool enable);
bool cc1101_set_pkt_format(cc1101_t *radio, uint8_t format);
bool cc1101_set_addr(cc1101_t *radio, uint8_t addr);
bool cc1101_set_addr_check(cc1101_t *radio, uint8_t mode);
bool cc1101_set_manchester(cc1101_t *radio, bool enable);
bool cc1101_set_sync_mode(cc1101_t *radio, uint8_t mode);
bool cc1101_set_fec(cc1101_t *radio, bool enable);
bool cc1101_set_preamble(cc1101_t *radio, uint8_t setting);
bool cc1101_set_pqt(cc1101_t *radio, uint8_t threshold);
bool cc1101_set_crc_autoflush(cc1101_t *radio, bool enable);
bool cc1101_set_append_status(cc1101_t *radio, bool enable);
bool cc1101_set_dc_filter_off(cc1101_t *radio, bool enable);

/* ---- State machine. waitForStateChange() is already a bounded, timer-
 * based loop in the reference (RADIO_CHANGE_STATE_TIMEOUT=50 ms) -- ported
 * as-is, not newly bounded. ---- */
bool cc1101_idle(cc1101_t *radio);
bool cc1101_tx(cc1101_t *radio);
/* Also strobes SAFC (AFC adjustment) after the state change, matching
 * rpCC1101::switchRx(). */
bool cc1101_rx(cc1101_t *radio);
bool cc1101_flush_rx(cc1101_t *radio);
/* rpCC1101::sleep(): switchIdle() then ELECHOUSE::goSleep() (SIDLE, SPWD). */
void cc1101_sleep(cc1101_t *radio);

/* ---- Packet TX/RX. rpCC1101::send_Packet()/receive_Packet()/
 * receive_packet_bytes_available(). ---- */
bool cc1101_send_packet(cc1101_t *radio, const uint8_t *data, uint8_t len);
/* Returns the number of bytes actually clocked into `data` (== `len`; see
 * the "bugs found" note above for why this differs from the reference's
 * literal `return 1`), or -1 if the burst read itself timed out. */
int cc1101_receive_packet(cc1101_t *radio, uint8_t *data, uint8_t len);
/* Raw RXBYTES status byte (bit 7 = RXFIFO_OVERFLOW flag, bits 6:0 = byte
 * count) -- ported unmasked, exactly like
 * rpCC1101::receive_packet_bytes_available(). Returns -1 on a timeout. */
int cc1101_rx_bytes_available(cc1101_t *radio);

/* ---- Status. ---- */
int16_t cc1101_get_rssi(cc1101_t *radio);
uint8_t cc1101_get_lqi(cc1101_t *radio, bool *crc_ok);
bool cc1101_get_gdo0(cc1101_t *radio);

#endif /* HOST_TEST */

#endif /* FWOG_CC1101_H */
