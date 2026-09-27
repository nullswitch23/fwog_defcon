#include "radio/cc1101.h"

/* ---- Pure: see the header for citations behind each of these. ---- */

cc1101_status_t cc1101_decode_status(uint8_t raw) {
    cc1101_status_t st;
    st.chip_rdy = (raw & 0x80u) != 0u;
    st.state = (uint8_t)((raw >> 4) & 0x07u);
    st.fifo_bytes = (uint8_t)(raw & 0x0Fu);
    st.ok = true;
    return st;
}

bool cc1101_freq_in_band(uint32_t freq_hz) {
    return (freq_hz >= CC1101_BAND_LOW_MIN_HZ && freq_hz <= CC1101_BAND_LOW_MAX_HZ) ||
           (freq_hz >= CC1101_BAND_MID_MIN_HZ && freq_hz <= CC1101_BAND_MID_MAX_HZ) ||
           (freq_hz >= CC1101_BAND_HIGH_MIN_HZ && freq_hz <= CC1101_BAND_HIGH_MAX_HZ);
}

void cc1101_freq_word(uint32_t freq_hz, uint32_t xtal_hz,
                       uint8_t *freq2, uint8_t *freq1, uint8_t *freq0) {
    /* rpCC1101.cpp:131 -- `(uint64_t)(iFreq) * 65536 / RADIO_CRYSTAL_HZ`. */
    uint64_t word = ((uint64_t)freq_hz * 65536ull) / (uint64_t)xtal_hz;
    if (freq2) *freq2 = (uint8_t)((word >> 16) & 0xFFu);
    if (freq1) *freq1 = (uint8_t)((word >> 8) & 0xFFu);
    if (freq0) *freq0 = (uint8_t)(word & 0xFFu);
}

int16_t cc1101_rssi_dbm(int8_t raw_reg) {
    /* See the header: only this expression ever executes in the reference,
     * for every possible byte value, and it already matches the datasheet
     * formula because raw_reg's own sign extension performs the "-256"
     * adjustment. */
    return (int16_t)((int16_t)raw_reg / 2) - 74;
}

uint8_t cc1101_lqi_value(uint8_t lqi_reg) {
    return (uint8_t)(lqi_reg & 0x7Fu);
}

bool cc1101_lqi_crc_ok(uint8_t lqi_reg) {
    return (lqi_reg & 0x80u) != 0u;
}

uint8_t cc1101_reg_merge(uint8_t old_value, uint8_t mask, uint8_t new_bits) {
    return (uint8_t)((old_value & (uint8_t)~mask) | (new_bits & mask));
}

uint8_t cc1101_drate_encode(float kbaud, uint8_t *drate_m) {
    /* Ported from ELECHOUSE_CC1101::setDRate() -- see the header for why
     * the magic constants (26 MHz-crystal-specific) are preserved as-is. */
    float c = kbaud;
    uint8_t drate_e = 0;
    uint8_t m = 0;
    if (c > 1621.83f) c = 1621.83f;
    if (c < 0.0247955f) c = 0.0247955f;
    for (int i = 0; i < 20; i++) {
        if (c <= 0.0494942f) {
            c = c - 0.0247955f;
            c = c / 0.00009685f;
            m = (uint8_t)c;
            float s1 = (c - (float)m) * 10.0f;
            if (s1 >= 5.0f) m++;
            break;
        }
        drate_e++;
        c = c / 2.0f;
    }
    if (drate_m) *drate_m = m;
    return drate_e;
}

uint8_t cc1101_deviation_encode(float khz) {
    /* Ported from ELECHOUSE_CC1101::setDeviation(). */
    float f = 1.586914f, v = 0.19836425f;
    int c = 0;
    if (khz > 380.859375f) khz = 380.859375f;
    if (khz < 1.586914f) khz = 1.586914f;
    for (int i = 0; i < 255; i++) {
        f += v;
        if (c == 7) { v *= 2.0f; c = -1; i += 8; }
        if (f >= khz) { c = i; i = 255; }
        c++;
    }
    return (uint8_t)c;
}

uint8_t cc1101_rxbw_encode(float khz) {
    /* Ported from ELECHOUSE_CC1101::setRxBW(). Returns CHANBW_E/CHANBW_M
     * already positioned at bits 7:4 (MDMCFG4), same as ELECHOUSE's
     * `m4RxBw`. */
    int s1 = 3, s2 = 3;
    for (int i = 0; i < 3; i++) {
        if (khz > 101.5625f) { khz /= 2.0f; s1--; } else { i = 3; }
    }
    for (int i = 0; i < 3; i++) {
        if (khz > 58.1f) { khz /= 1.25f; s2--; } else { i = 3; }
    }
    s1 *= 64;
    s2 *= 16;
    return (uint8_t)(s1 + s2);
}

