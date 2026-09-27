"""Host tests for the breakout output walk's pure logic.

The value of io_walk.py is the verdict it prints, and a bench session -- board
on the desk, human waiting -- is the worst place to find out the verdict logic
is wrong. Two things matter most here:

  1. The phase 0 SHORT-CIRCUIT. Its whole job is to stop a run with no IO
     voltage on header pin 4 from being read as a pin-level result.
  2. That the verdict claims only what the data supports. tools/ant_sweep.py
     printed a conclusion its data could not support and it cost a session
     (the hardware record) -- so the wording is asserted here, not just the label.
"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import io_walk as W


class TestPinTable(unittest.TestCase):
    def test_seven_outputs_in_header_order(self):
        self.assertEqual(len(W.OUTPUTS), 7)
        hdrs = [h for _, h, _ in W.OUTPUTS]
        self.assertEqual(hdrs, sorted(hdrs))

    def test_matches_published_pinout(self):
        """Against docs.freewili.com/gpio/gpio-pinout/.

        The same numbers are pinned on the firmware side by
        tests/test_io_pins.c. Both exist because a wrong header number sends
        the operator to the wrong LED and silently invalidates the run.
        """
        self.assertEqual(
            {g: h for g, h, _ in W.OUTPUTS},
            {13: 1, 27: 3, 8: 9, 11: 11, 15: 13, 14: 15, 25: 17})

    def test_loopback_pins_are_not_in_the_fpga_spi_group(self):
        """26 and 27 are FPGA-only lines with no expander direction bits."""
        self.assertNotIn(W.LOOPBACK_DRIVE, (12, 13, 14, 15))
        self.assertNotIn(W.LOOPBACK_SENSE, (12, 13, 14, 15))

    def test_result_codes_match_the_firmware_enum(self):
        """fwog_io_pin_result_t in bsp/common/io_pins.h."""
        self.assertEqual(W.PIN_RESULT, {
            0: "OK", 1: "NOT_BREAKOUT", 2: "NOT_OUTPUT",
            3: "IO_CONFIG_ACTIVE"})


class TestParse(unittest.TestCase):
    def test_drive_reply(self):
        p = W.parse_iopin("OK iopin gpio=27 drive=1 result=0 io_config=1")
        self.assertEqual(p, {"gpio": 27, "drive": 1, "result": 0,
                             "io_config": 1})

    def test_read_reply(self):
        p = W.parse_iopin("OK iopin gpio=26 read result=0 level=1")
        self.assertEqual(p["level"], 1)
        self.assertEqual(p["result"], 0)

    def test_refusal_is_parsed_not_discarded(self):
        """A refusal is data. result=3 must survive parsing so the caller can
        report WHICH rule refused rather than a bare failure."""
        p = W.parse_iopin("OK iopin gpio=13 drive=1 result=3 io_config=2")
        self.assertEqual(p["result"], 3)
        self.assertEqual(W.PIN_RESULT[p["result"]], "IO_CONFIG_ACTIVE")

    def test_non_replies(self):
        for s in (None, "", "ERR iopin bad gpio", "OK help 18 commands"):
            self.assertIsNone(W.parse_iopin(s))


class TestLoopback(unittest.TestCase):
    def test_tracks_both_ways(self):
        self.assertTrue(W.loopback_verdict(True, False))

    def test_stuck_high_fails(self):
        """The control must not accept a line stuck high -- that is one of the
        faults the tool exists to find."""
        self.assertFalse(W.loopback_verdict(True, True))

    def test_stuck_low_fails(self):
        self.assertFalse(W.loopback_verdict(False, False))

    def test_inverted_fails(self):
        self.assertFalse(W.loopback_verdict(False, True))

    def test_unreadable_fails(self):
        self.assertFalse(W.loopback_verdict(None, None))


class TestClassify(unittest.TestCase):
    def all_walked(self, ok=True):
        return {g: ok for g, _, _ in W.OUTPUTS}

    def test_phase0_failure_short_circuits(self):
        """Even with every pin reported good, a failed control voids the pin
        result -- and the reason must point at the voltage jumper."""
        v = W.classify(False, self.all_walked(True), {})
        self.assertEqual(v["outcome"], "PATH_DEAD_OR_NO_VPINS")
        self.assertIn("pin 4", v["reason"])
        self.assertIn("jumper", v["reason"])

    def test_phase0_not_run_is_void(self):
        v = W.classify(None, self.all_walked(True), {})
        self.assertEqual(v["outcome"], "VOID")

    def test_all_good(self):
        v = W.classify(True, self.all_walked(True), {})
        self.assertEqual(v["outcome"], "ALL_OUTPUTS_DRIVE")
        self.assertEqual(v["failed"], [])

    def test_verdict_does_not_overclaim(self):
        """The success text must disclaim drive strength explicitly.

        This is the ant_sweep.py lesson (the hardware record) encoded as a test: an LED
        lighting proves a level was reached, not that the 24 mA rating is met.
        """
        v = W.classify(True, self.all_walked(True), {})
        self.assertIn("24 mA", v["reason"])
        self.assertIn("says nothing", v["reason"])

    def test_one_stuck_is_named(self):
        walk = self.all_walked(True)
        walk[14] = False
        v = W.classify(True, walk, {})
        self.assertEqual(v["outcome"], "SOME_STUCK")
        self.assertEqual(v["failed"], [14])
        self.assertIn("GPIO14", v["reason"])
        self.assertIn("spi_sclk", v["reason"])

    def test_phase2_catches_what_the_walk_missed(self):
        """A pin that passed every walk step but is stuck on in phase 2 must
        still fail -- that is the whole reason phase 2 exists."""
        v = W.classify(True, self.all_walked(True), {25: False})
        self.assertEqual(v["outcome"], "SOME_STUCK")
        self.assertEqual(v["failed"], [25])

    def test_no_double_counting(self):
        """A pin failing BOTH the walk and phase 2 is named once."""
        walk = self.all_walked(True)
        walk[8] = False
        v = W.classify(True, walk, {8: False})
        self.assertEqual(v["failed"], [8])

    def test_skipped_pin_is_not_a_failure_but_is_not_a_pass_either(self):
        """`s` at the prompt means 'not observed'.

        Not a fault -- punishing an operator for being honest would train them
        to stop being honest. But not a PASS either: an earlier draft returned
        ALL_OUTPUTS_DRIVE here, quietly promoting six observations into seven.
        """
        walk = self.all_walked(True)
        walk[11] = None
        v = W.classify(True, walk, {})
        self.assertEqual(v["outcome"], "PARTIAL")
        self.assertEqual(v["failed"], [])
        self.assertEqual(v["unobserved"], [11])
        self.assertIn("unmeasured", v["reason"])

    def test_auto_mode_claims_nothing_about_the_pins(self):
        """--auto observes no LED at all.

        The control passing proves header pin 4 has voltage and one
        output/input pair works. It proves NOTHING about the seven outputs, and
        an earlier draft of classify() reported ALL_OUTPUTS_DRIVE from exactly
        this input -- the same overclaim that cost a session in the hardware record.
        """
        v = W.classify(True, {}, {})
        self.assertEqual(v["outcome"], "CONTROL_PASSED_UNOBSERVED")
        self.assertEqual(v["unobserved"], sorted(g for g, _, _ in W.OUTPUTS))
        self.assertIn("NO pin was observed", v["reason"])

    def test_all_outputs_drive_requires_every_pin_observed(self):
        """The strong verdict is reachable ONLY with all seven confirmed."""
        for missing, _, _ in W.OUTPUTS:
            walk = {g: True for g, _, _ in W.OUTPUTS if g != missing}
            self.assertNotEqual(W.classify(True, walk, {})["outcome"],
                                "ALL_OUTPUTS_DRIVE")


class TestInputPairs(unittest.TestCase):
    def test_covers_every_input_on_the_header(self):
        """The four inputs of gpio-pinout/, and no others."""
        self.assertEqual(sorted(s for _, s, _, _, _ in W.INPUT_PAIRS),
                         [9, 10, 12, 26])

    def test_drivers_are_all_proven_outputs(self):
        outs = {g for g, _, _ in W.OUTPUTS}
        for d, _, _, _, _ in W.INPUT_PAIRS:
            self.assertIn(d, outs)

    def test_header_numbers_match_the_pinout(self):
        self.assertEqual(
            {d: (dh, sh) for d, _, dh, sh, _ in W.INPUT_PAIRS},
            {8: (9, 5), 11: (11, 7), 15: (13, 12), 27: (3, 14)})


class TestClassifyInputs(unittest.TestCase):
    def identity(self):
        """The expected result: each input follows its own partner only."""
        partner = {d: s for d, s, _, _, _ in W.INPUT_PAIRS}
        senses = [s for _, s, _, _, _ in W.INPUT_PAIRS]
        return {d: {s: (s == partner[d]) for s in senses}
                for d, _, _, _, _ in W.INPUT_PAIRS}

    def test_clean_run(self):
        senses = [s for _, s, _, _, _ in W.INPUT_PAIRS]
        v = W.classify_inputs(self.identity(),
                              {s: False for s in senses},
                              {s: True for s in senses})
        self.assertEqual(v["outcome"], "ALL_INPUTS_SENSE")

    def test_success_text_does_not_overclaim(self):
        """Same discipline as the output verdict: say what was NOT measured."""
        v = W.classify_inputs(self.identity())
        self.assertIn("says nothing", v["reason"])
        self.assertIn("threshold", v["reason"])

    def test_dead_input_is_named(self):
        m = self.identity()
        m[15][12] = False           # spi_tx driven, spi_rx did not follow
        v = W.classify_inputs(m)
        self.assertEqual(v["outcome"], "SOME_INPUTS_DEAD")
        self.assertIn(15, v["dead"])
        self.assertIn("spi_tx -> spi_rx", v["reason"])

    def test_crosstalk_beats_dead_and_is_named(self):
        """A short is the more urgent finding: it invalidates the wiring, so it
        must be reported ahead of a dead input rather than buried under it."""
        m = self.identity()
        m[15][10] = True            # spi_tx also moved uart_cts
        v = W.classify_inputs(m)
        self.assertEqual(v["outcome"], "CROSSTALK")
        self.assertIn((15, 10), v["crosstalk"])
        self.assertIn("GPIO15 moved GPIO10", v["reason"])

    def test_four_pairwise_tests_would_have_missed_that(self):
        """The reason the matrix exists at all.

        Every input still follows its own partner here -- four independent
        pairwise checks would all pass -- yet two lines are shorted. Only the
        off-diagonal catches it.
        """
        m = self.identity()
        m[15][12] = True            # correct, and...
        m[15][10] = True            # ...also drags its neighbour
        self.assertTrue(m[15][12])  # the pairwise view still looks fine
        self.assertEqual(W.classify_inputs(m)["outcome"], "CROSSTALK")

    def test_input_stuck_high_with_all_drivers_low(self):
        senses = [s for _, s, _, _, _ in W.INPUT_PAIRS]
        low = {s: False for s in senses}
        low[9] = True
        v = W.classify_inputs(self.identity(), low,
                              {s: True for s in senses})
        self.assertEqual(v["outcome"], "SOME_INPUTS_DEAD")
        self.assertIn("GPIO9 high with every driver low", v["reason"])

    def test_unreadable_cell_is_void_not_a_failure(self):
        """A read that did not come back is missing data, not evidence of a
        dead pin. Calling it a failure would invent a fault."""
        m = self.identity()
        m[8][9] = None
        v = W.classify_inputs(m)
        self.assertEqual(v["outcome"], "VOID")
        self.assertEqual(v["dead"], [])


if __name__ == "__main__":
    unittest.main()
