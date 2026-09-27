#!/usr/bin/env python3
"""P3 -- localise the 315 MHz receive failure by sweeping the antenna matrix.

NO FIRMWARE CHANGE. Everything this needs already exists: apps/bench/display's
`ant <r1 0-3> <r2 0-3>` sets the two antenna fields INDEPENDENTLY, and
apps/bench/main's `loop <tx> <count>` reports `keyed=` and `received=`
separately -- the split that made fact 36's "the transmitter keys but nothing
arrives" conclusion possible in the first place. This is host orchestration
across BOTH CPUs at once, which is the one thing no existing tool does.

------------------------------------------------------------------------------
WHY THIS EXISTS -- the three gaps in the 2026-07-28 sweep (the hardware record)
------------------------------------------------------------------------------

What is already known, from one board:

  - At 315 MHz the transmitter KEYS correctly, 6 of 6, with GDO0 showing both
    the sync edge and the end edge. The synthesiser locked and the PA ran, so
    cc1101_set_frequency()'s per-band FSCTRL0/TEST0/FSCAL2 calibration is fine
    and the low band is not where the fault is.
  - Nothing is received on any path tried: isolation 0/10, 200 MHz 0/10,
    400 MHz 0/10.
  - FWOG_ANT_400MHZ and FWOG_ANT_900MHZ are positively confirmed at their own
    carriers. FWOG_ANT_200MHZ leaves the isolation state but HAS NEVER BEEN
    SHOWN TO PASS ANYTHING.

Three things that experiment did not test, each of which could independently
explain the result:

  1. The two antenna fields were never set to DIFFERENT values. Every cell in
     fact 36's tables sets radio1_ant and radio2_ant the same, or sweeps one
     while the other sits at 400 MHz. A 4x4 matrix has sixteen cells; the
     existing data covers a handful, none of them a genuine cross-pairing at
     315 MHz.
  2. The 900 MHz path was never tried AT 315 MHz. That row reads
     "0/10, 0/10, 0/10, --". The dash is not a result. If the band labels are
     wrong -- and the reference's naming is known-unreliable -- the working
     low-band path could be the one labelled 900 MHz.
  3. Which PHYSICAL radio each field drives is unknown. The cause is in the
     reference source: `rpCC1101 obRadio1; //radio 2 in schematics`. Every
     loopback needs both radios, so isolating either degrades both directions
     identically and no experiment run so far can distinguish
     radio1_ant = CS0 from radio1_ant = CS1. A cross-pairing sweep was expected
     to be the cheapest experiment that CAN. **It is not, and the 2026-07-28
     run proved it** -- the asymmetries it finds show each field pairs with one
     chip select but cannot say which, because "field 1 -> CS0 with the
     transmitter dominating" and "field 1 -> CS1 with the receiver dominating"
     fit every cell identically. Only a cell isolating ONE radio could separate
     them, and those all transfer nothing. See the hardware record and
     find_asymmetries() below. Gap 3 needs an EXTERNAL transmitter or receiver.

------------------------------------------------------------------------------
THE THREE OUTCOMES, NAMED IN ADVANCE
------------------------------------------------------------------------------

Naming these before the run is the point. It is what makes this an experiment
rather than a fishing trip, and this tool prints exactly one of them.

  1. BAND_MAP_WRONG -- a cell works at 315 MHz. Then the band encoding or the
     field-to-radio assignment is not what pcal6416.h says. The map gets
     corrected and a fwog_ant_for_frequency() helper lands so applications stop
     having to know any of this. This is the outcome that produces code.
  2. PATH_ABSENT -- no cell works at 315 MHz and the controls pass. The
     315 MHz path is genuinely absent from this board, unpopulated or not
     routed. FWOG_ANT_200MHZ's name is then actively misleading and gets a
     comment saying what it has and has not been shown to do. This is a
     LEGITIMATE and probably the most likely outcome, and it closes the
     question instead of leaving it open a fourth session.
  3. VOID -- the controls failed. Fix the bench. Do NOT interpret the 315 MHz
     data; a sweep of all zeros is indistinguishable from a bench that stopped
     working, which is exactly why the controls are in the matrix.

------------------------------------------------------------------------------
WHAT THIS WILL NOT ESTABLISH
------------------------------------------------------------------------------

RF behaviour at any real range. Every cell is two radios centimetres apart at
-30 dBm, which fact 36 already warns is "a brutal amount of margin" that lets a
wrong filter still carry packets. ONLY THE ZERO RESULTS ARE CLEAN-EDGED. It
also says nothing about 868 MHz, about the expander's breakout direction and
pull-up bits (fact 31 still stands for those), or about a second board -- which
for outcome 2 is exactly the check that would distinguish "unpopulated on this
board" from "not in the design", and cannot be run without one.

Usage:
    python tools/ant_sweep.py                 # the full 96-cell sweep
    python tools/ant_sweep.py --packets 4     # faster, less confident
    python tools/ant_sweep.py --out sweep.md  # also write a findings table
"""
import argparse
import json
import re
import sys
import time