uint8_t cc1101_chsp_encode(float khz, uint8_t *chanspc_m) {
    /* Ported from ELECHOUSE_CC1101::setChsp(). Returns CHANSPC_E (bits 1:0
     * of MDMCFG1); *chanspc_m is the full MDMCFG0 byte. */
    uint8_t chanspc_e = 0;
    uint8_t m = 0;
    if (khz > 405.456543f) khz = 405.456543f;
    if (khz < 25.390625f) khz = 25.390625f;
    for (int i = 0; i < 5; i++) {
        if (khz <= 50.682068f) {
            khz -= 25.390625f;
            khz /= 0.0991825f;
            m = (uint8_t)khz;
            float s1 = (khz - (float)m) * 10.0f;
            if (s1 >= 5.0f) m++;
            i = 5;
        } else {
            chanspc_e++;
            khz /= 2.0f;
        }
    }
    if (chanspc_m) *chanspc_m = m;
    return chanspc_e;
}

uint8_t cc1101_patable_lookup(float mhz, int dbm, int *out_band) {
    /* Ported from ELECHOUSE_CC1101::setPA()'s band dispatch and the four
     * PA_TABLE_* threshold ladders. Deliberately checks 378 MHz (not
     * rpCC1101::validFrequency()'s 387 MHz) for the middle band -- see the
     * CC1101_BAND_MID_MIN_HZ comment in the header: this is what
     * ELECHOUSE::setPA() itself checks, and this function has no way to
     * know whether some other gate already ran. */
    int a = 0, band = 0;
    if (mhz >= 300.0f && mhz <= 348.0f) {
        band = 1;
        if (dbm <= -30)      a = 0x12;
        else if (dbm <= -20) a = 0x0D;
        else if (dbm <= -15) a = 0x1C;
        else if (dbm <= -10) a = 0x34;
        else if (dbm <= 0)   a = 0x51;
        else if (dbm <= 5)   a = 0x85;
        else if (dbm <= 7)   a = 0xCB;
        else                 a = 0xC2;
    } else if (mhz >= 378.0f && mhz <= 464.0f) {
        band = 2;
        if (dbm <= -30)      a = 0x12;
        else if (dbm <= -20) a = 0x0E;
        else if (dbm <= -15) a = 0x1D;
        else if (dbm <= -10) a = 0x34;
        else if (dbm <= 0)   a = 0x60;
        else if (dbm <= 5)   a = 0x84;
        else if (dbm <= 7)   a = 0xC8;
        else                 a = 0xC0;
    } else if (mhz >= 779.0f && mhz <= 899.99f) {
        band = 3;
        if (dbm <= -30)      a = 0x03;
        else if (dbm <= -20) a = 0x17;
        else if (dbm <= -15) a = 0x1D;
        else if (dbm <= -10) a = 0x26;
        else if (dbm <= -6)  a = 0x37;
        else if (dbm <= 0)   a = 0x50;
        else if (dbm <= 5)   a = 0x86;
        else if (dbm <= 7)   a = 0xCD;
        else if (dbm <= 10)  a = 0xC5;
        else                 a = 0xC0;
    } else if (mhz >= 900.0f && mhz <= 928.0f) {
        band = 4;
        if (dbm <= -30)      a = 0x03;
        else if (dbm <= -20) a = 0x0E;
        else if (dbm <= -15) a = 0x1E;
        else if (dbm <= -10) a = 0x27;
        else if (dbm <= -6)  a = 0x38;
        else if (dbm <= 0)   a = 0x8E;
        else if (dbm <= 5)   a = 0x84;
        else if (dbm <= 7)   a = 0xCC;
        else if (dbm <= 10)  a = 0xC3;
        else                 a = 0xC0;
    }
    if (out_band) *out_band = band;
    return (uint8_t)a;
}

void cc1101_patable_build(uint8_t entry, bool ask_ook, uint8_t out[8]) {
    for (int i = 0; i < 8; i++) out[i] = 0;
    /* ELECHOUSE::setPA() tail: ASK/OOK uses PATABLE[0] for a transmitted
     * '0' (0, i.e. off) and PATABLE[1] for a transmitted '1' (the looked-up
     * power level); every other modulation uses PATABLE[0] for its one
     * active level. */
    if (ask_ook) {
        out[1] = entry;
    } else {
        out[0] = entry;
    }
}

