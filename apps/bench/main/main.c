/* Main-CPU bench console -- the CC1101 half of the bench rig.
 *
 * Pairs with apps/bench/display: same one-line `OK `/`ERR ` command protocol,
 * same parseable `HB ` heartbeat, driven by the same tools/bench.py (pass
 * `--cpu main`). This is the main CPU, so it covers what bench_display
 * physically cannot reach: the two CC1101 sub-GHz radios on SPI0.
 *
 * ---------------------------------------------------------------------------
 * THIS APP DOES NOT TRANSMIT.
 *
 * Every command here is a register read, plus the datasheet reset sequence.
 * Nothing sets output power, programs a carrier, or issues an SRX/STX strobe.
 * That is deliberate and is the whole point of this step: proving the parts
 * are present and the SPI bus works does not need RF, and the antenna path
 * runs through the display CPU's PCAL6416 whose bit map is still unconfirmed
 * (the hardware record). Radios must not be keyed into an unknown antenna path.
 * Whoever adds a TX command must settle fact 31 first.
 * ---------------------------------------------------------------------------
 *
 * GETTING BACK OUT: this CPU has a reachable hardware BOOTSEL, and
 * `fw bootsel --cpu main` works from the host besides, so unlike a display
 * image this one needs no built-in escape.
 *
 * WATCHDOG: board_init() arms it at FWOG_WATCHDOG_MS (8.3 s, the RP2040
 * hardware maximum, by default), because a
 * hung main CPU has no other recovery path. Every loop in this file that can
 * run longer than that kicks it. A bare wait longer than the timeout resets
 * the board and looks exactly like a crash.
 */
#include "fwog_main.h"
#include "fpga/fpga_clk.h"
/* For `fpga clk`, which reloads the bitstream after changing the clock so the
 * gateware starts on it rather than having it change under a running design.
 * Not in the umbrella header -- the loader is board_init()'s business, and
 * this app is deliberately the exception. */
#include "fpga/ice40.h"
/* Not in the umbrella header on purpose -- fwog_main_fs is opt-in, and this
 * app is one of the few that links it. See bsp/fwog_main.h. */
#include "fs/fwog_fs.h"
#include "fs/fs_geom.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#if FWOG_DIAG == FWOG_DIAG_USB
#include "pico/stdio_usb.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Calls board_watchdog_kick() from its loops -- see the WATCHDOG note in the
   file header above, and watchdog.h. */
FWOG_WATCHDOG_DEFAULT();

/* The reference's own choice -- `obSpiRadio.init(1'000'000, 8, 0, 0)`. Well
   inside the datasheet's 10 MHz single-access ceiling; preserved rather than
   raised, since nothing here is throughput-bound. */
#define BENCH_RADIO_SPI_HZ 1000000u

/* What a real CC1101 must report: PARTNUM 0x00, VERSION 0x14 (datasheet
   10.3, and ELECHOUSE::getCC1101()'s own check). VERSION is the useful one --
   PARTNUM being 0 means an absent or unselected chip reads the same as a
   healthy one on that register alone, so a bus that is dead in the "MISO
   stuck low" direction would pass on PARTNUM and fail on VERSION. */
#define CC1101_EXPECT_PARTNUM 0x00u
#define CC1101_EXPECT_VERSION 0x14u

/* Loopback defaults. 433.92 MHz because the PCAL6416's antenna path comes up
 * on FWOG_ANT_400MHZ for BOTH radios (pcal6416.h's fwog_ioexp_default()), so
 * this is the one band that needs no expander change to try -- and the
 * expander is on the display CPU, across the link, out of this app's reach.
 *
 * -30 dBm is the lowest entry in the PA table. The two radios are centimetres
 * apart on one board, so this is ample; it also keeps the emission minimal
 * and leaves headroom to SEE the antenna filter attenuate, which a loud
 * transmitter would swamp. */
#define BENCH_RF_DEFAULT_HZ  433920000u
#define BENCH_RF_DEFAULT_DBM (-30)

/* Payload of one loopback packet. Short: this is a link check, not a
 * throughput test, and a short packet fits the FIFO with the length byte and
 * the two appended status bytes with room to spare. */
#define BENCH_RF_PAYLOAD_LEN 6u

static cc1101_t s_radio[2];
static bool     s_bound;
static bool     s_rf_ready;
static uint32_t s_rf_hz  = BENCH_RF_DEFAULT_HZ;
static int      s_rf_dbm = BENCH_RF_DEFAULT_DBM;

static const char *okstr(bool b) { return b ? "ok" : "fail"; }

static bool streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

/* strtol wrapper: base 0 accepts "0x.." as well as plain decimal. *ok is
 * false on an empty string, a non-numeric string, or trailing garbage. Same
 * helper, same rationale, as apps/bench/display. */
static long parse_long(const char *s, bool *ok) {
    char *end;
    long v = strtol(s, &end, 0);
    *ok = (s[0] != '\0') && (*end == '\0');
    return v;
}

static const char *state_name(uint8_t st) {
    switch (st) {
    case CC1101_STATE_IDLE:             return "idle";
    case CC1101_STATE_RX:               return "rx";
    case CC1101_STATE_TX:               return "tx";
    case CC1101_STATE_FSTXON:           return "fstxon";
    case CC1101_STATE_CALIBRATE:        return "calibrate";
    case CC1101_STATE_SETTLING:         return "settling";
    case CC1101_STATE_RXFIFO_OVERFLOW:  return "rxfifo_overflow";
    case CC1101_STATE_TXFIFO_UNDERFLOW: return "txfifo_underflow";
    default:                            return "?";
    }
}

/* Parse a "0" or "1" radio selector into an index. Named by CHIP SELECT, not
   by an ordinal "radio 1/2": the reference's own variable names disagree with
   its schematic (see cc1101.h's header note), so this app refuses to inherit
   that ambiguity. */
static bool parse_radio(const char *s, int *out) {
    bool ok;
    const long v = parse_long(s, &ok);
    if (!ok || v < 0 || v > 1) return false;
    *out = (int)v;
    return true;
}

/* Read PARTNUM/VERSION for one radio and report whether both match. Pure
   reads -- see the file header: no reset, no strobe, no configuration. */