# ---------------------------------------------------------------------------
# Pure logic. No serial, no board. Host-tested by tools/tests/test_ant_sweep.py
# -- which matters more than usual here, because the whole value of this tool
# is the verdict it prints, and a bench session is a bad place to discover the
# verdict logic is wrong.
# ---------------------------------------------------------------------------

ANT_NAMES = {0: "isolation", 1: "200MHz", 2: "400MHz", 3: "900MHz"}

# The subject and the two controls. 433.92 and 915 MHz are CONTROLS, NOT
# EXTRAS: without them a sweep of all zeros at 315 MHz cannot be distinguished
# from a bench that stopped working.
CARRIER_SUBJECT = 315_000_000
CARRIER_CONTROLS = (433_920_000, 915_000_000)
CARRIERS = (CARRIER_SUBJECT,) + CARRIER_CONTROLS

# Matches fact 36 so the numbers are comparable rather than merely new. -30 dBm
# is the lowest PA table entry.
SWEEP_DBM = -30

# (carrier_hz, (radio1_ant, radio2_ant)) -> the cell whose result validates the
# whole run. Fact 36 measured 10/10 at about -47 dBm here.
CONTROL_CELL = (433_920_000, (2, 2))
CONTROL_RSSI_EXPECTED = -47


def sweep_cells(carriers=CARRIERS, packets=10):
    """The full matrix, in run order.

    3 carriers x 16 (radio1_ant, radio2_ant) pairs x 2 directions = 96
    direction-cells. Ordered carrier-major then antenna-pair-major so that the
    expensive `rf` retune happens once per carrier rather than once per cell.
    """
    out = []
    for hz in carriers:
        for a1 in range(4):
            for a2 in range(4):
                for tx in (0, 1):
                    out.append({
                        "freq_hz": hz,
                        "radio1_ant": a1,
                        "radio2_ant": a2,
                        "tx": tx,
                        "packets": packets,
                    })
    return out


_KV = re.compile(r"([a-z_0-9]+)=(-?[0-9a-fA-Fx]+)")


def parse_loop_reply(line):
    """`OK loop tx=cs0 rx=cs1 tried=10 keyed=10 received=10 crc_ok=10 ...`

    Returns a dict of the integer fields, or None if the line is not a
    successful loop reply. `tx=cs0` is deliberately NOT parsed as an int -- the
    `cs` prefix is what distinguishes a chip select from an ordinal, and fact 36
    is explicit that conflating the two is how the field-to-radio question got
    muddled in the first place.
    """
    if not line or not line.startswith("OK loop"):
        return None
    out = {}
    for k, v in _KV.findall(line):
        if k in ("tx", "rx"):
            continue
        try:
            out[k] = int(v, 0)
        except ValueError:
            pass
    m = re.search(r"tx=cs(\d)", line)
    if m:
        out["tx"] = int(m.group(1))
    m = re.search(r"rx=cs(\d)", line)
    if m:
        out["rx"] = int(m.group(1))
    return out or None


def parse_ant_reply(line):
    """`OK ant radio1=400MHz radio2=400MHz packed=0x1234 (...)`"""
    if not line or not line.startswith("OK ant"):
        return None
    m = re.search(r"radio1=(\S+)\s+radio2=(\S+)", line)
    if not m:
        return None
    out = {"radio1": m.group(1), "radio2": m.group(2)}
    p = re.search(r"packed=(0x[0-9A-Fa-f]+)", line)
    if p:
        out["packed"] = int(p.group(1), 16)
    return out


def cell_passed(result):
    """A cell 'works' only if every packet arrived AND passed CRC.

    Deliberately strict. At -30 dBm two centimetres apart, a partial result is
    far more likely to mean a marginal path than a working one, and fact 36's
    warning about a wrong filter still carrying packets means an ambiguous
    'sort of works' is the least useful answer this sweep could produce.
    """
    if not result:
        return False
    tried = result.get("tried", 0)
    return (tried > 0
            and result.get("received", 0) == tried
            and result.get("crc_ok", 0) == tried)