int cc1101_map(int x, int in_min, int in_max, int out_min, int out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void cc1101_calib_lookup(float mhz, uint8_t *fsctrl0, uint8_t *test0, int *out_band) {
    /* Ported from ELECHOUSE_CC1101::Calibrate(). clb1..clb4 in the
     * reference are the calibration-offset pairs {24,28}/{31,38}/{65,76}/
     * {77,79} -- inlined here since nothing in FreeWilliMain.cpp ever calls
     * setClb() to change them. */
    int band = 0;
    uint8_t f0 = 0, t0 = 0x0Bu;
    if (mhz >= 300.0f && mhz <= 348.0f) {
        band = 1;
        f0 = (uint8_t)cc1101_map((int)mhz, 300, 348, 24, 28);
        t0 = (mhz < 322.88f) ? 0x0Bu : 0x09u;
    } else if (mhz >= 378.0f && mhz <= 464.0f) {
        band = 2;
        f0 = (uint8_t)cc1101_map((int)mhz, 378, 464, 31, 38);
        t0 = (mhz < 430.5f) ? 0x0Bu : 0x09u;
    } else if (mhz >= 779.0f && mhz <= 899.99f) {
        band = 3;
        f0 = (uint8_t)cc1101_map((int)mhz, 779, 899, 65, 76);
        t0 = (mhz < 861.0f) ? 0x0Bu : 0x09u;
    } else if (mhz >= 900.0f && mhz <= 928.0f) {
        band = 4;
        f0 = (uint8_t)cc1101_map((int)mhz, 900, 928, 77, 79);
        t0 = 0x09u;
    }
    if (fsctrl0) *fsctrl0 = f0;
    if (test0) *test0 = t0;
    if (out_band) *out_band = band;
}

#ifndef HOST_TEST
#include "common/diag.h"
#include "platform/board.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/time.h"

/* rpSPI.cpp's own `rpspi_write_read_bounded()` (see the header): the Pico
 * SDK's spi_write_read_blocking() spins forever if the peripheral never
 * becomes readable/writable. That comment documents a real hang the
 * reference hit on a DIFFERENT SPI device on this same CPU family; this
 * port carries the identical bounded transfer loop rather than calling the
 * SDK's blocking form directly. */
#define CC1101_SPI_STALL_SPINS 1000000u

static bool spi_xfer_bounded(const uint8_t *tx, uint8_t *rx, size_t len) {
    size_t tx_remaining = len, rx_remaining = len;
    unsigned idle = 0;
    while (rx_remaining || tx_remaining) {
        bool progressed = false;
        if (tx_remaining && spi_is_writable(FWOG_RADIO_SPI) &&
            rx_remaining < tx_remaining + 8u) {
            spi_get_hw(FWOG_RADIO_SPI)->dr = (uint32_t)*tx++;
            tx_remaining--;
            progressed = true;
        }
        if (rx_remaining && spi_is_readable(FWOG_RADIO_SPI)) {
            *rx++ = (uint8_t)spi_get_hw(FWOG_RADIO_SPI)->dr;
            rx_remaining--;
            progressed = true;
        }
        if (progressed) {
            idle = 0;
        } else if (++idle >= CC1101_SPI_STALL_SPINS) {
            return false;
        }
    }
    return true;
}

/* Datasheet 10.1 / 10 (p.29-31): after CSn goes low, the MCU must wait for
 * SO (MISO) to go low before clocking anything. MISO is the one SPI0 signal
 * shared by both radios (board.h: PIN_CC1101_MISO), so this needs no
 * per-radio pin -- whichever chip's CS is currently asserted is the one
 * driving it. Bounded: see CC1101_READY_TIMEOUT_US and the header's
 * "chip-ready" note on why an unbounded version of this exact wait must
 * never ship on the watchdog-protected main CPU. */
static bool wait_chip_ready(void) {
    absolute_time_t deadline = make_timeout_time_us(CC1101_READY_TIMEOUT_US);
    while (gpio_get(PIN_CC1101_MISO)) {
        if (time_reached(deadline)) return false;
    }
    return true;
}

static inline void cs_select(cc1101_t *r) { gpio_put(r->cs_pin, 0); }
static inline void cs_deselect(cc1101_t *r) { gpio_put(r->cs_pin, 1); }

void cc1101_bus_init(uint32_t baud_hz) {
    /* rpSPI::init(1'000'000, 8, 0, 0) -- mode 0, 8 bits. Shared by both
     * radios; call once. */
    spi_init(FWOG_RADIO_SPI, baud_hz);
    spi_set_format(FWOG_RADIO_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_CC1101_SCLK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CC1101_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CC1101_MISO, GPIO_FUNC_SPI);
}

void cc1101_bind(cc1101_t *radio, cc1101_radio_t which) {
    if (which == CC1101_RADIO_CS0) {
        radio->cs_pin = PIN_CC1101_CS0;
        radio->gdo0_pin = PIN_CC1101_GDO0_1;
        radio->gdo2_pin = PIN_CC1101_GDO2_1;
    } else {
        radio->cs_pin = PIN_CC1101_CS1;
        radio->gdo0_pin = PIN_CC1101_GDO0_2;
        radio->gdo2_pin = PIN_CC1101_GDO2_2;
    }
    radio->pa_dbm = 12;      /* ELECHOUSE default: `int pa = 12;` */
    radio->pa_band = 0;
    radio->mhz = 433.92f;    /* ELECHOUSE default: `float MHz = 433.92f;` */
    radio->ask_ook = true;   /* ELECHOUSE default: `byte modulation = 2;` (ASK) */

    /* CS direction/level is board_init_pins()'s job -- see the header. */
    gpio_init(radio->gdo0_pin);
    gpio_set_dir(radio->gdo0_pin, GPIO_IN);
    gpio_pull_up(radio->gdo0_pin);
    gpio_init(radio->gdo2_pin);
    gpio_set_dir(radio->gdo2_pin, GPIO_IN);
    gpio_pull_up(radio->gdo2_pin);
}

bool cc1101_reset(cc1101_t *radio) {
    /* Datasheet 19.1.2 / Figure 27, ELECHOUSE_CC1101::Reset(). */
    cs_select(radio);
    sleep_ms(1);
    cs_deselect(radio);
    sleep_ms(1);
    cs_select(radio);
    bool ok = wait_chip_ready();
    if (ok) {
        uint8_t tx = CC1101_STROBE_SRES, rx;
        ok = spi_xfer_bounded(&tx, &rx, 1);
    }
    if (ok) ok = wait_chip_ready();
    cs_deselect(radio);
    if (!ok) DIAG("[cc1101] reset timed out (cs=%u)\n", (unsigned)radio->cs_pin);
    return ok;
}

bool cc1101_read_reg(cc1101_t *radio, uint8_t addr, uint8_t *out) {
    uint8_t tx[2] = { (uint8_t)(addr | CC1101_HDR_READ_SINGLE), 0u };
    uint8_t rx[2] = { 0u, 0u };
    cs_select(radio);
    bool ok = wait_chip_ready() && spi_xfer_bounded(tx, rx, 2);
    cs_deselect(radio);
    if (ok && out) *out = rx[1];
    return ok;
}

bool cc1101_read_burst(cc1101_t *radio, uint8_t addr, uint8_t *out, uint8_t n) {
    uint8_t hdr = (uint8_t)(addr | CC1101_HDR_READ_BURST);
    uint8_t junk = 0u;
    cs_select(radio);
    bool ok = wait_chip_ready() && spi_xfer_bounded(&hdr, &junk, 1);
    for (uint8_t i = 0; ok && i < n; i++) {
        uint8_t zero = 0u;
        ok = spi_xfer_bounded(&zero, &out[i], 1);
    }
    cs_deselect(radio);
    return ok;
}

bool cc1101_write_reg(cc1101_t *radio, uint8_t addr, uint8_t value) {
    uint8_t tx[2] = { addr, value };
    uint8_t rx[2];
    cs_select(radio);
    bool ok = wait_chip_ready() && spi_xfer_bounded(tx, rx, 2);
    cs_deselect(radio);
    return ok;
}

bool cc1101_write_burst(cc1101_t *radio, uint8_t addr, const uint8_t *data, uint8_t n) {
    uint8_t hdr = (uint8_t)(addr | CC1101_HDR_WRITE_BURST);
    uint8_t junk = 0u;
    cs_select(radio);
    bool ok = wait_chip_ready() && spi_xfer_bounded(&hdr, &junk, 1);
    for (uint8_t i = 0; ok && i < n; i++) {
        ok = spi_xfer_bounded(&data[i], &junk, 1);
    }
    cs_deselect(radio);
    return ok;
}

bool cc1101_read_status(cc1101_t *radio, uint8_t addr, uint8_t *out) {
    uint8_t tx[2] = { (uint8_t)(addr | CC1101_HDR_READ_BURST), 0u };
    uint8_t rx[2] = { 0u, 0u };
    cs_select(radio);
    bool ok = wait_chip_ready() && spi_xfer_bounded(tx, rx, 2);
    cs_deselect(radio);
    if (ok && out) *out = rx[1];
    return ok;
}

cc1101_status_t cc1101_strobe(cc1101_t *radio, uint8_t strobe) {
    uint8_t tx = strobe, rx = 0u;
    cs_select(radio);
    /* GENUINE DEVIATION from ELECHOUSE::SpiStrobe(): see the header's
     * "chip-ready handshake" note -- the reference does not wait here, and
     * that is a real gap against the datasheet's unconditional rule, not a
     * documented optimization. */
    bool ok = wait_chip_ready();
    if (ok) ok = spi_xfer_bounded(&tx, &rx, 1);
    cs_deselect(radio);
    cc1101_status_t st = cc1101_decode_status(rx);
    st.ok = ok;
    return st;
}

static bool wait_for_state(cc1101_t *radio, uint8_t want_state, uint32_t timeout_ms) {
    /* rpCC1101::waitForStateChange() -- already a bounded, timer-based loop
     * in the reference. Ported as-is. */
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    for (;;) {
        cc1101_status_t st = cc1101_strobe(radio, CC1101_STROBE_SNOP);
        if (st.ok && !st.chip_rdy && st.state == want_state) return true;
        if (time_reached(deadline)) return false;
    }
}

bool cc1101_bringup(cc1101_t *radio) {
    if (!cc1101_reset(radio)) return false;

    bool ok = true;
    ok = ok && cc1101_write_reg(radio, CC1101_REG_FSCTRL1, 0x06u);

    /* setCCMode(true), the packet-mode fixed bank. The transient
     * setCCMode(false) pass RegConfigSettings() performs first in the
     * reference is NOT reproduced: setCCMode(true) overwrites every one of
     * these same registers, byte for byte, moments later in the reference
     * (same modulation, same pa, same mhz in between), so the discarded
     * pass leaves no trace in final chip state -- see the header. */
    ok = ok && cc1101_write_reg(radio, CC1101_REG_IOCFG2,   0x0Bu);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_IOCFG0,   0x06u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_PKTCTRL0, 0x05u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_MDMCFG3,  0xF8u);
    /* MDMCFG4 = 11 + m4RxBw; m4RxBw is ELECHOUSE's class-default 0 here,
     * always, in the one real call path -- see the header. */
    ok = ok && cc1101_write_reg(radio, CC1101_REG_MDMCFG4,  0x0Bu);

    /* setMHZ(433.92)'s only LASTING effect on register state: FSCTRL0 via
     * Calibrate()'s band lookup. setMHZ() never writes FREQ2/FREQ1/FREQ0
     * (dead local variables in the reference -- confirmed by reading
     * ELECHOUSE_CC1101_SRC_DRV.cpp's setMHZ() body); those stay at the
     * datasheet power-on default here too, until a real
     * cc1101_set_frequency() call -- exactly matching the reference, where
     * the real carrier is programmed by rpCC1101::setFrequency(), called
     * separately right after Init() in the one real caller.
     * Calibrate() also computes TEST0 and conditionally bumps FSCAL2, but
     * RegConfigSettings() unconditionally overwrites BOTH with the flat
     * constants below a few lines later in the reference, so neither
     * survives -- writing the flat constants directly reproduces the same
     * final state without the intermediate churn. */
    uint8_t fsctrl0 = 0u;
    cc1101_calib_lookup(radio->mhz, &fsctrl0, NULL, NULL);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_FSCTRL0, fsctrl0);

    ok = ok && cc1101_write_reg(radio, CC1101_REG_MDMCFG1,  0x02u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_MDMCFG0,  0xF8u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_CHANNR,   0x00u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_DEVIATN,  0x47u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_FREND1,   0x56u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_MCSM0,    0x18u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_FOCCFG,   0x16u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_BSCFG,    0x1Cu);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_AGCCTRL2, 0xC7u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_AGCCTRL1, 0x00u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_AGCCTRL0, 0xB2u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_FSCAL3,   0xE9u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_FSCAL2,   0x2Au);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_FSCAL1,   0x00u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_FSCAL0,   0x1Fu);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_FSTEST,   0x59u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_TEST2,    0x81u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_TEST1,    0x35u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_TEST0,    0x09u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_PKTCTRL1, 0x04u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_ADDR,     0x00u);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_PKTLEN,   0x00u);

    ok = ok && cc1101_set_modulation(radio, CC1101_MOD_ASK);
    if (!ok) DIAG("[cc1101] bringup failed (cs=%u)\n", (unsigned)radio->cs_pin);
    return ok;
}