static bool probe_one(int idx, uint8_t *out_part, uint8_t *out_ver) {
    uint8_t part = 0xFFu, ver = 0xFFu;
    const bool got_part =
        cc1101_read_status(&s_radio[idx], CC1101_STATUS_PARTNUM, &part);
    const bool got_ver =
        cc1101_read_status(&s_radio[idx], CC1101_STATUS_VERSION, &ver);
    if (out_part) *out_part = part;
    if (out_ver) *out_ver = ver;
    return got_part && got_ver &&
           part == CC1101_EXPECT_PARTNUM && ver == CC1101_EXPECT_VERSION;
}

static void cmd_help(void) {
    DIAG("commands:\n"
         "  help                       this list\n"
         "  radio                      probe BOTH CC1101s (PARTNUM/VERSION)\n"
         "  reset <0|1>                datasheet manual reset, then re-probe\n"
         "  rstat <0|1> <addr>         read a status register (0x30-0x3D)\n"
         "  rreg <0|1> <addr>          read a config register (0x00-0x2E)\n"
         "  rf [freq_hz] [dbm]         bring BOTH radios up (433.92M, -30 dBm)\n"
         "  loop <tx 0|1> [count]      radio-to-radio packet test, on board\n"
         "  pins                       report the SPI0 and GDO pin map\n"
         "  flashcrc [passes]                 CRC 256 KB of XIP repeatedly "
         "(default 200 passes, ~50-150 ms each, ~15-30 s total)\n"
         "  fpga | fpga clk <int> [frac]   CDONE, clock; or re-drive + reload\n"
         "  reboot [delay_ms]          reset THIS cpu after a delay (the hardware record)\n"
         "  iodir default | iodir <word> [gp26 gp27]   set breakout I/O "
         "direction\n"
         "  iodir probe                raw FPGA direction-register readback\n"
         "  iodir window <0|1>         hold the IO_CONFIG window open/closed\n"
         "  iopin | iopin <gpio> 0|1 | iopin <gpio> read   breakout pin LEVEL\n"
         "  i2cpin | i2cpin <sda|scl> <low|release>   breakout I2C lines\n"
         "  fs info                    mount (if needed) and report free/total\n"
         "  fs ls [dir]                list a directory by index\n"
         "  fs write <path> <kb>       write kb KB of a position-dependent pattern\n"
         "  fs read <path>             read back and verify that pattern\n"
         "  fs rm <path>               delete a file\n"
         "  fs format                  DESTRUCTIVE -- rewrite the file table\n");
    DIAG("OK help 20 commands\n");
}

static void cmd_radio(void) {
    if (!s_bound) { DIAG("ERR radio not-bound\n"); return; }
    uint8_t p0, v0, p1, v1;
    const bool ok0 = probe_one(0, &p0, &v0);
    const bool ok1 = probe_one(1, &p1, &v1);
    DIAG("OK radio cs0=%s partnum=0x%02X version=0x%02X "
         "cs1=%s partnum=0x%02X version=0x%02X "
         "(expect partnum=0x00 version=0x14)\n",
         okstr(ok0), (unsigned)p0, (unsigned)v0,
         okstr(ok1), (unsigned)p1, (unsigned)v1);
}

static void cmd_reset(int argc, char **argv) {
    if (!s_bound) { DIAG("ERR reset not-bound\n"); return; }
    if (argc != 2) { DIAG("ERR reset usage: reset <0|1>\n"); return; }
    int idx;
    if (!parse_radio(argv[1], &idx)) {
        DIAG("ERR reset bad radio (0=CS0, 1=CS1)\n");
        return;
    }
    const bool rst = cc1101_reset(&s_radio[idx]);
    uint8_t part, ver;
    const bool ok = probe_one(idx, &part, &ver);
    DIAG("OK reset cs%d reset=%s probe=%s partnum=0x%02X version=0x%02X\n",
         idx, okstr(rst), okstr(ok), (unsigned)part, (unsigned)ver);
}

static void cmd_rstat(int argc, char **argv) {
    if (!s_bound) { DIAG("ERR rstat not-bound\n"); return; }
    if (argc != 3) { DIAG("ERR rstat usage: rstat <0|1> <addr>\n"); return; }
    int idx;
    bool oka;
    if (!parse_radio(argv[1], &idx)) {
        DIAG("ERR rstat bad radio (0=CS0, 1=CS1)\n");
        return;
    }
    const long addr = parse_long(argv[2], &oka);
    /* Status registers are 0x30-0x3D and are single-access only (datasheet
       10.2) -- a burst read of this space returns something else entirely, so
       the range is enforced here rather than passed through. */
    if (!oka || addr < 0x30 || addr > 0x3D) {
        DIAG("ERR rstat bad addr (0x30-0x3D)\n");
        return;
    }
    uint8_t v = 0xFFu;
    if (!cc1101_read_status(&s_radio[idx], (uint8_t)addr, &v)) {
        DIAG("ERR rstat cs%d addr=0x%02X read-timeout\n", idx, (unsigned)addr);
        return;
    }
    DIAG("OK rstat cs%d addr=0x%02X value=0x%02X\n", idx, (unsigned)addr,
         (unsigned)v);
}

static void cmd_rreg(int argc, char **argv) {
    if (!s_bound) { DIAG("ERR rreg not-bound\n"); return; }
    if (argc != 3) { DIAG("ERR rreg usage: rreg <0|1> <addr>\n"); return; }
    int idx;
    bool oka;
    if (!parse_radio(argv[1], &idx)) {
        DIAG("ERR rreg bad radio (0=CS0, 1=CS1)\n");
        return;
    }
    const long addr = parse_long(argv[2], &oka);
    if (!oka || addr < 0x00 || addr > 0x2E) {
        DIAG("ERR rreg bad addr (0x00-0x2E)\n");
        return;
    }
    uint8_t v = 0xFFu;
    if (!cc1101_read_reg(&s_radio[idx], (uint8_t)addr, &v)) {
        DIAG("ERR rreg cs%d addr=0x%02X read-timeout\n", idx, (unsigned)addr);
        return;
    }
    DIAG("OK rreg cs%d addr=0x%02X value=0x%02X\n", idx, (unsigned)addr,
         (unsigned)v);
}

/* Bring both radios up on one frequency and power. Separate from the boot
 * path on purpose: probing (which this app does unprompted) is read-only,
 * while this writes the whole configuration bank and is therefore something
 * the operator asks for explicitly. */
