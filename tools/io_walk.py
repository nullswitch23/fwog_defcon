#!/usr/bin/env python3
"""Test the FreeWili 1 breakout header's OUTPUT pins.

Drives each output on the breakout connector and reports which ones actually
moved. Designed against a Maestro Debug Orca
(docs.freewili.com/extending-with-orcas/maestro-debug-orca/), whose per-IO LEDs
are what the operator watches in phase 1.

Requires apps/bench/main's `iopin` command (added 2026-07-28).

------------------------------------------------------------------------------
THE HEADER
------------------------------------------------------------------------------

From docs.freewili.com/gpio/gpio-pinout/. The directions there match this
BSP's fwog_io_cfg_default() on all nine direction-controlled lines.

    hdr  1  GPIO13  spi_cs     OUT        hdr 11  GPIO11  uart_rts   OUT
    hdr  3  GPIO27  gpio27     OUT        hdr 12  GPIO12  spi_rx     in
    hdr  5  GPIO09  uart_rx    in         hdr 13  GPIO15  spi_tx     OUT
    hdr  7  GPIO10  uart_cts   in         hdr 14  GPIO26  gpio26     in
    hdr  9  GPIO08  uart_tx    OUT        hdr 15  GPIO14  spi_sclk   OUT
                                          hdr 17  GPIO25  gpio25     OUT

Seven outputs. All seven are ALREADY outputs in the default config, so this
tool never changes a direction -- the never-verified direction-CHANGE path
(the hardware record's residual list) stays out of this entirely.

------------------------------------------------------------------------------
PIN 4 IS THE ONE THAT WILL WASTE YOUR SESSION
------------------------------------------------------------------------------

Header pin 4 (`V PINS IN`) MUST carry 1.1-5.5 V or NO breakout GPIO functions
at all. On the Orca that is the voltage jumper: 2-4 for 5 V, 4-6 for 3.3 V.

With no IO voltage every LED stays dark -- which is indistinguishable, from the
console, from firmware that is not driving anything. That is exactly why
phase 0 exists and why it is a HARD GATE.

------------------------------------------------------------------------------
THE MAESTRO DEBUG ORCA v1 HAS A UART WIRING ISSUE
------------------------------------------------------------------------------

Reported by the board owner on 2026-07-28, after this tool's first --inputs run
showed BOTH UART loopback pairs (uart_tx->uart_rx, uart_rts->uart_cts) reading
0 in every condition while spi_tx->spi_rx and gpio27->gpio26 tracked perfectly.

The FreeWili side was fine and had already been shown to be: GPIO 8 and GPIO 11
read back exactly what they were driven, so the driver pads work, and GPIO 12
and GPIO 26 sensed correctly, so the input path in general works.

If the UART pairs fail here, wire them WITHOUT the Orca before concluding
anything about the board. And note what happens if you unplug the Orca
entirely: header pin 4 loses its supply and EVERY line dies at once, which
looks far more alarming than it is. Bridge hdr 6 (3.3 V out) -> hdr 4 to feed
it, or keep the Orca attached for power.

------------------------------------------------------------------------------
THE OUTCOMES, NAMED IN ADVANCE
------------------------------------------------------------------------------

  ALL_OUTPUTS_DRIVE   -- every one of the seven was OBSERVED to behave.
  PARTIAL             -- observed pins behaved, but some were never observed.
                         Unobserved is not passing; those pins are unmeasured.
  CONTROL_PASSED_UNOBSERVED -- phase 0 passed and no pin was observed at all
                         (this is what --auto produces). It says header pin 4
                         has voltage and one output/input pair works. It does
                         NOT say anything about the seven outputs.
  SOME_STUCK          -- a NAMED subset failed. The names are the result.
  PATH_DEAD_OR_NO_VPINS -- phase 0's electrical control failed. Check the
                         voltage jumper before believing anything else. No LED
                         observation is interpreted.
  VOID                -- the console stopped answering, or a refusal made the
                         run meaningless. Interpret nothing.

Naming them before the run is the point. But note what this tool does NOT
claim, because a sibling tool overclaimed here and it cost a session
(the hardware record): an LED lighting says the line reached a level the LED and the
level shifter accept. It says NOTHING about the 24 mA drive rating, about
rise/fall time, or about behaviour at 5 V.

Usage:
    python tools/io_walk.py                  # phase 0 + walk + stuck check
    python tools/io_walk.py --auto           # no prompts; phase 0 + phase 2 only
    python tools/io_walk.py --dwell 1.5      # seconds per walk step
    python tools/io_walk.py --json out.json
"""
import argparse
import json
import re
import sys