bool cc1101_probe(cc1101_t *radio) {
    uint8_t partnum = 0xFFu, version = 0u;
    bool ok = cc1101_read_status(radio, CC1101_STATUS_PARTNUM, &partnum);
    ok = ok && cc1101_read_status(radio, CC1101_STATUS_VERSION, &version);
    return ok && partnum == 0u && version == 0x14u;
}

static bool merge_write(cc1101_t *radio, uint8_t addr, uint8_t mask, uint8_t bits) {
    uint8_t cur;
    if (!cc1101_read_reg(radio, addr, &cur)) return false;
    return cc1101_write_reg(radio, addr, cc1101_reg_merge(cur, mask, bits));
}

bool cc1101_set_frequency(cc1101_t *radio, uint32_t freq_hz) {
    if (!cc1101_freq_in_band(freq_hz)) return false;

    float mhz = (float)freq_hz / 1000000.0f;
    radio->mhz = mhz;   /* fix for bug 3 -- see the header */

    uint8_t freq2, freq1, freq0;
    cc1101_freq_word(freq_hz, CC1101_CRYSTAL_HZ, &freq2, &freq1, &freq0);
    bool ok = true;
    ok = ok && cc1101_write_reg(radio, CC1101_REG_FREQ2, freq2);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_FREQ1, freq1);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_FREQ0, freq0);

    /* ELECHOUSE::Calibrate(), folded in here -- see bug 3 above. */
    uint8_t fsctrl0, test0;
    int band;
    cc1101_calib_lookup(mhz, &fsctrl0, &test0, &band);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_FSCTRL0, fsctrl0);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_TEST0, test0);
    if (ok && test0 == 0x09u) {
        /* Calibrate()'s `SpiReadStatus(CC1101_FSCAL2)`: FSCAL2 (0x24) is an
         * ordinary CONFIG register, not a true status register (those are
         * only 0x30-0x3D per datasheet 10.2) -- the reference just reuses
         * that helper's read pattern. A plain cc1101_read_reg() reads the
         * same byte. */
        uint8_t fscal2;
        if (cc1101_read_reg(radio, CC1101_REG_FSCAL2, &fscal2) && fscal2 < 32u) {
            ok = ok && cc1101_write_reg(radio, CC1101_REG_FSCAL2, (uint8_t)(fscal2 + 32u));
        }
        if (ok && band != radio->pa_band) {
            ok = ok && cc1101_set_power(radio, radio->pa_dbm);
        }
    }

    /* rpCC1101::setFrequency(): strobe SCAL, then wait for CALIBRATE. */
    ok = ok && cc1101_strobe(radio, CC1101_STROBE_SCAL).ok;
    if (ok) ok = wait_for_state(radio, (uint8_t)CC1101_STATE_CALIBRATE,
                                 CC1101_STATE_CHANGE_TIMEOUT_MS);
    return ok;
}