static void cmd_rf(int argc, char **argv) {
    if (!s_bound) { DIAG("ERR rf not-bound\n"); return; }
    if (argc > 3) { DIAG("ERR rf usage: rf [freq_hz] [dbm]\n"); return; }

    uint32_t hz = BENCH_RF_DEFAULT_HZ;
    int dbm = BENCH_RF_DEFAULT_DBM;
    if (argc >= 2) {
        bool ok;
        const long v = parse_long(argv[1], &ok);
        if (!ok || v <= 0) { DIAG("ERR rf bad freq_hz\n"); return; }
        hz = (uint32_t)v;
    }
    if (argc >= 3) {
        bool ok;
        const long v = parse_long(argv[2], &ok);
        if (!ok || v < -30 || v > 10) { DIAG("ERR rf bad dbm (-30..10)\n"); return; }
        dbm = (int)v;
    }
    /* The driver's own band gate, checked here so the failure names the
       frequency rather than surfacing as a silent mis-tune. */
    if (!cc1101_freq_in_band(hz)) {
        DIAG("ERR rf %u Hz is outside the three legal bands "
             "(300-348, 387-464, 779-928 MHz)\n", (unsigned)hz);
        return;
    }

    bool all = true;
    for (int i = 0; i < 2; i++) {
        board_watchdog_kick();
        bool ok = cc1101_bringup(&s_radio[i]);
        ok = ok && cc1101_set_frequency(&s_radio[i], hz);
        ok = ok && cc1101_set_power(&s_radio[i], dbm);
        ok = ok && cc1101_idle(&s_radio[i]);
        DIAG("  cs%d bringup=%s\n", i, okstr(ok));
        all = all && ok;
    }
    s_rf_ready = all;
    s_rf_hz = hz;
    s_rf_dbm = dbm;
    DIAG("OK rf freq_hz=%u dbm=%d both=%s "
         "(antenna path is the expander's, on the DISPLAY cpu -- default is "
         "FWOG_ANT_400MHZ for both radios)\n",
         (unsigned)hz, dbm, okstr(all));
}

/* One transmit-and-receive attempt between the two on-board radios.
 * `tx`/`rx` are chip-select indices. Returns true if a packet came back with
 * the payload intact. */
static bool loop_once(int tx, int rx, uint8_t seq, unsigned *out_n,
                      int *out_rssi, uint8_t *out_lqi, bool *out_crc,
                      bool *out_match, bool *out_sent) {
    uint8_t payload[BENCH_RF_PAYLOAD_LEN] = {'F', 'W', 'O', 'G', 0u, 0u};
    payload[4] = seq;
    payload[5] = (uint8_t)~seq;

    /* Receiver first, and flushed: a stale FIFO from a previous attempt would
       otherwise be read as this attempt's reply. */
    cc1101_flush_rx(&s_radio[rx]);
    if (!cc1101_rx(&s_radio[rx])) return false;

    /* Reported separately from reception, because the two failures look
       identical from the outside and are not the same problem at all:
       cc1101_send_packet() returns false when GDO0 never showed the sync/end
       edges, i.e. the TRANSMITTER never keyed -- a synthesiser that will not
       lock on this band. A receiver that simply heard nothing means the RF
       path did not carry it. Collapsing both into "received=0" is what made
       the 315 MHz result undiagnosable on first measurement. */
    const bool sent = cc1101_send_packet(&s_radio[tx], payload,
                                         BENCH_RF_PAYLOAD_LEN);
    if (out_sent) *out_sent = sent;
    if (!sent) return false;

    /* Bounded, and kicks: a wedged radio must not take the CPU down with it.
       The 300 ms bound is well inside FWOG_WATCHDOG_MS either way. */
    const absolute_time_t deadline = make_timeout_time_ms(300);
    int raw = 0;
    while (!time_reached(deadline)) {
        board_watchdog_kick();
        raw = cc1101_rx_bytes_available(&s_radio[rx]);
        if (raw > 0 && (raw & 0x7F) > 0) break;
    }
    const unsigned n = (raw > 0) ? (unsigned)(raw & 0x7F) : 0u;
    if (out_n) *out_n = n;
    if (n == 0u) return false;

    /* Length byte, payload, then the two appended status bytes the reference's
       RegConfigSettings() leaves enabled (PKTCTRL1 APPEND_STATUS). */
    uint8_t buf[32];
    const unsigned want = (n > sizeof buf) ? (unsigned)sizeof buf : n;
    if (cc1101_receive_packet(&s_radio[rx], buf, (uint8_t)want) < 0) return false;

    const bool match = (want >= 1u + BENCH_RF_PAYLOAD_LEN) &&
                       (buf[0] == BENCH_RF_PAYLOAD_LEN) &&
                       (memcmp(&buf[1], payload, BENCH_RF_PAYLOAD_LEN) == 0);
    if (out_match) *out_match = match;

    /* Per-PACKET RSSI and LQI, from the appended status bytes -- not the
       free-running RSSI register, which reads whatever the channel held at
       the moment it was sampled. This is the number the antenna-filter sweep
       needs. */
    if (want >= 3u + BENCH_RF_PAYLOAD_LEN) {
        if (out_rssi) *out_rssi = cc1101_rssi_dbm((int8_t)buf[1 + BENCH_RF_PAYLOAD_LEN]);
        const uint8_t lqi_reg = buf[2 + BENCH_RF_PAYLOAD_LEN];
        if (out_lqi) *out_lqi = cc1101_lqi_value(lqi_reg);
        if (out_crc) *out_crc = cc1101_lqi_crc_ok(lqi_reg);
    }
    return match;
}

static void cmd_loop(int argc, char **argv) {
    if (!s_bound) { DIAG("ERR loop not-bound\n"); return; }
    if (!s_rf_ready) { DIAG("ERR loop run 'rf' first\n"); return; }
    if (argc < 2 || argc > 3) {
        DIAG("ERR loop usage: loop <tx 0|1> [count]\n");
        return;
    }
    int tx;
    if (!parse_radio(argv[1], &tx)) {
        DIAG("ERR loop bad radio (0=CS0, 1=CS1)\n");
        return;
    }
    long count = 8;
    if (argc == 3) {
        bool ok;
        count = parse_long(argv[2], &ok);
        if (!ok || count < 1 || count > 100) {
            DIAG("ERR loop bad count (1-100)\n");
            return;
        }
    }
    const int rx = 1 - tx;

    unsigned got = 0u, crc_ok = 0u, sent_ok = 0u;
    long rssi_sum = 0;
    unsigned rssi_n = 0u;
    uint8_t last_lqi = 0u;
    for (long i = 0; i < count; i++) {
        board_watchdog_kick();
        unsigned n = 0u;
        int rssi = 0;
        uint8_t lqi = 0u;
        bool crc = false, match = false, sent = false;
        const bool ok = loop_once(tx, rx, (uint8_t)i, &n, &rssi, &lqi, &crc,
                                  &match, &sent);
        if (sent) sent_ok++;
        if (ok) {
            got++;
            rssi_sum += rssi;
            rssi_n++;
            last_lqi = lqi;
            if (crc) crc_ok++;
        }
    }
    /* Leave both radios idle rather than one stuck in RX burning current and
       holding the channel. */
    cc1101_idle(&s_radio[0]);
    cc1101_idle(&s_radio[1]);

    DIAG("OK loop tx=cs%d rx=cs%d tried=%ld keyed=%u received=%u crc_ok=%u "
         "rssi_dbm=%ld lqi=%u freq_hz=%u dbm=%d\n",
         tx, rx, count, sent_ok, got, crc_ok,
         rssi_n ? (rssi_sum / (long)rssi_n) : 0, (unsigned)last_lqi,
         (unsigned)s_rf_hz, s_rf_dbm);
}

