"""ogemu world-bus line helpers (no board, no SendInput)."""
import json
import pathlib
import sys
import unittest

_TOOLS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_TOOLS / "ogemu"))

import bench  # noqa: E402


class TestWorldLines(unittest.TestCase):
    def test_accel(self):
        d = json.loads(bench.accel_line(100, 0, 1000))
        self.assertEqual(d["cmd"], "accel")
        self.assertEqual(d["x"], 100)
        self.assertEqual(d["z"], 1000)

    def test_rssi_negative(self):
        d = json.loads(bench.rssi_line(-72, 433920000))
        self.assertEqual(d["dbm"], -72)
        self.assertEqual(d["hz"], 433920000)
        d = json.loads(bench.rf_line(0, -72))
        self.assertEqual(d["cmd"], "rf")
        self.assertEqual(d["art"], 0)

    def test_ook_and_mic(self):
        d = json.loads(bench.ook_line("tools/ogemu/scripts/fan.ook"))
        self.assertEqual(d["cmd"], "ook")
        self.assertTrue(d["file"].endswith("fan.ook"))
        self.assertEqual(json.loads(bench.mic_line(2400))["rms"], 2400)

    def test_press_release(self):
        self.assertEqual(json.loads(bench.press_line("green"))["btn"], "green")
        self.assertEqual(json.loads(bench.release_line("red"))["cmd"], "release")

    def test_split_inject_lines(self):
        lines = bench.split_inject_lines(
            "# skip\n"
            '{"cmd": "shake", "ms": 200}\n'
            "\n"
            "// no\n"
            "press green\n"
        )
        self.assertEqual(lines[0], '{"cmd": "shake", "ms": 200}')
        self.assertEqual(lines[1], "press green")


class TestLiveFramePpm(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.repo = pathlib.Path(__file__).resolve().parents[2]
        name = "ogemu_hostdeck.exe" if sys.platform == "win32" else "ogemu_hostdeck"
        cls.exe = cls.repo / "build-emu" / name

    def test_script_writes_ppm(self):
        if not self.exe.is_file():
            self.skipTest("ogemu_hostdeck not built")
        import subprocess
        import tempfile
        with tempfile.TemporaryDirectory() as td:
            ppm = pathlib.Path(td) / "panel.ppm"
            png = pathlib.Path(td) / "panel.png"
            subprocess.check_call(
                [str(self.exe),
                 "--script", str(self.repo / "tools/ogemu/scripts/smoke.jsonl"),
                 "--frame", str(ppm),
                 "--dump", str(png)],
                cwd=str(self.repo),
            )
            raw = ppm.read_bytes()
            self.assertTrue(raw.startswith(b"P6\n320 240\n"))
            self.assertGreater(len(raw), 320 * 240 * 3)


if __name__ == "__main__":
    unittest.main()