bool cc1101_set_modulation(cc1101_t *radio, cc1101_modulation_t mod) {
    uint8_t m = (uint8_t)mod;
    if (m > 4u) m = 4u;
    uint8_t mod_bits, frend0;
    switch (m) {
    case CC1101_MOD_2FSK: mod_bits = 0x00u; frend0 = 0x10u; break;
    case CC1101_MOD_GFSK: mod_bits = 0x10u; frend0 = 0x10u; break;
    case CC1101_MOD_ASK:  mod_bits = 0x30u; frend0 = 0x11u; break;
    case CC1101_MOD_4FSK: mod_bits = 0x40u; frend0 = 0x10u; break;
    default:              mod_bits = 0x70u; frend0 = 0x10u; break; /* MSK */
    }
    radio->ask_ook = (m == (uint8_t)CC1101_MOD_ASK);
    bool ok = merge_write(radio, CC1101_REG_MDMCFG2, 0x70u, mod_bits);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_FREND0, frend0);
    ok = ok && cc1101_set_power(radio, radio->pa_dbm);
    return ok;
}

bool cc1101_set_data_rate(cc1101_t *radio, float kbaud) {
    uint8_t drate_m;
    uint8_t drate_e = cc1101_drate_encode(kbaud, &drate_m);
    bool ok = merge_write(radio, CC1101_REG_MDMCFG4, 0x0Fu, drate_e);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_MDMCFG3, drate_m);
    return ok;
}