static void cmd_pins(void) {
    DIAG("OK pins miso=%u sclk=%u mosi=%u cs0=%u cs1=%u "
         "gdo0_1=%u gdo2_1=%u gdo0_2=%u gdo2_2=%u spi_hz=%u\n",
         (unsigned)PIN_CC1101_MISO, (unsigned)PIN_CC1101_SCLK,
         (unsigned)PIN_CC1101_MOSI, (unsigned)PIN_CC1101_CS0,
         (unsigned)PIN_CC1101_CS1, (unsigned)PIN_CC1101_GDO0_1,
         (unsigned)PIN_CC1101_GDO2_1, (unsigned)PIN_CC1101_GDO0_2,
         (unsigned)PIN_CC1101_GDO2_2, (unsigned)BENCH_RADIO_SPI_HZ);
}

/* CRC a 256 KB XIP window repeatedly. The value must not change between
 * passes: a red-button press that reached main's QSPI_SS would corrupt an
 * in-flight read, which shows up here as a differing CRC or a hard fault.
 *
 * fwog_crc32() (bsp/common/crc.c:24-33) is bitwise -- 8 shift/xor ops per
 * byte -- over 262144 bytes, plus XIP cache misses (256 KB deliberately
 * thrashes the 16 KB cache). Real cost is roughly 50-150 ms per pass, NOT
 * "a few milliseconds", so the 200-pass default blocks for ~15-30 s.
 * tools/bench.py uses a 2.0 s reply timeout, so a plain `flashcrc` call from
 * that harness WILL time out unless the caller raises it. The watchdog is
 * kicked every pass rather than every N regardless. */
#define FLASHCRC_BASE        ((const uint8_t *)0x10000000u)
#define FLASHCRC_LEN         (256u * 1024u)
#define FLASHCRC_MAX_PASSES  1000u

static void cmd_flashcrc(int argc, char **argv) {
    unsigned passes = 200u;
    if (argc >= 2) {
        bool ok;
        const long v = parse_long(argv[1], &ok);
        if (!ok || v < 1 || v > (long)FLASHCRC_MAX_PASSES) {
            DIAG("ERR flashcrc bad passes (1-%u)\n", FLASHCRC_MAX_PASSES);
            return;
        }
        passes = (unsigned)v;
    }

    uint32_t first = 0;
    bool stable = true;
    unsigned diffs = 0u;
    for (unsigned i = 0; i < passes; i++) {
        uint32_t c = fwog_crc32(FLASHCRC_BASE, FLASHCRC_LEN);
        if (i == 0) first = c;
        else if (c != first) { stable = false; diffs++; }
        board_watchdog_kick();
    }
    DIAG("OK flashcrc passes=%u crc=0x%08X stable=%s diffs=%u\n",
         passes, (unsigned)first, stable ? "yes" : "no", diffs);
}

/* `reboot [delay_ms]` -- reset THIS CPU after a delay.
 *
 * For the hardware record's MAIN_BOOT_OE check, which needs main's BOOTSEL strap
 * sampled while the red button is held. The delay is the whole point: the
 * operator (or the display's `bootoe`) has to get the board into the state
 * under test BEFORE the strap is sampled, and a reset that happened the
 * instant the command arrived would leave no window to do it in.
 *
 * Uses the watchdog because that is the only reset this CPU can give itself.
 * NOTE that means board_watchdog_caused_reboot() will read 1 afterwards --
 * see the hardware record, that flag is already unreliable for unrelated reasons and
 * must not be read as evidence about anything here.
 *
 * Whether a watchdog reset re-samples the QSPI_SS strap at all is NOT
 * established -- it is precisely what the positive control in that test is
 * for. If the control fails, escalate to a real power cycle rather than
 * concluding anything about the lockout. */
static void cmd_reboot(int argc, char **argv) {
    long ms = 500;
    if (argc >= 2) {
        bool ok;
        ms = parse_long(argv[1], &ok);
        if (!ok || ms < 1 || ms > 10000) {
            DIAG("ERR reboot bad delay (1-10000 ms)\n");
            return;
        }
    }
    /* Reply BEFORE arming, and flush: after this the console is gone. */
    DIAG("OK reboot in_ms=%ld (this port will drop; it returns if main boots "
         "normally, and does NOT if main lands in BOOTSEL)\n", ms);
    /* No flush helper exists, and USB CDC needs the stack serviced for the
     * reply to actually leave. The delay below is the flush: it is >= 1 ms and
     * the reply is short, so it is out long before the reset lands. Do not
     * shorten this to zero. */
    sleep_ms(50);
    watchdog_reboot(0u, 0u, (uint32_t)ms);
    for (;;) { tight_loop_contents(); }
}

/* `fpga` | `fpga clk <div_int> [div_frac]`
 *
 * NOTE the plain form reports FWOG_FPGA_CLK_HZ, a COMPILE-TIME CONSTANT. It is
 * what the divider targets, not a measurement -- nothing on this board has
 * ever measured the clock the iCE40 actually receives, and reading that number
 * as evidence would be a mistake.
 *
 * `fpga clk` re-drives PIN_FPGA_CLK with an explicit divider and reloads the
 * bitstream, so the gateware starts on the new clock rather than having it
 * change underneath a running design. It exists to make the clock a variable:
 * this BSP runs the gateware at 25 MHz against a 31.25 MHz synthesised design
 * point (fpga_clk.h), and that deviation is a live suspect for the breakout
 * direction change being accepted everywhere and never reaching the pins. */