# ---------------------------------------------------------------------------
# Pure logic. No serial, no board. Host-tested by tools/tests/test_io_walk.py.
# ---------------------------------------------------------------------------

# (gpio, header_pin, name). Header order, mirroring bsp/common/io_pins.c --
# tests/test_io_pins.c pins that table against the published pinout, and
# test_io_walk.py pins this one against the same numbers.
OUTPUTS = [
    (13,  1, "spi_cs"),
    (27,  3, "gpio27"),
    ( 8,  9, "uart_tx"),
    (11, 11, "uart_rts"),
    (15, 13, "spi_tx"),
    (14, 15, "spi_sclk"),
    (25, 17, "gpio25"),
]

# ---- the INPUT test's loopback pairs ----
#
# (driver_gpio, sense_gpio, driver_hdr, sense_hdr, label). Each input is paired
# with its natural protocol partner, which is also the most obvious physical
# wiring: TX->RX, RTS->CTS, MOSI->MISO. GPIO27->GPIO26 is the odd one out and
# is simply the two plain GPIO.
#
# These four cover EVERY input on the header. There is no fifth.
INPUT_PAIRS = [
    ( 8,  9,  9,  5, "uart_tx -> uart_rx"),
    (11, 10, 11,  7, "uart_rts -> uart_cts"),
    (15, 12, 13, 12, "spi_tx -> spi_rx"),
    (27, 26,  3, 14, "gpio27 -> gpio26"),
]

# Phase 0's electrical control. GPIO27 (hdr 3) drives, GPIO26 (hdr 14) senses.
# The safest pair on the board to bridge: both are FPGA-only lines with no
# expander direction bits, and output-into-input cannot contend.
LOOPBACK_DRIVE = 27
LOOPBACK_SENSE = 26

# fwog_io_pin_result_t, from bsp/common/io_pins.h. Kept in sync by
# test_io_walk.py, which asserts the names and values together.
PIN_RESULT = {
    0: "OK",
    1: "NOT_BREAKOUT",
    2: "NOT_OUTPUT",
    3: "IO_CONFIG_ACTIVE",
}

_KV = re.compile(r"([a-z_0-9]+)=(-?[0-9]+)")


def parse_iopin(line):
    """`OK iopin gpio=27 drive=1 result=0 io_config=1` -> dict, or None."""
    if not line or not line.startswith("OK iopin"):
        return None
    out = {k: int(v) for k, v in _KV.findall(line)}
    return out or None


def loopback_verdict(high_read, low_read):
    """Did the sensed pin follow the driven one?

    Both directions must track. Checking only the high case would pass a line
    that is stuck high -- which is precisely one of the faults this whole tool
    exists to find, so accepting it in the CONTROL would be self-defeating.
    """
    return high_read is True and low_read is False