bool cc1101_set_deviation(cc1101_t *radio, float khz) {
    /* At the exact top of the clamp range (380.859375 kHz) the search
     * returns 0x80, which sets DEVIATN's bit 7 -- datasheet-reserved
     * ("Not used"), one past the valid 0x00-0x77 range the 3-bit
     * DEVIATION_E/DEVIATION_M fields actually cover. Same class of found,
     * flagged boundary quirk as cc1101_drate_encode()'s 1621.83 -> 16 case
     * (see the header). Masked here rather than written raw. */
    return cc1101_write_reg(radio, CC1101_REG_DEVIATN,
                             (uint8_t)(cc1101_deviation_encode(khz) & 0x7Fu));
}

bool cc1101_set_rx_bandwidth(cc1101_t *radio, float khz) {
    return merge_write(radio, CC1101_REG_MDMCFG4, 0xF0u, cc1101_rxbw_encode(khz));
}

bool cc1101_set_channel(cc1101_t *radio, uint8_t channel) {
    return cc1101_write_reg(radio, CC1101_REG_CHANNR, channel);
}

bool cc1101_set_channel_spacing(cc1101_t *radio, float khz) {
    uint8_t chanspc_m;
    uint8_t chanspc_e = cc1101_chsp_encode(khz, &chanspc_m);
    bool ok = merge_write(radio, CC1101_REG_MDMCFG1, 0x03u, chanspc_e);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_MDMCFG0, chanspc_m);
    return ok;
}