static void cmd_fpga(int argc, char **argv) {
    if (argc >= 3 && streq(argv[1], "clk")) {
        bool oki, okf = true;
        const long di = parse_long(argv[2], &oki);
        long df = 0;
        if (argc >= 4) df = parse_long(argv[3], &okf);
        if (!oki || !okf || di < 1 || di > 0xFFFFFF || df < 0 || df > 255) {
            DIAG("ERR fpga clk usage: fpga clk <div_int 1..> [div_frac 0-255]\n");
            return;
        }
        const uint32_t hz = fwog_fpga_clk_start_div((uint32_t)di, (uint8_t)df);
        const bool reloaded = fwog_ice40_load_default();
        DIAG("OK fpga clk hz=%u div=%ld+%ld/256 reloaded=%d cdone=%d\n",
             (unsigned)hz, di, df, (int)reloaded,
             (int)gpio_get(PIN_FPGA_DONE));
        return;
    }
    DIAG("OK fpga cdone=%d clk=%u Hz (CONSTANT, not measured)\n",
         (int)gpio_get(PIN_FPGA_DONE), (unsigned)FWOG_FPGA_CLK_HZ);
}

/* iodir default | iodir <dirword hex> [gp26 gp27]
 *
 * Drives fwog_io_dir_apply() (gpio/breakout.h), the one function on this
 * board that ever changes a breakout line's direction: it opens the
 * expander's IO_CONFIG window over the link, writes and reads back the
 * FPGA's direction register, applies the RP2040's own pads, then closes the
 * window -- see common/io_seq.h's ordering note. `result` is the raw
 * fwog_io_result_t (0=OK .. 4=FWOG_IO_ERR_FPGA_VERIFY, 5=FWOG_IO_ERR_EXPANDER,
 * 6=FWOG_IO_ERR_UNSUPPORTED) and is reported unconditionally, success or
 * not, because the number IS the answer this command exists to produce.
 * `fully_applied` (breakout.h's fwog_io_dir_last_fully_applied()) is also
 * always reported: on FWOG_IO_ERR_EXPANDER the FPGA and pads have already
 * moved even though the call failed, so an operator recovering from a
 * failure needs to know whether the board's last-recorded config actually
 * matches all three surfaces before building the next command from it. */
/* `iodir probe` -- print the FPGA's direction register instead of a verdict.
 *
 * `iodir` reports FWOG_IO_ERR_FPGA_VERIFY (result=4) when the readback
 * disagrees, which says the FPGA is not holding what we wrote but not how it
 * differs. These are the bytes that tell the two candidate causes apart:
 * all 0x00 / all 0xFF means nothing is driving MISO, structured-but-wrong
 * means the transaction works and the value is not sticking, and the seed
 * 0xA5/0x5A surviving would mean fwog_fpga_dir_read() did not write both
 * bytes at all. Comparing `open` against `pre`/`post` says whether the
 * IO_CONFIG window changes anything, without needing a second run.
 *
 * Changes no pad and no shifter direction -- see fwog_io_dir_probe()'s
 * contract in gpio/breakout.h. */
static void cmd_iodir_probe(void) {
    /* The CURRENT configuration, not fwog_io_cfg_default().
     *
     * This used to probe with the default config, and because
     * fwog_io_dir_probe() WRITES the word it is probing with, running `iodir
     * probe` silently reverted the FPGA's direction register to the default
     * and left it there. On 2026-07-28 that produced a full day of wrong
     * conclusions: every "the direction change does not reach the pins" test
     * had a probe between setting the direction and driving the pin, so the
     * instrument was destroying the state it was meant to observe. Probing
     * with the config already in force makes the write a no-op in effect.
     *
     * Note it still writes -- that is inherent to a probe that has to exercise
     * the write path. What it must never do again is write something DIFFERENT
     * from what is currently applied. */
    const fwog_io_cfg_t c = *fwog_io_dir_last();

    fwog_io_dir_probe_t p;
    fwog_io_dir_probe(&c, &p);

    DIAG("OK iodir probe want=%02X%02X pre=%02X%02X open_pre=%02X%02X "
         "open=%02X%02X post=%02X%02X enable_ok=%d disable_ok=%d\n",
         p.want[0], p.want[1], p.pre[0], p.pre[1],
         p.open_pre[0], p.open_pre[1],
         p.open_rd[0], p.open_rd[1], p.post[0], p.post[1],
         (int)p.enable_ok, (int)p.disable_ok);
}

/* iopin | iopin <gpio> 0|1 | iopin <gpio> read
 *
 * The ONLY way to set a breakout line's LEVEL -- `iodir` sets DIRECTION and
 * nothing in the tree set levels before this. Parsing only; every refusal
 * rule lives in fwog_io_pin_check_drive() (common/io_pins.h) so it is
 * host-tested rather than trusted to a bench run.
 *
 * `result` is the raw fwog_io_pin_result_t (0=OK, 1=NOT_BREAKOUT,
 * 2=NOT_OUTPUT, 3=IO_CONFIG_ACTIVE), reported unconditionally, success or
 * not, exactly as cmd_iodir does -- the number is the answer. */
static void cmd_iopin_list(void) {
    const fwog_io_cfg_t *cfg = fwog_io_dir_last();
    for (size_t i = 0; i < fwog_io_pin_count(); i++) {
        const fwog_io_pin_t *p = fwog_io_pin_at(i);
        bool lvl = false;
        (void)fwog_io_pin_read(p->gpio, &lvl);
        DIAG("  . hdr%-2u gpio%-2u %-9s %-3s level=%d\n",
             (unsigned)p->header_pin, (unsigned)p->gpio, p->name,
             fwog_io_pin_is_output(cfg, p->gpio) ? "out" : "in", (int)lvl);
    }
    DIAG("OK iopin count=%u io_config=%d\n",
         (unsigned)fwog_io_pin_count(), (int)fwog_io_config_state());
}

