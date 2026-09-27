"""Host tests for ChirpMail canned parse / wire script. No board."""
import pathlib
import sys
import unittest
from unittest import mock

_TOOLS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_TOOLS))
sys.path.insert(0, str(_TOOLS / "chirpmail"))

import chirpmail as cm  # noqa: E402


class TestCanned(unittest.TestCase):
    def test_parse_truncates_and_pads(self):
        text = "# hi\nMEET NOC STEPS NOW\nTHIS LINE IS WAY TOO LONG FOR RF\n"
        slots = cm.parse_canned(text)
        self.assertEqual(len(slots), 24)
        self.assertEqual(slots[0], "MEET NOC STEPS NOW")
        self.assertEqual(len(slots[1]), 20)
        self.assertEqual(slots[2], "")

    def test_wire_script(self):
        slots = [""] * 24
        slots[0] = "HELLO"
        w = cm.wire_script(slots)
        self.assertTrue(w.startswith("CM1\n"))
        self.assertIn("SLOT 00 HELLO", w)
        self.assertTrue(w.strip().endswith("END"))

    def test_roundtrip_file(self):
        src = (_TOOLS / "chirpmail" / "canned.txt").read_text(encoding="utf-8")
        slots = cm.parse_canned(src)
        self.assertEqual(slots[0], "MEET NOC STEPS NOW")
        self.assertEqual(slots[6], "ACK PIN CODE")
        again = cm.parse_canned(cm.format_canned(slots))
        self.assertEqual(again[0], slots[0])
        self.assertEqual(again[7], slots[7])


class TestPickMain(unittest.TestCase):
    def test_finds_chirpmail_product(self):
        ports = [cm.fw.CpuPort(
            "COM5", 0x093C, "S", "FWOG main chirpmail 004", 0x2054)]
        with mock.patch.object(cm.fw, "_cpu_ports", return_value=ports):
            p = cm._pick_chirpmail_main()
        self.assertEqual(p.device, "COM5")

    def test_other_main_is_a_clear_exit_not_attribute_error(self):
        ports = [cm.fw.CpuPort(
            "COM5", 0x093C, "S", "FWOG main voltpet 004", 0x2054)]
        with mock.patch.object(cm.fw, "_cpu_ports", return_value=ports), \
             mock.patch.object(cm.fw, "_pick_cpu_port", return_value="COM5"):
            with self.assertRaises(SystemExit) as e:
                cm._pick_chirpmail_main()
        msg = str(e.exception)
        self.assertIn("chirpmail_main", msg)
        self.assertIn("voltpet", msg.lower())


if __name__ == "__main__":
    unittest.main()