bool cc1101_set_sync_word(cc1101_t *radio, uint8_t hi, uint8_t lo) {
    bool ok = cc1101_write_reg(radio, CC1101_REG_SYNC1, hi);
    ok = ok && cc1101_write_reg(radio, CC1101_REG_SYNC0, lo);
    return ok;
}

bool cc1101_set_power(cc1101_t *radio, int dbm) {
    radio->pa_dbm = dbm;
    int band = 0;
    uint8_t entry = cc1101_patable_lookup(radio->mhz, dbm, &band);
    radio->pa_band = band;
    uint8_t table[8];
    cc1101_patable_build(entry, radio->ask_ook, table);
    return cc1101_write_burst(radio, CC1101_PATABLE, table, 8u);
}

bool cc1101_set_packet_length(cc1101_t *radio, uint8_t len) {
    return cc1101_write_reg(radio, CC1101_REG_PKTLEN, len);
}

bool cc1101_set_length_config(cc1101_t *radio, uint8_t mode) {
    if (mode > 3u) mode = 3u;
    return merge_write(radio, CC1101_REG_PKTCTRL0, 0x03u, mode);
}

bool cc1101_set_crc(cc1101_t *radio, bool enable) {
    return merge_write(radio, CC1101_REG_PKTCTRL0, 0x04u, enable ? 0x04u : 0u);
}

bool cc1101_set_white_data(cc1101_t *radio, bool enable) {
    return merge_write(radio, CC1101_REG_PKTCTRL0, 0x40u, enable ? 0x40u : 0u);
}

bool cc1101_set_pkt_format(cc1101_t *radio, uint8_t format) {
    if (format > 3u) format = 3u;
    return merge_write(radio, CC1101_REG_PKTCTRL0, 0x30u, (uint8_t)(format << 4));
}

bool cc1101_set_addr(cc1101_t *radio, uint8_t addr) {
    return cc1101_write_reg(radio, CC1101_REG_ADDR, addr);
}

bool cc1101_set_addr_check(cc1101_t *radio, uint8_t mode) {
    if (mode > 3u) mode = 3u;
    return merge_write(radio, CC1101_REG_PKTCTRL1, 0x03u, mode);
}

bool cc1101_set_manchester(cc1101_t *radio, bool enable) {
    return merge_write(radio, CC1101_REG_MDMCFG2, 0x08u, enable ? 0x08u : 0u);
}

bool cc1101_set_sync_mode(cc1101_t *radio, uint8_t mode) {
    if (mode > 7u) mode = 7u;
    return merge_write(radio, CC1101_REG_MDMCFG2, 0x07u, mode);
}

bool cc1101_set_fec(cc1101_t *radio, bool enable) {
    return merge_write(radio, CC1101_REG_MDMCFG1, 0x80u, enable ? 0x80u : 0u);
}

bool cc1101_set_preamble(cc1101_t *radio, uint8_t setting) {
    if (setting > 7u) setting = 7u;
    return merge_write(radio, CC1101_REG_MDMCFG1, 0x70u, (uint8_t)(setting << 4));
}

bool cc1101_set_pqt(cc1101_t *radio, uint8_t threshold) {
    if (threshold > 7u) threshold = 7u;
    return merge_write(radio, CC1101_REG_PKTCTRL1, 0xE0u, (uint8_t)(threshold << 5));
}

bool cc1101_set_crc_autoflush(cc1101_t *radio, bool enable) {
    return merge_write(radio, CC1101_REG_PKTCTRL1, 0x08u, enable ? 0x08u : 0u);
}

bool cc1101_set_append_status(cc1101_t *radio, bool enable) {
    return merge_write(radio, CC1101_REG_PKTCTRL1, 0x04u, enable ? 0x04u : 0u);
}

bool cc1101_set_dc_filter_off(cc1101_t *radio, bool enable) {
    return merge_write(radio, CC1101_REG_MDMCFG2, 0x80u, enable ? 0x80u : 0u);
}

