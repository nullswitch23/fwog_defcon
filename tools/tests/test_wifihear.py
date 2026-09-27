"""Host tests for the WifiHear CDC dashboard.

No board, no RF: line parsing, 1200-baud refusal, and main-vs-display
identification. This helper must never look like a deauther and must not
BOOTSEL the CPU it attaches to.
"""
import pathlib
import sys
import unittest

_TOOLS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_TOOLS))
sys.path.insert(0, str(_TOOLS / "wifihear"))

import fw  # noqa: E402
import wifihear as wh  # noqa: E402


def _display(product="FWOG display wifihear 001", device="COM60"):
    return fw.CpuPort(device, fw.ICS_USB_VID, "S-DISPLAY", product,
                      fw.CPU_PID["display"])


def _main(product="FWOG main wifihear 001", device="COM65"):
    return fw.CpuPort(device, fw.ICS_USB_VID, "S-MAIN", product,
                      fw.CPU_PID["main"])


class TestParse(unittest.TestCase):
    def test_ap_short_and_bn(self):
        ev = wh.parse_wifihear_line(
            "WH AP ssid=Cafe bssid=aabbccddeeff ch=6 rssi=-42 auth=wpa2"
        )
        self.assertEqual(ev["kind"], "AP")
        self.assertEqual(ev["ssid"], "Cafe")
        self.assertEqual(ev["bssid"], "aabbccddeeff")
        self.assertEqual(ev["ch"], 6)
        self.assertEqual(ev["rssi"], -42)
        ev = wh.parse_wifihear_line(
            "[wifihear] BN WIFI AP ssid=Cafe bssid=AABBCCDDEEFF ch=6 rssi=-42 auth=wpa2"
        )
        self.assertEqual(ev["bssid"], "aabbccddeeff")

    def test_sta_and_scan(self):
        ev = wh.parse_wifihear_line(
            "WH STA mac=112233445566 bssid=aabbccddeeff rssi=-55"
        )
        self.assertEqual(ev["kind"], "STA")
        self.assertEqual(ev["mac"], "112233445566")
        self.assertEqual(wh.parse_wifihear_line("WH on")["on"], True)
        self.assertEqual(wh.parse_wifihear_line("BN WIFI off")["on"], False)

    def test_ignores_banners_and_deauth_shaped_lines(self):
        self.assertIsNone(wh.parse_wifihear_line("[wifihear] display: ok"))
        self.assertIsNone(wh.parse_wifihear_line("attack deauth all"))
        self.assertIsNone(wh.parse_wifihear_line("MACRO page=0 slot=0 Play -> x"))


class TestPort(unittest.TestCase):
    def test_refuses_display_pid(self):
        with self.assertRaises(wh.WifiHearError):
            wh.resolve_wifihear_port([_display()], "COM60")

    def test_picks_main(self):
        p = wh.resolve_wifihear_port([_display(), _main()])
        self.assertEqual(p.device, "COM65")

    def test_refuses_1200(self):
        with self.assertRaises(wh.WifiHearError) as ctx:
            wh.open_cdc("COM65", baud=1200)
        self.assertIn("1200", str(ctx.exception))


class TestSurvey(unittest.TestCase):
    def test_demo_rows(self):
        s = wh.Survey()
        wh.load_demo(s)
        snap = s.snapshot()
        self.assertTrue(snap["scan_on"])
        self.assertEqual(len(snap["aps"]), 2)
        self.assertEqual(snap["aps"][0]["ssid"], "Lab-AP")
        self.assertEqual(len(snap["stas"]), 1)


if __name__ == "__main__":
    unittest.main()
