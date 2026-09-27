"""Host tests for fsbak classify / parse / PUT restore text.

No board, no serial.
"""
import pathlib
import struct
import sys
import tempfile
import unittest
import wave
from unittest import mock

_TOOLS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_TOOLS))
sys.path.insert(0, str(_TOOLS / "fsbak"))

import fsbak as fb  # noqa: E402


class TestClassify(unittest.TestCase):
    def test_known_files(self):
        self.assertEqual(fb.classify("OPTIC.BIN"), "OpticClick")
        self.assertEqual(fb.classify("/PHLIB.BIN"), "PingHalo")
        self.assertEqual(fb.classify("mscope.cal"), "MicScope")
        self.assertEqual(fb.classify("chirpmail/CANNED.TXT"), "ChirpMail")
        self.assertEqual(fb.classify("talkclip/CLIP0001.RAW"), "TalkClip")
        self.assertEqual(fb.classify("trail/WALK0001.CSV"), "InertialTrail")
        self.assertEqual(fb.classify("trailrf/TRAIL001.CSV"), "InertialTrailRF")
        self.assertEqual(fb.classify("ismburst/BURST0001.BIN"), "ISMburst")
        self.assertEqual(fb.classify("scripts/SMOKE.WASM"), "DiskGlass")
        self.assertEqual(fb.classify("mystery.dat"), "other")

    def test_fatfs_path(self):
        self.assertEqual(fb.fatfs_path("OPTIC.BIN"), "/OPTIC.BIN")
        self.assertEqual(fb.fatfs_path("talkclip/CLIP0001.RAW"),
                         "/talkclip/CLIP0001.RAW")


class TestParseAndPut(unittest.TestCase):
    def test_roundtrip_text(self):
        dump = [
            "noise",
            "FSBK1",
            "FILE /OPTIC.BIN 4",
            "deadbeef",
            "FILE /talkclip/CLIP0001.RAW 2",
            "0102",
            "END",
        ]
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            n = fb.parse_dump(dump, root)
            self.assertEqual(n, 2)
            self.assertEqual((root / "OPTIC.BIN").read_bytes(), bytes.fromhex("deadbeef"))
            self.assertEqual((root / "talkclip" / "CLIP0001.RAW").read_bytes(),
                             bytes.fromhex("0102"))
            groups = fb.group_tree(root)
            self.assertIn("OpticClick", groups)
            self.assertIn("TalkClip", groups)
            put = fb.format_put("/OPTIC.BIN", bytes.fromhex("deadbeef"))
            self.assertTrue(put.startswith("PUT /OPTIC.BIN 4\n"))
            self.assertIn("deadbeef", put)
            self.assertTrue(put.endswith("END\n"))
            files = fb.collect_push(root)
            paths = {p for p, _ in files}
            self.assertEqual(paths, {"/OPTIC.BIN", "/talkclip/CLIP0001.RAW"})


class _Port:
    def __init__(self, device, product, pid=0x2054):
        self.device = device
        self.product = product
        self.pid = pid


class TestPickMain(unittest.TestCase):
    def test_as_cpu_port_unwraps_com_string(self):
        ports = [_Port("COM5", "FWOG main kithome 004")]
        got = fb._as_cpu_port(ports, "COM5")
        self.assertIs(got, ports[0])
        self.assertEqual(got.product, "FWOG main kithome 004")

    def test_refuse_display_uses_product_not_str(self):
        fb._refuse_display(_Port("COM5", "FWOG main kithome 004"))
        with self.assertRaises(SystemExit):
            fb._refuse_display(_Port("COM4", "FWOG display kithome 004",
                                     pid=0x2055))


class TestTalkClipWav(unittest.TestCase):
    def test_pcm16_from_dgf1(self):
        pcm = bytes([0x00, 0x10, 0xFF, 0x7F])
        raw = struct.pack("<IHHII", fb.DG_MAGIC, 1, fb.DG_KIND_PCM16,
                          len(pcm), 8000) + b"\x00" * 32 + pcm
        self.assertEqual(len(raw), 48 + 4)
        got = fb.pcm16_from_dgf1(raw)
        self.assertEqual(got, (pcm, 8000))
        self.assertIsNone(fb.pcm16_from_dgf1(b"not a header"))

    def test_export_sidecar_and_skip_push(self):
        pcm = b"\x00\x00" * 16
        raw = struct.pack("<IHHII", fb.DG_MAGIC, 1, fb.DG_KIND_PCM16,
                          len(pcm), 8000) + b"\x00" * 32 + pcm
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            clip = root / "talkclip" / "CLIP0001.RAW"
            clip.parent.mkdir()
            clip.write_bytes(raw)
            logs = []
            with mock.patch.object(fb.shutil, "which", return_value=None):
                n = fb.export_talkclip_wavs(root, log=logs.append)
            self.assertEqual(n, 1)
            wav = root / "talkclip" / "CLIP0001.wav"
            self.assertTrue(wav.is_file())
            with wave.open(str(wav), "rb") as w:
                self.assertEqual(w.getnchannels(), 1)
                self.assertEqual(w.getsampwidth(), 2)
                self.assertEqual(w.getframerate(), 8000)
                self.assertEqual(w.readframes(w.getnframes()), pcm)
            pushed = {p for p, _ in fb.collect_push(root)}
            self.assertIn("/talkclip/CLIP0001.RAW", pushed)
            self.assertNotIn("/talkclip/CLIP0001.wav", pushed)
            self.assertTrue(any("CLIP0001.wav" in s for s in logs))

    def test_ffmpeg_argv(self):
        dest = pathlib.Path("out.wav")
        cmd = fb.ffmpeg_pcm_cmd("/usr/bin/ffmpeg", 8000, dest)
        self.assertEqual(cmd[0], "/usr/bin/ffmpeg")
        self.assertIn("pipe:0", cmd)
        self.assertIn("s16le", cmd)


if __name__ == "__main__":
    unittest.main()