bool cc1101_idle(cc1101_t *radio) {
    cc1101_strobe(radio, CC1101_STROBE_SIDLE);
    return wait_for_state(radio, (uint8_t)CC1101_STATE_IDLE, CC1101_STATE_CHANGE_TIMEOUT_MS);
}

bool cc1101_tx(cc1101_t *radio) {
    cc1101_strobe(radio, CC1101_STROBE_STX);
    return wait_for_state(radio, (uint8_t)CC1101_STATE_TX, CC1101_STATE_CHANGE_TIMEOUT_MS);
}

bool cc1101_rx(cc1101_t *radio) {
    cc1101_strobe(radio, CC1101_STROBE_SRX);
    bool ok = wait_for_state(radio, (uint8_t)CC1101_STATE_RX, CC1101_STATE_CHANGE_TIMEOUT_MS);
    if (ok) cc1101_strobe(radio, CC1101_STROBE_SAFC);
    return ok;
}

bool cc1101_flush_rx(cc1101_t *radio) {
    cc1101_strobe(radio, CC1101_STROBE_SIDLE);
    bool ok = wait_for_state(radio, (uint8_t)CC1101_STATE_IDLE, CC1101_STATE_CHANGE_TIMEOUT_MS);
    cc1101_strobe(radio, CC1101_STROBE_SFRX);
    return ok;
}

void cc1101_sleep(cc1101_t *radio) {
    /* rpCC1101::sleep(): switchIdle() then ELECHOUSE::goSleep(), which
     * itself strobes SIDLE again (redundant, matched here) then SPWD. */
    cc1101_idle(radio);
    cc1101_strobe(radio, CC1101_STROBE_SIDLE);
    cc1101_strobe(radio, CC1101_STROBE_SPWD);
}

bool cc1101_send_packet(cc1101_t *radio, const uint8_t *data, uint8_t len) {
    gpio_set_dir(radio->gdo0_pin, GPIO_IN);   /* m_pGDO0->setDirection(false) */
    bool ok = cc1101_write_reg(radio, CC1101_TXFIFO, len);
    ok = ok && cc1101_write_burst(radio, CC1101_TXFIFO, data, len);
    ok = ok && cc1101_strobe(radio, CC1101_STROBE_STX).ok;

    /* Fix for bug 4 (see the header): track whether each edge was actually
     * observed, rather than the reference's `return true;` regardless. */
    bool sync_seen = false, end_seen = false;
    if (ok) {
        absolute_time_t deadline = make_timeout_time_ms(100);
        while (!time_reached(deadline)) {
            if (gpio_get(radio->gdo0_pin)) { sync_seen = true; break; }
        }
    }
    if (ok && sync_seen) {
        absolute_time_t deadline = make_timeout_time_ms(100);
        while (!time_reached(deadline)) {
            if (!gpio_get(radio->gdo0_pin)) { end_seen = true; break; }
        }
    }
    cc1101_strobe(radio, CC1101_STROBE_SFTX);
    if (!(ok && sync_seen && end_seen)) {
        DIAG("[cc1101] send_packet incomplete (cs=%u sync=%d end=%d)\n",
             (unsigned)radio->cs_pin, (int)sync_seen, (int)end_seen);
    }
    return ok && sync_seen && end_seen;
}

int cc1101_receive_packet(cc1101_t *radio, uint8_t *data, uint8_t len) {
    if (!cc1101_read_burst(radio, CC1101_RXFIFO, data, len)) return -1;
    cc1101_strobe(radio, CC1101_STROBE_SFRX);
    return (int)len;   /* fix for bug 2 -- see the header */
}

int cc1101_rx_bytes_available(cc1101_t *radio) {
    uint8_t v;
    if (!cc1101_read_status(radio, CC1101_STATUS_RXBYTES, &v)) return -1;
    return (int)v;
}

int16_t cc1101_get_rssi(cc1101_t *radio) {
    uint8_t v = 0u;
    cc1101_read_status(radio, CC1101_STATUS_RSSI, &v);
    return cc1101_rssi_dbm((int8_t)v);
}

uint8_t cc1101_get_lqi(cc1101_t *radio, bool *crc_ok) {
    uint8_t v = 0u;
    cc1101_read_status(radio, CC1101_STATUS_LQI, &v);
    if (crc_ok) *crc_ok = cc1101_lqi_crc_ok(v);
    return cc1101_lqi_value(v);   /* fix for bug 1 -- see the header */
}

bool cc1101_get_gdo0(cc1101_t *radio) {
    return gpio_get(radio->gdo0_pin);
}

#endif /* HOST_TEST */