static void cmd_iopin(int argc, char **argv) {
    if (argc < 2) { cmd_iopin_list(); return; }

    bool okg;
    const long g = parse_long(argv[1], &okg);
    if (!okg || g < 0 || g > 29) {
        DIAG("ERR iopin bad gpio\n");
        return;
    }

    if (argc >= 3 && streq(argv[2], "read")) {
        bool lvl = false;
        const fwog_io_pin_result_t r = fwog_io_pin_read((unsigned)g, &lvl);
        DIAG("OK iopin gpio=%ld read result=%d level=%d\n",
             g, (int)r, (int)lvl);
        return;
    }

    if (argc < 3) {
        DIAG("ERR iopin usage: iopin | iopin <gpio> 0|1 | iopin <gpio> read\n");
        return;
    }

    bool okv;
    const long v = parse_long(argv[2], &okv);
    if (!okv || (v != 0 && v != 1)) {
        DIAG("ERR iopin bad level (0 or 1)\n");
        return;
    }

    const fwog_io_pin_result_t r = fwog_io_pin_drive((unsigned)g, v != 0);
    DIAG("OK iopin gpio=%ld drive=%ld result=%d io_config=%d\n",
         g, v, (int)r, (int)fwog_io_config_state());
}

/* i2cpin | i2cpin <sda|scl> <low|release>
 *
 * The breakout I2C lines (GPIO 16/17, header pins 10/8), open-drain. There is
 * no way to drive one high -- see fwog_io_i2c_pull() -- because that is
 * contention on this bus, not a test.
 *
 * Both levels are reported on every call, not just the one being changed:
 * "did the OTHER line move" is the crosstalk check, and a command that only
 * reported the line it touched could not answer it. */
static void cmd_i2cpin(int argc, char **argv) {
    unsigned sda = FWOG_IO_I2C_SDA_GPIO, scl = FWOG_IO_I2C_SCL_GPIO;

    if (argc >= 3) {
        unsigned g;
        if (!fwog_io_i2c_line(argv[1], &g)) {
            DIAG("ERR i2cpin bad line (sda or scl)\n");
            return;
        }
        bool low;
        if (streq(argv[2], "low"))          low = true;
        else if (streq(argv[2], "release")) low = false;
        else {
            DIAG("ERR i2cpin bad action (low or release)\n");
            return;
        }
        uint32_t rise = FWOG_I2C_RISE_NONE;
        const fwog_io_pin_result_t r = fwog_io_i2c_pull(g, low, &rise);
        if (r != FWOG_IO_PIN_OK) {
            DIAG("OK i2cpin result=%d\n", (int)r);
            return;
        }
        if (!low) {
            /* -1 means it never rose within FWOG_I2C_RISE_TIMEOUT_US. That is
               reported, not hidden: a line held low is a real state. */
            DIAG("  . %s released, rise_us=%ld\n", argv[1],
                 rise == FWOG_I2C_RISE_NONE ? -1L : (long)rise);
        }
    } else if (argc == 2) {
        DIAG("ERR i2cpin usage: i2cpin | i2cpin <sda|scl> <low|release>\n");
        return;
    }

    bool lsda = false, lscl = false;
    (void)fwog_io_i2c_sense(sda, &lsda);
    (void)fwog_io_i2c_sense(scl, &lscl);
    DIAG("OK i2cpin sda=%d scl=%d\n", (int)lsda, (int)lscl);
}

/* `iodir window <0|1>` -- open or close the expander's IO_CONFIG window and
 * LEAVE it that way.
 *
 * Deliberately a state-setter and NOT a measurement: with the window held
 * open, the ordinary `iopin` commands do the measuring, so the result rests on
 * the path already proven by the hardware record rather than on a bespoke function
 * that would have to be trusted on its own terms. That bundling is exactly
 * what made its predecessor (`iodir live`) unusable -- it could not see a
 * signal that was demonstrably present, and there was no way to tell whether
 * the bus was gated or the function was wrong.
 *
 * Open question it exists for: is the breakout bus live at all while IO_CONFIG
 * is asserted, or are the shifters gated during reconfiguration? Direction
 * changes themselves are settled -- they work, window closed.
 *
 * CLOSE IT WHEN DONE. An open window lets a later expander write disturb
 * directions (io_seq.h), and the FPGA SPI group refuses to be driven while the
 * state is anything but DISABLED. */
static void cmd_iodir_window(int argc, char **argv) {
    if (argc < 3) {
        DIAG("ERR iodir window usage: iodir window <0|1>\n");
        return;
    }
    bool okv;
    const long v = parse_long(argv[2], &okv);
    if (!okv || (v != 0 && v != 1)) {
        DIAG("ERR iodir window bad value (0=close 1=open)\n");
        return;
    }
    const bool acked = fwog_io_config_window(v != 0);
    DIAG("OK iodir window requested=%ld acked=%d io_config=%d%s\n",
         v, (int)acked, (int)fwog_io_config_state(),
         acked ? "" : "  (NOT acked -- state is UNKNOWN, not unchanged)");
}

static void cmd_iodir(int argc, char **argv) {
    fwog_io_cfg_t c = *fwog_io_dir_last();

    if (argc >= 2 && streq(argv[1], "window")) {
        cmd_iodir_window(argc, argv);
        return;
    }

    if (argc >= 2 && streq(argv[1], "probe")) {
        cmd_iodir_probe();
        return;
    }

    if (argc >= 2 && streq(argv[1], "default")) {
        fwog_io_cfg_default(&c);
    } else if (argc >= 2) {
        bool okw;
        const long w = parse_long(argv[1], &okw);
        if (!okw || w < 0 || w > 0xFFFF) {
            DIAG("ERR iodir bad dirword\n");
            return;
        }
        fwog_io_unpack_dirword((uint16_t)w, &c);
        if (argc >= 4) {
            bool ok26, ok27;
            const long g26 = parse_long(argv[2], &ok26);
            const long g27 = parse_long(argv[3], &ok27);
            if (!ok26 || !ok27) {
                DIAG("ERR iodir bad gp26/gp27\n");
                return;
            }
            c.gpio26_out = g26 != 0;
            c.gpio27_out = g27 != 0;
        }
    } else {
        DIAG("ERR iodir usage: iodir default | iodir probe | "
             "iodir <word> [gp26 gp27]\n");
        return;
    }

    fwog_io_result_t r = fwog_io_dir_apply(&c);
    DIAG("OK iodir result=%d dirword=0x%04X gp26=%d gp27=%d fully_applied=%d\n",
         (int)r, (unsigned)fwog_io_pack_dirword(&c),
         (int)c.gpio26_out, (int)c.gpio27_out,
         (int)fwog_io_dir_last_fully_applied());
}

