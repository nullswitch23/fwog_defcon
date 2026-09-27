"""Host tests for RigGlass helper formatting and windows."""
import pathlib
import sys
import unittest

_TOOLS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_TOOLS / "rigglass"))

import rigglass as rg  # noqa: E402


class FakeClock:
    def __init__(self):
        self.t = 0.0

    def __call__(self):
        return self.t


class TestNetSkip(unittest.TestCase):
    def test_real_nic_kept(self):
        self.assertFalse(rg.net_skip("Ethernet"))
        self.assertFalse(rg.net_skip("Wi-Fi"))

    def test_virtual_dropped(self):
        self.assertTrue(rg.net_skip("Teredo Tunneling Pseudo-Interface"))
        self.assertTrue(rg.net_skip("WAN Miniport (IKEv2)"))


class TestRolling(unittest.TestCase):
    def test_avg_windows(self):
        clock = FakeClock()
        r = rg.Rolling(clock=clock)
        r.add(10)
        clock.t = 30
        r.add(20)
        clock.t = 90
        self.assertEqual(r.avg(120, 0), 15)
        clock.t = 200
        r.add(40)
        # 10 at t=0 is outside 120s window (200-120=80); 20 at 30 also out
        self.assertEqual(r.avg(120, 0), 40)

    def test_triple(self):
        r = rg.Rolling(clock=FakeClock())
        self.assertEqual(r.triple(7), (7, 7, 7))


class TestFormat(unittest.TestCase):
    def test_line(self):
        line = rg.format_line(
            "office-pc",
            (12, 10, 8),
            (45, 44, 40),
            (8, 5, 3),
            (30, 20, 15),
            (61, 58, 55),
        )
        self.assertTrue(line.startswith("RG "))
        self.assertIn("cpu=12:10:8", line)
        self.assertIn("net=8:5:3", line)
        self.assertIn("gpu=30:20:15", line)
        self.assertIn("tmp=61:58:55", line)
        self.assertIn("host=office-pc", line)
        self.assertTrue(line.endswith("\n"))
        self.assertLess(len(line), 160)

    def test_na(self):
        line = rg.format_line("x", (1, 1, 1), (2, 2, 2), (0, 0, 0),
                              (255, 255, 255), (255, 255, 255))
        self.assertIn("gpu=255:255:255", line)


class TestTempConvert(unittest.TestCase):
    def test_kelvin_tenths(self):
        c = rg._kelvin_tenths_to_c(3200)
        self.assertIsNotNone(c)
        self.assertAlmostEqual(c, 46.85, places=1)

    def test_celsius_passthrough(self):
        self.assertEqual(rg._kelvin_tenths_to_c(61.0), 61.0)


if __name__ == "__main__":
    unittest.main()