def classify(loopback_ok, walk, stuck):
    """Return the named outcome.

    Order matters and phase 0 short-circuits: a run whose control failed says
    nothing about any LED, and reporting a pin verdict from it would be exactly
    the mistake the control exists to prevent.

    `walk`  -- {gpio: True/False/None}. None = not observed (--auto).
    `stuck` -- {gpio: True/False/None} from phase 2's all-low/all-high.
    """
    if loopback_ok is None:
        return {
            "outcome": "VOID",
            "reason": "phase 0 did not run or the console stopped answering; "
                      "nothing here is interpretable",
            "failed": [], "unobserved": [],
        }
    if not loopback_ok:
        return {
            "outcome": "PATH_DEAD_OR_NO_VPINS",
            "reason": "GPIO%d -> GPIO%d loopback did not track. Either header "
                      "pin 4 (V PINS IN) has no voltage -- check the Orca "
                      "jumper, 4-6 for 3.3 V -- or the output path is dead. "
                      "No LED observation is interpreted."
                      % (LOOPBACK_DRIVE, LOOPBACK_SENSE),
            "failed": [], "unobserved": [],
        }

    names = {g: n for g, _, n in OUTPUTS}
    failed = sorted([g for g, ok in walk.items() if ok is False] +
                    [g for g, ok in stuck.items()
                     if ok is False and not (walk.get(g) is False)])
    if failed:
        return {
            "outcome": "SOME_STUCK",
            "reason": "%d of %d outputs did not behave: %s"
                      % (len(failed), len(OUTPUTS),
                         ", ".join("GPIO%d (%s)" % (g, names.get(g, "?"))
                                   for g in failed)),
            "failed": failed, "unobserved": [],
        }

    # A pin nobody looked at is NOT a pin that passed.
    #
    # This is the whole ant_sweep.py lesson (the hardware record) applied to this
    # tool's own verdict: --auto observes no LED at all, and an earlier draft
    # of this function reported ALL_OUTPUTS_DRIVE from it -- the control
    # passing being silently promoted into a claim about seven pins. Confirmed
    # means confirmed; everything else gets named.
    confirmed = {g for g, ok in walk.items() if ok is True}
    unobserved = sorted(g for g, _, _ in OUTPUTS if g not in confirmed)

    if len(unobserved) == len(OUTPUTS):
        return {
            "outcome": "CONTROL_PASSED_UNOBSERVED",
            "reason": "the GPIO%d -> GPIO%d loopback passed, so header pin 4 "
                      "has IO voltage and at least one output and one input "
                      "path work. NO pin was observed, so nothing is claimed "
                      "about the seven outputs individually. Re-run without "
                      "--auto to observe them."
                      % (LOOPBACK_DRIVE, LOOPBACK_SENSE),
            "failed": [], "unobserved": unobserved,
        }
    if unobserved:
        return {
            "outcome": "PARTIAL",
            "reason": "the control passed and every observed output behaved, "
                      "but %d of %d were not observed: %s. They are neither "
                      "passing nor failing -- they are unmeasured."
                      % (len(unobserved), len(OUTPUTS),
                         ", ".join("GPIO%d (%s)" % (g, names.get(g, "?"))
                                   for g in unobserved)),
            "failed": [], "unobserved": unobserved,
        }
    return {
        "outcome": "ALL_OUTPUTS_DRIVE",
        "reason": "the loopback control passed and all %d outputs were "
                  "observed to behave. Each line reached a level its LED and "
                  "level shifter accept -- this says nothing about the 24 mA "
                  "drive rating, rise/fall time, or 5 V operation."
                  % len(OUTPUTS),
        "failed": [], "unobserved": [],
    }