static void print_heartbeat(fwog_display_result_t d) {
    uint8_t p0 = 0xFFu, v0 = 0xFFu, p1 = 0xFFu, v1 = 0xFFu;
    bool ok0 = false, ok1 = false;
    if (s_bound) {
        ok0 = probe_one(0, &p0, &v0);
        ok1 = probe_one(1, &p1, &v1);
    }
    /* The chip status byte comes back on every SPI transaction; reading it
       here costs nothing extra and shows the state machine is alive rather
       than merely that two registers hold the right constants. */
    cc1101_status_t st0 = {0}, st1 = {0};
    if (s_bound) {
        st0 = cc1101_strobe(&s_radio[0], CC1101_STROBE_SNOP);
        st1 = cc1101_strobe(&s_radio[1], CC1101_STROBE_SNOP);
    }
    DIAG("HB display=%s "
         "cs0=%s partnum=0x%02X version=0x%02X state=%s rdyn=%u "
         "cs1=%s partnum=0x%02X version=0x%02X state=%s rdyn=%u up_ms=%u\n",
         fwog_display_result_text(d),
         okstr(ok0), (unsigned)p0, (unsigned)v0,
         st0.ok ? state_name(st0.state) : "err", (unsigned)st0.chip_rdy,
         okstr(ok1), (unsigned)p1, (unsigned)v1,
         st1.ok ? state_name(st1.state) : "err", (unsigned)st1.chip_rdy,
         (unsigned)to_ms_since_boot(get_absolute_time()));
}

/* ---- Filesystem ----
 *
 * The pattern written by `fs write` and checked by `fs read` is deliberately
 * position-dependent (byte i is (i*31+i/251) & 0xFF, which has a period longer
 * than any sector or cluster). A constant fill would pass even if the disk
 * layer mapped every LBA to the same sector, which is precisely the geometry
 * bug most worth catching on a board. */
static uint8_t fs_pattern_byte(uint32_t i) {
    return (uint8_t)((i * 31u + i / 251u) & 0xFFu);
}

/* Static, not stack: main's default PICO_STACK_SIZE is 2048. */
static uint8_t s_fs_buf[1024];

static void cmd_fs_info(void) {
    if (!fwog_fs_mount()) { DIAG("ERR fs mount-failed\n"); return; }
    uint64_t freeb = 0u, totalb = 0u;
    if (!fwog_fs_volume_info(&freeb, &totalb)) {
        DIAG("ERR fs info-failed\n");
        return;
    }
    DIAG("OK fs info free_kb=%u total_kb=%u offset=0x%08x sectors=%u "
         "sector_bytes=%u\n",
         (unsigned)(freeb / 1024u), (unsigned)(totalb / 1024u),
         (unsigned)FWOG_FS_FLASH_OFFSET, (unsigned)FWOG_FS_SECTOR_COUNT,
         (unsigned)FWOG_FS_SECTOR_SIZE);
}

static void cmd_fs_ls(int argc, char **argv) {
    if (!fwog_fs_mount()) { DIAG("ERR fs mount-failed\n"); return; }
    const char *dir = (argc >= 3) ? argv[2] : "/";
    char name[64];
    bool is_dir = false;
    unsigned n = 0u;
    for (unsigned i = 0u; i < 128u; i++) {
        if (!fwog_fs_dir_entry(dir, i, name, sizeof(name), &is_dir)) break;
        DIAG("  [%u] %s%s\n", i, name, is_dir ? "/" : "");
        n++;
    }
    DIAG("OK fs ls dir=%s entries=%u\n", dir, n);
}

static void cmd_fs_write(int argc, char **argv) {
    if (argc < 4) { DIAG("ERR fs usage: fs write <path> <kb>\n"); return; }
    if (!fwog_fs_mount()) { DIAG("ERR fs mount-failed\n"); return; }
    bool okk;
    const long kb = parse_long(argv[3], &okk);
    if (!okk || kb <= 0 || kb > 4096) {
        DIAG("ERR fs bad kb (1-4096)\n");
        return;
    }

    if (!fwog_fs_open(argv[2], true, false)) {
        DIAG("ERR fs open-failed\n");
        return;
    }

    const absolute_time_t t0 = get_absolute_time();
    uint32_t written = 0u;
    const uint32_t total = (uint32_t)kb * 1024u;
    bool ok = true;
    while (written < total) {
        for (unsigned i = 0u; i < sizeof(s_fs_buf); i++)
            s_fs_buf[i] = fs_pattern_byte(written + i);
        if (!fwog_fs_write(s_fs_buf, sizeof(s_fs_buf))) { ok = false; break; }
        written += (uint32_t)sizeof(s_fs_buf);
    }
    const bool closed = fwog_fs_close();
    const int64_t us = absolute_time_diff_us(t0, get_absolute_time());

    if (!ok || !closed) {
        DIAG("ERR fs write failed after %u bytes (volume full?)\n",
             (unsigned)written);
        return;
    }
    /* The elapsed time IS the watchdog-chunking evidence, and it is the only
       evidence this reply can carry: a watchdog reset mid-write would reboot
       main and this line would never be printed at all. So `ms` greater than
       FWOG_WATCHDOG_MS, in a reply you actually received, is the proof.

       board_watchdog_caused_reboot() is deliberately NOT reported here. It
       was, and it was a FALSE POSITIVE that briefly looked like a real
       finding on 2026-07-28: on RP2040 the bootrom's own UF2/BOOTSEL reset
       goes through the watchdog, so it reads 1 after any `fw flash` and says
       nothing whatever about this write. A field that is almost always 1 for
       an unrelated reason is worse than no field. See the hardware record. */
    DIAG("OK fs write path=%s bytes=%u ms=%u sectors=%u wdt_window_ms=%u\n",
         argv[2], (unsigned)written, (unsigned)(us / 1000),
         (unsigned)((written + FWOG_FS_SECTOR_SIZE - 1u) / FWOG_FS_SECTOR_SIZE),
         (unsigned)FWOG_WATCHDOG_MS);
}

