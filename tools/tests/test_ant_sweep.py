"""Host tests for the 315 MHz antenna sweep's pure logic.

These matter more than their size suggests. The whole value of ant_sweep.py is
the VERDICT it prints, and a bench session -- with a board on the desk and a
human waiting -- is the worst possible place to discover the verdict logic is
wrong. In particular the VOID short-circuit must be tested, because its entire
job is to stop a broken bench from being read as a 315 MHz result.
"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import ant_sweep as A


def cell(hz, a1, a2, tx, received=0, tried=10, crc=None):
    """Build one result row. `received=10` means the cell passed."""
    crc = received if crc is None else crc
    return {
        "freq_hz": hz, "radio1_ant": a1, "radio2_ant": a2, "tx": tx,
        "packets": tried,
        "result": {"tried": tried, "keyed": tried, "received": received,
                   "crc_ok": crc, "rssi_dbm": -47, "tx": tx, "rx": 1 - tx},
    }


def full_run(subject_passes=(), control_ok=True):
    """A complete 96-cell run: all zeros except what is asked for."""
    out = []
    for c in A.sweep_cells():
        recv = 0
        key = (c["freq_hz"], c["radio1_ant"], c["radio2_ant"], c["tx"])
        if c["freq_hz"] == A.CARRIER_SUBJECT and key[1:] in subject_passes:
            recv = 10
        if (c["freq_hz"], (c["radio1_ant"], c["radio2_ant"])) == A.CONTROL_CELL:
            recv = 10 if control_ok else 0
        out.append(cell(c["freq_hz"], c["radio1_ant"], c["radio2_ant"],
                        c["tx"], received=recv))
    return out


class TestMatrix(unittest.TestCase):
    def test_ninety_six_direction_cells(self):
        cells = A.sweep_cells()
        # 3 carriers x 16 antenna pairs x 2 directions.
        self.assertEqual(len(cells), 96)
        self.assertEqual(len({(c["freq_hz"], c["radio1_ant"],
                               c["radio2_ant"], c["tx"]) for c in cells}), 96)

    def test_covers_all_sixteen_pairs_including_cross_pairings(self):
        """Gap 1: the 2026-07-28 sweep never set the two fields differently."""
        at_315 = {(c["radio1_ant"], c["radio2_ant"]) for c in A.sweep_cells()
                  if c["freq_hz"] == A.CARRIER_SUBJECT}
        self.assertEqual(len(at_315), 16)
        self.assertIn((1, 3), at_315)   # a genuine cross-pairing
        self.assertIn((3, 1), at_315)   # and its mirror

    def test_900mhz_path_is_tried_at_315(self):
        """Gap 2: that cell was a dash, and a dash is not a result."""
        self.assertTrue(any(c["freq_hz"] == A.CARRIER_SUBJECT
                            and c["radio1_ant"] == 3 and c["radio2_ant"] == 3
                            for c in A.sweep_cells()))

    def test_controls_are_in_the_matrix(self):
        freqs = {c["freq_hz"] for c in A.sweep_cells()}
        self.assertIn(433_920_000, freqs)
        self.assertIn(915_000_000, freqs)

    def test_carrier_major_ordering_minimises_retunes(self):
        """One `rf` retune per carrier, not one per cell."""
        seen, changes = None, 0
        for c in A.sweep_cells():
            if c["freq_hz"] != seen:
                changes += 1
                seen = c["freq_hz"]
        self.assertEqual(changes, 3)


class TestParsing(unittest.TestCase):
    def test_parse_loop_reply(self):
        line = ("OK loop tx=cs0 rx=cs1 tried=10 keyed=10 received=9 crc_ok=8 "
                "rssi_dbm=-47 lqi=12 freq_hz=433920000 dbm=-30")
        r = A.parse_loop_reply(line)
        self.assertEqual(r["tried"], 10)
        self.assertEqual(r["keyed"], 10)
        self.assertEqual(r["received"], 9)
        self.assertEqual(r["crc_ok"], 8)
        self.assertEqual(r["rssi_dbm"], -47)
        self.assertEqual(r["freq_hz"], 433920000)
        self.assertEqual(r["dbm"], -30)
        # tx/rx are chip selects, not ordinals -- conflating the two is how the
        # field-to-radio question got muddled in the first place.
        self.assertEqual(r["tx"], 0)
        self.assertEqual(r["rx"], 1)

    def test_parse_loop_reply_rejects_non_replies(self):
        self.assertIsNone(A.parse_loop_reply(None))
        self.assertIsNone(A.parse_loop_reply(""))
        self.assertIsNone(A.parse_loop_reply("ERR loop not-bound"))
        self.assertIsNone(A.parse_loop_reply("OK rf freq_hz=315000000 dbm=-30"))

    def test_parse_ant_reply(self):
        line = ("OK ant radio1=400MHz radio2=900MHz packed=0x1234 (this chip "
                "does NOT reset with the RP2040 -- it holds until rewritten)")
        r = A.parse_ant_reply(line)
        self.assertEqual(r["radio1"], "400MHz")
        self.assertEqual(r["radio2"], "900MHz")
        self.assertEqual(r["packed"], 0x1234)
        self.assertIsNone(A.parse_ant_reply("ERR ant i2c-write-failed"))


class TestCellPassed(unittest.TestCase):
    def test_only_a_clean_sweep_counts(self):
        self.assertTrue(A.cell_passed({"tried": 10, "received": 10, "crc_ok": 10}))
        # Partial results do NOT count. At -30 dBm two centimetres apart a
        # partial is far more likely a marginal path than a working one.
        self.assertFalse(A.cell_passed({"tried": 10, "received": 9, "crc_ok": 9}))
        # Received but CRC-bad is not a working path either.
        self.assertFalse(A.cell_passed({"tried": 10, "received": 10, "crc_ok": 9}))
        self.assertFalse(A.cell_passed({"tried": 10, "received": 0, "crc_ok": 0}))
        self.assertFalse(A.cell_passed({"tried": 0, "received": 0, "crc_ok": 0}))
        self.assertFalse(A.cell_passed(None))


class TestClassify(unittest.TestCase):
    def test_void_when_the_named_control_fails(self):
        """The whole reason the controls are in the matrix."""
        v = A.classify(full_run(control_ok=False))
        self.assertEqual(v["outcome"], "VOID")
        self.assertIn("do NOT interpret", v["reason"])

    def test_void_short_circuits_even_if_315_appears_to_work(self):
        """A broken bench that somehow shows a 315 MHz pass is still VOID.

        This is the assertion that matters most: without the short-circuit,
        a run whose controls failed could report BAND_MAP_WRONG and send
        someone off to 'correct' a map on the strength of noise.
        """
        v = A.classify(full_run(subject_passes={(1, 3, 0)}, control_ok=False))
        self.assertEqual(v["outcome"], "VOID")

    def test_void_when_there_are_no_controls_at_all(self):
        results = [cell(A.CARRIER_SUBJECT, a1, a2, tx)
                   for a1 in range(4) for a2 in range(4) for tx in (0, 1)]
        v = A.classify(results)
        self.assertEqual(v["outcome"], "VOID")

    def test_path_absent_when_controls_pass_and_315_is_silent(self):
        v = A.classify(full_run())
        self.assertEqual(v["outcome"], "PATH_ABSENT")
        self.assertEqual(v["subject_passes"], 0)
        self.assertIn("second board", v["reason"])

    def test_band_map_wrong_when_a_315_cell_works(self):
        v = A.classify(full_run(subject_passes={(3, 3, 0), (3, 3, 1)}))
        self.assertEqual(v["outcome"], "BAND_MAP_WRONG")
        self.assertEqual(v["subject_passes"], 2)
        self.assertIn({"radio1_ant": 3, "radio2_ant": 3, "tx": 0},
                      v["working_cells"])


class TestAsymmetry(unittest.TestCase):
    def test_finds_a_mirrored_disagreement(self):
        """Gap 3: this is what distinguishes radio1_ant = CS0 from = CS1."""
        v = A.classify(full_run(subject_passes={(1, 3, 0)}))
        self.assertEqual(v["outcome"], "BAND_MAP_WRONG")
        self.assertTrue(v["asymmetries"])
        a = v["asymmetries"][0]
        self.assertEqual(a["freq_hz"], A.CARRIER_SUBJECT)
        self.assertEqual(a["tx"], 0)
        self.assertEqual({a["pair"], a["mirror"]}, {(1, 3), (3, 1)})
        self.assertNotEqual(a["pair_passed"], a["mirror_passed"])

    def test_symmetric_results_report_no_asymmetry(self):
        """A symmetric pass leaves gap 3 open, and must say so."""
        v = A.classify(full_run(subject_passes={(1, 3, 0), (3, 1, 0)}))
        self.assertEqual(v["asymmetries"], [])

    def test_identical_pairs_are_not_asymmetry_candidates(self):
        v = A.classify(full_run(subject_passes={(2, 2, 0)}))
        for a in v["asymmetries"]:
            self.assertNotEqual(a["pair"][0], a["pair"][1])

    def test_each_mirrored_pair_reported_once(self):
        v = A.classify(full_run(subject_passes={(0, 1, 0), (2, 3, 1)}))
        keys = [(a["freq_hz"], tuple(sorted(a["pair"])), a["tx"])
                for a in v["asymmetries"]]
        self.assertEqual(len(keys), len(set(keys)))


class TestTable(unittest.TestCase):
    def test_table_has_a_row_per_cell(self):
        results = full_run()
        table = A.format_table(results)
        self.assertEqual(len(table.splitlines()), len(results) + 2)  # + header
        self.assertIn("315.00 MHz", table)
        self.assertIn("isolation", table)

    def test_missing_results_render_as_dashes_not_zeros(self):
        """A cell that never replied must NOT look like a measured zero."""
        results = [dict(A.sweep_cells()[0], result=None)]
        self.assertIn("| - | - | - |", A.format_table(results))


if __name__ == "__main__":
    unittest.main()
