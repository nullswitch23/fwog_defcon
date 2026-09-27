"""Host tests for the HostDeck Windows helper.

No board, no SendInput: identification and chord parsing are pure. The
helper must refuse the main CPU, a non-HostDeck display app, 1200 baud,
and an unknown chord — those are the ways it would become an injector or
brick the display's BOOTSEL path.
"""
import pathlib
import sys
import unittest

_TOOLS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_TOOLS))
sys.path.insert(0, str(_TOOLS / "hostdeck"))

import fw  # noqa: E402
import hostdeck as hd  # noqa: E402


def _display(product="FWOG display hostdeck 001", device="COM60"):
    return fw.CpuPort(device, fw.ICS_USB_VID, "S-DISPLAY", product,
                      fw.CPU_PID["display"])


def _main(product="FWOG main hostdeck 001", device="COM65"):
    return fw.CpuPort(device, fw.ICS_USB_VID, "S-MAIN", product,
                      fw.CPU_PID["main"])


class TestParseMacro(unittest.TestCase):
    def test_v001_copy_line(self):
        ev = hd.parse_macro_line(
            "MACRO page=0 slot=2 Copy -> Ctrl+C (HID not enumerated in v001)"
        )
        self.assertEqual(ev["kind"], "MACRO")
        self.assertEqual(ev["page"], 0)
        self.assertEqual(ev["slot"], 2)
        self.assertEqual(ev["label"], "Copy")
        self.assertEqual(ev["chord"], "Ctrl+C")

    def test_strips_cr_and_ignores_banners(self):
        self.assertIsNone(hd.parse_macro_line(
            "[hostdeck] macros on this CDC; TinyUSB HID is not in the BSP yet"
        ))
        ev = hd.parse_macro_line(
            "MACRO page=1 slot=4 Esc -> Escape (HID not enumerated in v001)\r"
        )
        self.assertEqual(ev["chord"], "Escape")

    def test_consumer_names_keep_spaces(self):
        ev = hd.parse_macro_line(
            "MACRO page=0 slot=0 Play -> Consumer Play/Pause "
            "(HID not enumerated in v001)"
        )
        self.assertEqual(ev["chord"], "Consumer Play/Pause")

    def test_hostdeck_token(self):
        ev = hd.parse_macro_line("HOSTDECK chord=Ctrl+C page=0 slot=2 label=Copy")
        self.assertEqual(ev["kind"], "HOSTDECK")
        self.assertEqual(ev["chord"], "Ctrl+C")
        self.assertEqual(ev["label"], "Copy")


class TestChords(unittest.TestCase):
    def test_firmware_page_one(self):
        self.assertEqual(hd.parse_chord("Consumer Play/Pause"),
                         ((), hd.VK_MEDIA_PLAY_PAUSE))
        self.assertEqual(hd.parse_chord("Consumer Mute"),
                         ((), hd.VK_VOLUME_MUTE))
        self.assertEqual(hd.parse_chord("Ctrl+C"),
                         ((hd.VK_CONTROL,), ord("C")))
        self.assertEqual(hd.parse_chord("Ctrl+V"),
                         ((hd.VK_CONTROL,), ord("V")))
        self.assertEqual(hd.parse_chord("Return"),
                         ((), hd.VK_RETURN))

    def test_firmware_page_two(self):
        self.assertEqual(hd.parse_chord("Ctrl+X"), ((hd.VK_CONTROL,), ord("X")))
        self.assertEqual(hd.parse_chord("Ctrl+Z"), ((hd.VK_CONTROL,), ord("Z")))
        self.assertEqual(hd.parse_chord("Ctrl+S"), ((hd.VK_CONTROL,), ord("S")))
        self.assertEqual(hd.parse_chord("Tab"), ((), hd.VK_TAB))
        self.assertEqual(hd.parse_chord("Escape"), ((), hd.VK_ESCAPE))

    def test_modifiers_and_consumer_aliases(self):
        self.assertEqual(hd.parse_chord("Alt+Shift+Tab"),
                         ((hd.VK_MENU, hd.VK_SHIFT), hd.VK_TAB))
        self.assertEqual(hd.parse_chord("Win+R"),
                         ((hd.VK_LWIN,), ord("R")))
        self.assertEqual(hd.parse_chord("Play"), ((), hd.VK_MEDIA_PLAY_PAUSE))
        self.assertEqual(hd.parse_chord("Mute"), ((), hd.VK_VOLUME_MUTE))
        self.assertEqual(hd.parse_chord("VolUp"), ((), hd.VK_VOLUME_UP))
        self.assertEqual(hd.parse_chord("VolDown"), ((), hd.VK_VOLUME_DOWN))

    def test_unknown_chord_refuses(self):
        with self.assertRaises(hd.ChordError):
            hd.parse_chord("LaunchTheMissiles")
        with self.assertRaises(hd.ChordError):
            hd.parse_chord("Ctrl+")