def classify_inputs(matrix, all_low=None, all_high=None, pairs=None):
    """Verdict for the 4x4 input loopback matrix.

    `matrix[driver_gpio][sense_gpio]` is True/False/None, taken with that ONE
    driver high and every other driver low. The expected result is the identity
    matrix: each input follows its own partner and nothing else.

    Testing the whole matrix rather than four independent pairs is the point.
    Four pairwise tests would pass happily on a board where two adjacent lines
    are shorted together, because each input would still follow SOMETHING at
    the right moment. Only the off-diagonal shows that up -- and adjacent
    header pins are exactly where a solder bridge or a misplaced jumper lands
    (hdr 12 and 13 are the spi_rx/spi_tx pair, physically next to each other).

    `all_low` / `all_high` are the sensed values with every driver low and
    every driver high; they catch an input stuck independent of any driver.
    """
    # Only the pairs actually JUMPERED are under test. An absent jumper is not
    # a dead pin, and reporting it as one is how a tool invents a hardware
    # fault -- this function did exactly that on 2026-07-28 when two of four
    # jumpers were removed between runs.
    pairs = INPUT_PAIRS if pairs is None else pairs
    partner = {d: s for d, s, _, _, _ in pairs}
    label = {d: l for d, _, _, _, l in pairs}
    senses = [s for _, s, _, _, _ in pairs]

    unreadable, dead, crosstalk = [], [], []
    for d, s, _, _, _ in pairs:
        row = matrix.get(d, {})
        for sg in senses:
            v = row.get(sg)
            if v is None:
                unreadable.append((d, sg))
            elif sg == partner[d] and v is not True:
                dead.append(d)
            elif sg != partner[d] and v is not False:
                crosstalk.append((d, sg))

    stuck = []
    for sg in senses:
        if all_low is not None and all_low.get(sg) is True:
            stuck.append((sg, "high with every driver low"))
        if all_high is not None and all_high.get(sg) is False:
            stuck.append((sg, "low with every driver high"))

    if unreadable:
        return {"outcome": "VOID",
                "reason": "%d cell(s) could not be read; interpret nothing"
                          % len(unreadable),
                "dead": [], "crosstalk": [], "stuck": []}
    if crosstalk:
        return {"outcome": "CROSSTALK",
                "reason": "an input followed a driver that is not its partner: "
                          "%s. A shorted pair or a misplaced jumper -- check "
                          "the wiring before reading anything else here."
                          % ", ".join("GPIO%d moved GPIO%d" % (d, s)
                                      for d, s in crosstalk),
                "dead": sorted(set(dead)), "crosstalk": crosstalk,
                "stuck": stuck}
    if dead or stuck:
        parts = []
        if dead:
            parts.append("did not follow its partner: "
                         + ", ".join("%s" % label[d] for d in sorted(set(dead))))
        if stuck:
            parts.append("stuck: "
                         + ", ".join("GPIO%d %s" % (g, w) for g, w in stuck))
        return {"outcome": "SOME_INPUTS_DEAD",
                "reason": "; ".join(parts),
                "dead": sorted(set(dead)), "crosstalk": [], "stuck": stuck}
    return {"outcome": "ALL_INPUTS_SENSE",
            "reason": "all %d jumpered inputs followed their own partner and "
                      "no other, "
                      "in both states. Each input distinguished a driven high "
                      "from a driven low through its level shifter -- this says "
                      "nothing about input threshold voltages, leakage, or "
                      "behaviour with the line left floating, none of which "
                      "were measured." % len(pairs),
            "dead": [], "crosstalk": [], "stuck": []}


# ---------------------------------------------------------------------------
# I/O. Everything below needs a board.
# ---------------------------------------------------------------------------

def run_inputs(verbose=True):
    """Drive each pair's output alone and read ALL inputs. Needs the four
    jumpers: hdr 9->5, hdr 11->7, hdr 13->12, hdr 3->14."""
    import pathlib
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    from bench import Bench

    m = Bench(cpu="main")
    matrix, all_low, all_high = {}, {}, {}
    drivers = [d for d, _, _, _, _ in INPUT_PAIRS]
    senses = [s for _, s, _, _, _ in INPUT_PAIRS]

    def log(msg):
        if verbose:
            print(msg, file=sys.stderr, flush=True)

    def drive(g, v):
        r, _ = m.send("iopin %d %d" % (g, v), wait=6.0)
        p = parse_iopin(r)
        if p is None or p.get("result") != 0:
            log("  !! iopin %d %d -> %s" % (g, v, r))

    def sense(g):
        r, _ = m.send("iopin %d read" % g, wait=6.0)
        p = parse_iopin(r)
        if p is None or p.get("result") != 0:
            log("  !! iopin %d read -> %s" % (g, r))
            return None
        return bool(p.get("level"))

    try:
        r, _ = m.send("iodir default", wait=10.0)
        log("iodir default -> %s" % r)
        if r is None or not r.startswith("OK"):
            return matrix, all_low, all_high

        for d in drivers:
            for g in drivers:
                drive(g, 1 if g == d else 0)
            matrix[d] = {s: sense(s) for s in senses}
            log("  drive GPIO%-2d high alone -> %s"
                % (d, "  ".join("g%d=%s" % (s, matrix[d][s]) for s in senses)))

        for g in drivers:
            drive(g, 0)
        all_low = {s: sense(s) for s in senses}
        log("  ALL drivers low  -> %s"
            % "  ".join("g%d=%s" % (s, all_low[s]) for s in senses))

        for g in drivers:
            drive(g, 1)
        all_high = {s: sense(s) for s in senses}
        log("  ALL drivers high -> %s"
            % "  ".join("g%d=%s" % (s, all_high[s]) for s in senses))
    except KeyboardInterrupt:
        log("\n!! interrupted")
    finally:
        try:
            r, _ = m.send("iodir default", wait=10.0)
            log("restore: %s" % r)
        finally:
            m.close()
    return matrix, all_low, all_high