def find_asymmetries(results):
    """Cells where (A,B) differs from (B,A) at the same carrier and direction.

    This was written believing it would RESOLVE gap 3 -- that if one asymmetric
    cell works and its mirror does not, the two antenna fields are
    distinguished and we know which physical radio each drives. **The 2026-07-28
    run showed that reasoning is wrong, and the hardware record records why.**

    Asymmetries do establish that each field pairs with one chip select. They
    do NOT say which. Every cell is fitted equally well by
    "radio1_ant -> CS0 and the transmitter's filter dominates" and by
    "radio1_ant -> CS1 and the receiver's filter dominates": relabelling the
    radios and swapping which side dominates maps one reading onto the other
    exactly. Telling them apart needs a cell where only ONE radio has an
    antenna path, and every such cell transfers nothing, because an on-board
    loopback needs both radios. No on-board experiment can close gap 3.

    Still worth computing -- the pairing is real and the asymmetry is the
    evidence for it. Just do not let the output claim more than that.
    """
    by_key = {}
    for r in results:
        key = (r["freq_hz"], r["radio1_ant"], r["radio2_ant"], r["tx"])
        by_key[key] = cell_passed(r.get("result"))

    seen = set()
    out = []
    for (hz, a1, a2, tx), passed in sorted(by_key.items()):
        if a1 == a2:
            continue
        mirror = (hz, a2, a1, tx)
        if mirror not in by_key:
            continue
        if (hz, min(a1, a2), max(a1, a2), tx) in seen:
            continue
        if passed != by_key[mirror]:
            seen.add((hz, min(a1, a2), max(a1, a2), tx))
            out.append({
                "freq_hz": hz, "tx": tx,
                "pair": (a1, a2), "pair_passed": passed,
                "mirror": (a2, a1), "mirror_passed": by_key[mirror],
            })
    return out


def classify(results):
    """Return the named outcome. One of VOID, BAND_MAP_WRONG, PATH_ABSENT.

    Order matters: the control check comes FIRST and short-circuits. A run
    whose controls failed tells us nothing about 315 MHz, and reporting a
    315 MHz conclusion from it would be exactly the mistake the controls exist
    to prevent.
    """
    controls = [r for r in results if r["freq_hz"] in CARRIER_CONTROLS]
    control_passes = [r for r in controls if cell_passed(r.get("result"))]

    named = [r for r in results
             if (r["freq_hz"], (r["radio1_ant"], r["radio2_ant"]))
             == CONTROL_CELL]
    named_ok = any(cell_passed(r.get("result")) for r in named)

    subject = [r for r in results if r["freq_hz"] == CARRIER_SUBJECT]
    subject_passes = [r for r in subject if cell_passed(r.get("result"))]

    if not controls:
        return {
            "outcome": "VOID",
            "reason": "no control carriers were swept; 315 MHz data from a run "
                      "with no controls is uninterpretable",
            "control_passes": 0, "subject_passes": 0, "asymmetries": [],
        }

    if not named_ok:
        return {
            "outcome": "VOID",
            "reason": "the named control cell (433.92 MHz, both antennas "
                      "400MHz) did not reproduce fact 36's 10/10. The bench is "
                      "not working; do NOT interpret the 315 MHz data.",
            "control_passes": len(control_passes),
            "subject_passes": len(subject_passes),
            "asymmetries": [],
        }

    asym = find_asymmetries(results)

    if subject_passes:
        return {
            "outcome": "BAND_MAP_WRONG",
            "reason": "%d of %d cells passed at 315 MHz, so the band encoding "
                      "or the field-to-radio assignment is not what "
                      "pcal6416.h says. Correct the map and add "
                      "fwog_ant_for_frequency()."
                      % (len(subject_passes), len(subject)),
            "control_passes": len(control_passes),
            "subject_passes": len(subject_passes),
            "working_cells": [
                {"radio1_ant": r["radio1_ant"], "radio2_ant": r["radio2_ant"],
                 "tx": r["tx"]} for r in subject_passes],
            "asymmetries": asym,
        }

    return {
        "outcome": "PATH_ABSENT",
        "reason": "controls pass and no cell of %d works at 315 MHz, so the "
                  "315 MHz path is absent from THIS BOARD -- unpopulated or "
                  "not routed. FWOG_ANT_200MHZ's name is actively misleading "
                  "and should say what it has and has not been shown to do. "
                  "Distinguishing 'unpopulated here' from 'not in the design' "
                  "needs a second board." % len(subject),
        "control_passes": len(control_passes),
        "subject_passes": 0,
        "asymmetries": asym,
    }


def format_table(results):
    """A markdown table, for pasting into a bench findings write-up."""
    lines = ["| carrier | radio1_ant | radio2_ant | tx | keyed | recv | crc_ok | rssi |",
             "|---|---|---|---|---|---|---|---|"]
    for r in results:
        res = r.get("result") or {}
        lines.append("| %.2f MHz | %s | %s | cs%d | %s | %s | %s | %s |" % (
            r["freq_hz"] / 1e6,
            ANT_NAMES[r["radio1_ant"]], ANT_NAMES[r["radio2_ant"]], r["tx"],
            res.get("keyed", "-"), res.get("received", "-"),
            res.get("crc_ok", "-"), res.get("rssi_dbm", "-")))
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# I/O. Everything below needs a board.
# ---------------------------------------------------------------------------