class TestIdentify(unittest.TestCase):
    def test_picks_display_hostdeck_not_main(self):
        ports = [_main(), _display()]
        got = hd.pick_hostdeck_display(ports)
        self.assertEqual(got.device, "COM60")
        self.assertEqual(got.pid, fw.CPU_PID["display"])

    def test_refuses_a_display_app_that_is_not_hostdeck(self):
        ports = [_display("FWOG display bench 001")]
        with self.assertRaises(hd.HostDeckError) as cm:
            hd.pick_hostdeck_display(ports)
        self.assertIn("not HostDeck", str(cm.exception))

    def test_refuses_main_even_if_named_hostdeck(self):
        # Product can be renamed; PID 2054 is still main. fw._pick_cpu_port
        # may fall through to the display prefix, so the helper has to refuse.
        ports = [_main("FWOG display hostdeck 001")]
        with self.assertRaises(hd.HostDeckError) as cm:
            hd.pick_hostdeck_display(ports)
        self.assertIn("MAIN", str(cm.exception))

    def test_port_override_refuses_main_pid(self):
        ports = [_main(), _display()]
        with self.assertRaises(hd.HostDeckError) as cm:
            hd.resolve_hostdeck_port(ports, "COM65")
        self.assertIn("MAIN", str(cm.exception))

    def test_port_override_accepts_hostdeck_display(self):
        ports = [_main(), _display()]
        got = hd.resolve_hostdeck_port(ports, "COM60")
        self.assertEqual(got.device, "COM60")

    def test_two_display_pids_refuse_rather_than_guess(self):
        ports = [
            _display(device="COM60"),
            _display("FWOG display hostdeck 001", device="COM61"),
        ]
        with self.assertRaises(fw.CpuPortError) as cm:
            hd.pick_hostdeck_display(ports)
        self.assertIn("--port", str(cm.exception))

    def test_product_token_is_case_insensitive(self):
        self.assertTrue(hd.is_hostdeck_product("FWOG display HostDeck 001"))
        self.assertTrue(hd.is_hostdeck_product("FWOG display kithome 001"))
        self.assertFalse(hd.is_hostdeck_product("FWOG display lcd 001"))
        # Token presence alone is not enough — pick_hostdeck_display still
        # keys PID, so a renamed main product cannot type keys.
        self.assertTrue(hd.is_hostdeck_product("FWOG main hostdeck 001"))

    def test_picks_kithome_display(self):
        ports = [_main("FWOG main kithome 001"),
                 _display("FWOG display kithome 001")]
        got = hd.pick_hostdeck_display(ports)
        self.assertEqual(got.device, "COM60")
        self.assertEqual(got.pid, fw.CPU_PID["display"])


class TestOpenGuard(unittest.TestCase):
    def test_refuses_1200_baud(self):
        with self.assertRaises(hd.HostDeckError) as cm:
            hd.open_cdc("COM60", baud=1200)
        self.assertIn("1200", str(cm.exception))
        self.assertIn("BOOTSEL", str(cm.exception))


if __name__ == "__main__":
    unittest.main()