def run(dwell=1.0, auto=False, verbose=True):
    import pathlib
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    from bench import Bench

    m = Bench(cpu="main")
    walk, stuck = {}, {}
    loopback_ok = None

    def log(msg):
        if verbose:
            print(msg, file=sys.stderr, flush=True)

    def drive(gpio, level):
        reply, _ = m.send("iopin %d %d" % (gpio, level), wait=6.0)
        p = parse_iopin(reply)
        if p is None or p.get("result") != 0:
            log("  !! iopin %d %d -> %s" % (gpio, level, reply))
        return p

    def sense(gpio):
        reply, _ = m.send("iopin %d read" % gpio, wait=6.0)
        p = parse_iopin(reply)
        if p is None or p.get("result") != 0:
            log("  !! iopin %d read -> %s" % (gpio, reply))
            return None
        return bool(p.get("level"))

    try:
        # Put all three surfaces in a known state. This also establishes
        # IO_CONFIG=DISABLED in main's own tracking, which is what lets the
        # FPGA's SPI group (12/13/14/15) be driven at all -- see io_pins.h.
        reply, _ = m.send("iodir default", wait=10.0)
        log("iodir default -> %s" % reply)
        if reply is None or not reply.startswith("OK"):
            log("!! could not establish a known direction state")
            return None, walk, stuck

        # ---- phase 0: the electrical control, one jumper hdr3 -> hdr14 ----
        log("\n== phase 0: loopback control, GPIO%d (hdr 3) -> GPIO%d (hdr 14)"
            % (LOOPBACK_DRIVE, LOOPBACK_SENSE))
        drive(LOOPBACK_DRIVE, 1)
        hi = sense(LOOPBACK_SENSE)
        drive(LOOPBACK_DRIVE, 0)
        lo = sense(LOOPBACK_SENSE)
        loopback_ok = loopback_verdict(hi, lo)
        log("   drive 1 -> sensed %s ; drive 0 -> sensed %s  => %s"
            % (hi, lo, "PASS" if loopback_ok else "FAIL"))
        if not loopback_ok:
            return loopback_ok, walk, stuck

        # ---- phase 1: walking ones ----
        if not auto:
            log("\n== phase 1: walking ones. Exactly ONE LED should be lit at "
                "a time.")
            for gpio, hdr, name in OUTPUTS:
                for g2, _, _ in OUTPUTS:
                    drive(g2, 1 if g2 == gpio else 0)
                ans = input("  hdr %-2d GPIO%-2d %-9s driven HIGH alone -- "
                            "lit? [y/n/s] " % (hdr, gpio, name)).strip().lower()
                walk[gpio] = True if ans.startswith("y") else (
                    None if ans.startswith("s") else False)

        # ---- phase 2: all-low then all-high ----
        # Catches a pin stuck in one state, which the walk alone does not: a
        # permanently-lit LED looks correct in six of seven walk steps.
        log("\n== phase 2: all outputs LOW, then all HIGH")
        for g, _, _ in OUTPUTS:
            drive(g, 0)
        if not auto:
            a = input("  ALL LEDs off? [y/n] ").strip().lower()
            if not a.startswith("y"):
                bad = input("  which GPIOs are still lit? (space separated) ")
                for tok in bad.split():
                    try:
                        stuck[int(tok)] = False
                    except ValueError:
                        pass
        for g, _, _ in OUTPUTS:
            drive(g, 1)
        if not auto:
            a = input("  ALL LEDs on? [y/n] ").strip().lower()
            if not a.startswith("y"):
                bad = input("  which GPIOs are still dark? (space separated) ")
                for tok in bad.split():
                    try:
                        stuck[int(tok)] = False
                    except ValueError:
                        pass
    except KeyboardInterrupt:
        log("\n!! interrupted")
    finally:
        # Restore documented idle levels. These lines drive a physical header,
        # so leaving them wherever the last step happened to stop is not
        # acceptable -- and `iodir default` re-applies pads on all three
        # surfaces, which is exactly the restore we want.
        try:
            reply, _ = m.send("iodir default", wait=10.0)
            log("\nrestore: %s" % reply)
        finally:
            m.close()
    return loopback_ok, walk, stuck