def run_sweep(packets=10, carriers=CARRIERS, verbose=True):
    import pathlib
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    from bench import Bench

    disp = Bench(cpu="display")
    main = Bench(cpu="main")
    results = []
    last_freq = None

    def log(msg):
        if verbose:
            print(msg, file=sys.stderr)

    try:
        log("# display on %s, main on %s" % (disp.port, main.port))
        for cell in sweep_cells(carriers, packets):
            if cell["freq_hz"] != last_freq:
                reply, _ = main.send("rf %d %d" % (cell["freq_hz"], SWEEP_DBM),
                                     wait=8.0)
                log("rf %d -> %s" % (cell["freq_hz"], reply))
                if reply is None or not reply.startswith("OK"):
                    log("!! rf retune failed; every cell at this carrier will "
                        "read as a zero that means nothing")
                last_freq = cell["freq_hz"]

            areply, _ = disp.send("ant %d %d" % (cell["radio1_ant"],
                                                 cell["radio2_ant"]), wait=4.0)
            if areply is None or not areply.startswith("OK"):
                log("!! ant %d %d failed: %s" % (cell["radio1_ant"],
                                                 cell["radio2_ant"], areply))

            # `loop` transmits `packets` frames with a per-packet timeout, so
            # this needs far more than bench.py's 2 s default.
            lreply, _ = main.send("loop %d %d" % (cell["tx"], packets),
                                  wait=5.0 + 1.5 * packets)
            parsed = parse_loop_reply(lreply)
            results.append(dict(cell, result=parsed, raw=lreply))
            log("  %.2fMHz a1=%s a2=%s tx=cs%d -> %s" % (
                cell["freq_hz"] / 1e6, ANT_NAMES[cell["radio1_ant"]],
                ANT_NAMES[cell["radio2_ant"]], cell["tx"],
                "PASS" if cell_passed(parsed) else "fail"))
    except KeyboardInterrupt:
        # Restoring on interrupt is not politeness. The PCAL6416 does NOT reset
        # when the RP2040 does (pcal6416.h), so a run killed part way through
        # leaves the board in whatever antenna state it was mid-cell -- and
        # FWOG_ANT_ISOLATION looks exactly like dead radios to whoever picks the
        # board up next. A long run is far more likely to be killed than a
        # short one, which is why this branch exists at all.
        log("\n!! interrupted -- restoring antenna defaults before exiting")
    finally:
        try:
            reply, _ = disp.send("ant default", wait=4.0)
            log("restore: %s" % reply)
            if reply is None or not reply.startswith("OK"):
                log("!! ANTENNA RESTORE FAILED. The expander holds its state "
                    "across a reset -- run 'python tools/bench.py ant default' "
                    "before trusting this board's radios again.")
        finally:
            disp.close()
            main.close()
    return results


def main_cli(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--packets", type=int, default=10,
                    help="packets per cell (default 10, matching fact 36)")
    ap.add_argument("--out", help="write the markdown table and verdict here")
    ap.add_argument("--json", help="write raw results here")
    args = ap.parse_args(argv)

    results = run_sweep(packets=args.packets)
    verdict = classify(results)

    table = format_table(results)
    print(table)
    print()
    print("OUTCOME: %s" % verdict["outcome"])
    print(verdict["reason"])
    if verdict["asymmetries"]:
        print("\nASYMMETRIES -- these show each field pairs with ONE chip "
              "select. They do NOT say which physical radio (the hardware record):")
        for a in verdict["asymmetries"]:
            print("  %.2f MHz tx=cs%d: (%s,%s)=%s but (%s,%s)=%s" % (
                a["freq_hz"] / 1e6, a["tx"],
                ANT_NAMES[a["pair"][0]], ANT_NAMES[a["pair"][1]],
                "PASS" if a["pair_passed"] else "fail",
                ANT_NAMES[a["mirror"][0]], ANT_NAMES[a["mirror"][1]],
                "PASS" if a["mirror_passed"] else "fail"))
    else:
        print("\nNo asymmetries: this run does not even establish that the two "
              "antenna fields are distinguishable (gap 3 stays open, as it "
              "does either way -- see the hardware record).")

    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write("# 315 MHz antenna sweep\n\n%s\n\n## Outcome: %s\n\n%s\n"
                    % (table, verdict["outcome"], verdict["reason"]))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"results": results, "verdict": verdict}, f, indent=2)
    return 0 if verdict["outcome"] != "VOID" else 2


if __name__ == "__main__":
    raise SystemExit(main_cli())