static void cmd_fs_read(int argc, char **argv) {
    if (argc < 3) { DIAG("ERR fs usage: fs read <path>\n"); return; }
    if (!fwog_fs_mount()) { DIAG("ERR fs mount-failed\n"); return; }
    if (!fwog_fs_open(argv[2], false, false)) {
        DIAG("ERR fs open-failed\n");
        return;
    }

    const uint32_t size = fwog_fs_size();
    uint32_t pos = 0u;
    uint32_t bad = 0u;
    uint32_t first_bad = 0xFFFFFFFFu;
    for (;;) {
        size_t len = sizeof(s_fs_buf);
        if (!fwog_fs_read(s_fs_buf, &len)) { bad = 0xFFFFFFFFu; break; }
        if (len == 0u) break;
        for (size_t i = 0u; i < len; i++) {
            if (s_fs_buf[i] != fs_pattern_byte(pos + (uint32_t)i)) {
                if (first_bad == 0xFFFFFFFFu) first_bad = pos + (uint32_t)i;
                bad++;
            }
        }
        pos += (uint32_t)len;
    }
    (void)fwog_fs_close();

    DIAG("OK fs read path=%s size=%u read=%u mismatches=%u first_bad=%d\n",
         argv[2], (unsigned)size, (unsigned)pos, (unsigned)bad,
         (first_bad == 0xFFFFFFFFu) ? -1 : (int)first_bad);
}

static void cmd_fs_rm(int argc, char **argv) {
    if (argc < 3) { DIAG("ERR fs usage: fs rm <path>\n"); return; }
    if (!fwog_fs_mount()) { DIAG("ERR fs mount-failed\n"); return; }
    if (!fwog_fs_remove(argv[2])) { DIAG("ERR fs rm-failed\n"); return; }
    DIAG("OK fs rm path=%s\n", argv[2]);
}

static void cmd_fs_format(void) {
    /* No extra confirmation prompt here beyond the explicit subcommand name.
       The console has no way to read a second line mid-command, and inventing
       one for this alone would be a bigger surface than the risk -- `format`
       is already a word nobody types by accident. */
    if (!fwog_fs_format()) { DIAG("ERR fs format-failed\n"); return; }
    DIAG("OK fs format\n");
}

static void cmd_fs(int argc, char **argv) {
    if (argc < 2) {
        DIAG("ERR fs usage: fs info|ls|write|read|rm|format\n");
        return;
    }
    if      (streq(argv[1], "info"))   cmd_fs_info();
    else if (streq(argv[1], "ls"))     cmd_fs_ls(argc, argv);
    else if (streq(argv[1], "write"))  cmd_fs_write(argc, argv);
    else if (streq(argv[1], "read"))   cmd_fs_read(argc, argv);
    else if (streq(argv[1], "rm"))     cmd_fs_rm(argc, argv);
    else if (streq(argv[1], "format")) cmd_fs_format();
    else DIAG("ERR fs unknown subcommand '%s'\n", argv[1]);
}

static void process_line(char *line) {
    char *argv[10];
    int argc = 0;
    char *tok = strtok(line, " \t");
    while (tok && argc < 10) {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t");
    }
    if (argc == 0) return;

    const char *cmd = argv[0];
    if (streq(cmd, "help")) {
        cmd_help();
    } else if (streq(cmd, "radio")) {
        cmd_radio();
    } else if (streq(cmd, "reset")) {
        cmd_reset(argc, argv);
    } else if (streq(cmd, "rstat")) {
        cmd_rstat(argc, argv);
    } else if (streq(cmd, "rreg")) {
        cmd_rreg(argc, argv);
    } else if (streq(cmd, "rf")) {
        cmd_rf(argc, argv);
    } else if (streq(cmd, "loop")) {
        cmd_loop(argc, argv);
    } else if (streq(cmd, "pins")) {
        cmd_pins();
    } else if (streq(cmd, "flashcrc")) {
        cmd_flashcrc(argc, argv);
    } else if (streq(cmd, "reboot")) {
        cmd_reboot(argc, argv);
    } else if (streq(cmd, "fpga")) {
        cmd_fpga(argc, argv);
    } else if (streq(cmd, "fs")) {
        cmd_fs(argc, argv);
    } else if (streq(cmd, "i2cpin")) {
        cmd_i2cpin(argc, argv);
    } else if (streq(cmd, "iopin")) {
        cmd_iopin(argc, argv);
    } else if (streq(cmd, "iodir")) {
        cmd_iodir(argc, argv);
    } else {
        DIAG("ERR unknown command '%s'; try help\n", cmd);
    }
}

int main(void) {
    board_init();

    /* Required of every main-CPU app, and it performs board_release_display()
       itself at the point the announce window is guaranteed to be caught --
       so this app must NOT call that separately (AGENTS.md). */
    const fwog_display_result_t d = fwog_display_update_run();

    /* pico_stdio_usb drops output when no host is attached, so wait
       (bounded) for one -- kicking the watchdog, because this wait is longer
       than FWOG_WATCHDOG_MS and a bare spin here would reset the board. */
#if FWOG_DIAG == FWOG_DIAG_USB
    const absolute_time_t host_deadline = make_timeout_time_ms(5000);
    while (!stdio_usb_connected() && !time_reached(host_deadline)) {
        board_watchdog_kick();
        tight_loop_contents();
    }
    board_watchdog_kick();
    sleep_ms(300);
#endif

    DIAG("\n[bench_main] alive\n");
    DIAG("[bench_main] display: %s\n", fwog_display_result_text(d));

    /* One bus for both chips (they share MOSI/MISO/SCLK), then one bind per
       chip select. board_init() has already parked both chip selects high as
       a whole-CPU invariant (the hardware record), which cc1101_bind() relies on. */
    cc1101_bus_init(BENCH_RADIO_SPI_HZ);
    cc1101_bind(&s_radio[0], CC1101_RADIO_CS0);
    cc1101_bind(&s_radio[1], CC1101_RADIO_CS1);
    s_bound = true;
    board_watchdog_kick();

    /* Report the probe once at boot as well as on every heartbeat: a console
       that attaches late is the normal case, but a console that attaches on
       time should not have to wait for a heartbeat either. */
    cmd_pins();
    cmd_radio();

    DIAG("[bench_main] ready; type 'help' for commands\n");

    static char line_buf[128];
    unsigned line_len = 0u;
    absolute_time_t next_hb = make_timeout_time_ms(2000);

    while (true) {
        board_watchdog_kick();      /* required: see watchdog.h */

        int c;
        while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
            if (c == '\r' || c == '\n') {
                if (line_len > 0u) {
                    line_buf[line_len] = '\0';
                    process_line(line_buf);
                    line_len = 0u;
                }
            } else if (c == '\b' || c == 0x7F) {
                if (line_len > 0u) line_len--;
            } else if (line_len < sizeof(line_buf) - 1u) {
                line_buf[line_len++] = (char)c;
            }
            /* else: line too long, extra characters silently dropped. */
        }

        if (time_reached(next_hb)) {
            next_hb = make_timeout_time_ms(2000);
            print_heartbeat(d);
        }
    }
}