def main_cli(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dwell", type=float, default=1.0,
                    help="seconds to hold each walk step (default 1.0)")
    ap.add_argument("--auto", action="store_true",
                    help="no prompts: phase 0 and phase 2 only, no LED reading")
    ap.add_argument("--inputs", action="store_true",
                    help="test the four INPUT pins by loopback instead "
                         "(needs jumpers hdr 9->5, 11->7, 13->12, 3->14)")
    ap.add_argument("--json", help="write raw results here")
    args = ap.parse_args(argv)

    if args.inputs:
        matrix, all_low, all_high = run_inputs()
        verdict = classify_inputs(matrix, all_low, all_high)
        senses = [s for _, s, _, _, _ in INPUT_PAIRS]
        print()
        print("| driver high alone | " +
              " | ".join("g%d" % s for s in senses) + " |")
        print("|---|" + "---|" * len(senses))
        for d, _, dh, _, lab in INPUT_PAIRS:
            row = matrix.get(d, {})
            print("| GPIO%d (hdr %d) | %s |"
                  % (d, dh, " | ".join(str(row.get(s)) for s in senses)))
        print("| _all low_ | %s |"
              % " | ".join(str(all_low.get(s)) for s in senses))
        print("| _all high_ | %s |"
              % " | ".join(str(all_high.get(s)) for s in senses))
        print()
        print("OUTCOME: %s" % verdict["outcome"])
        print(verdict["reason"])
        if args.json:
            with open(args.json, "w", encoding="utf-8") as f:
                json.dump({"matrix": {str(k): {str(a): b for a, b in v.items()}
                                      for k, v in matrix.items()},
                           "all_low": {str(k): v for k, v in all_low.items()},
                           "all_high": {str(k): v for k, v in all_high.items()},
                           "verdict": verdict}, f, indent=2)
        return 0 if verdict["outcome"] == "ALL_INPUTS_SENSE" else 2

    loopback_ok, walk, stuck = run(dwell=args.dwell, auto=args.auto)
    verdict = classify(loopback_ok, walk, stuck)

    print()
    print("| hdr | gpio | name | walk | stuck-check |")
    print("|---|---|---|---|---|")
    for gpio, hdr, name in OUTPUTS:
        def mark(v):
            return {True: "PASS", False: "FAIL", None: "-"}[v]
        print("| %d | %d | %s | %s | %s |"
              % (hdr, gpio, name, mark(walk.get(gpio)), mark(stuck.get(gpio))))
    print()
    print("OUTCOME: %s" % verdict["outcome"])
    print(verdict["reason"])

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"walk": {str(k): v for k, v in walk.items()},
                       "stuck": {str(k): v for k, v in stuck.items()},
                       "loopback_ok": loopback_ok,
                       "verdict": verdict}, f, indent=2)
    return 0 if verdict["outcome"] == "ALL_OUTPUTS_DRIVE" else 2


if __name__ == "__main__":
    raise SystemExit(main_cli())
