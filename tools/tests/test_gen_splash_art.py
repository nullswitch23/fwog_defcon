import pathlib
import struct
import sys
import tempfile
import unittest
import wave

from PIL import Image, ImageDraw

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import gen_splash_art as g  # noqa: E402


class TestSkully(unittest.TestCase):
    def test_badge_is_round_bw(self):
        badge = g.draw_skully(64)
        self.assertEqual(badge.size, (64, 64))
        # Corners of the square stay transparent; the rim is white.
        for xy in ((0, 0), (63, 0), (0, 63), (63, 63)):
            self.assertEqual(badge.getpixel(xy)[3], 0)
        cx, cy = 32, 32
        r, gch, b, a = badge.getpixel((cx, 2))
        self.assertEqual(a, 255)
        self.assertEqual((r, gch, b), (255, 255, 255))
        # Interior has both ink and paper — a blank disc would fail this.
        blacks = whites = 0
        for y in range(64):
            for x in range(64):
                r, gch, b, a = badge.getpixel((x, y))
                if a < 16:
                    continue
                if r + gch + b < 48:
                    blacks += 1
                elif r + gch + b > 720:
                    whites += 1
                else:
                    self.fail(f"grey pixel at {(x, y)}: {(r, gch, b)}")
        self.assertGreater(blacks, 200)
        self.assertGreater(whites, 200)


class TestConvertWav(unittest.TestCase):
    def _write(self, path, rate, width, channels, frames):
        with wave.open(str(path), "wb") as wav:
            wav.setnchannels(channels)
            wav.setsampwidth(width)
            wav.setframerate(rate)
            wav.writeframes(frames)

    def test_16khz_mono_decimates_to_8khz_u8(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "clip.wav"
            # 8 samples at 16 kHz -> 4 at 8 kHz. Mid-scale silence (0).
            self._write(p, 16000, 2, 1, struct.pack("<8h", *([0] * 8)))
            pcm = g.convert_wav(p, g.PCM_MAX_SAMPLES)
            self.assertEqual(len(pcm), 4)
            for s in pcm:
                self.assertGreaterEqual(s, 120)
                self.assertLessEqual(s, 136)

    def test_caps_at_three_seconds(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "long.wav"
            n = g.I2S_RATE * 4  # 4 s at the driver's rate
            self._write(p, g.I2S_RATE, 1, 1, bytes([128] * n))
            pcm = g.convert_wav(p, g.PCM_MAX_SAMPLES)
            self.assertEqual(len(pcm), g.PCM_MAX_SAMPLES)

    def test_peak_normalize_uses_full_scale(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "quiet.wav"
            # 8 kHz, 4 samples, 16-bit at 10% of full scale.
            frames = struct.pack("<4h", 3277, -3277, 1638, 0)
            self._write(p, 8000, 2, 1, frames)
            quiet = g.convert_wav(p, g.PCM_MAX_SAMPLES)
            loud = g.convert_wav(p, g.PCM_MAX_SAMPLES, peak_normalize=True)
            self.assertEqual(len(loud), 4)
            # Un-normalized stays near 128; normalized should swing hard.
            self.assertLess(max(abs(s - 128) for s in quiet), 20)
            self.assertGreater(max(abs(s - 128) for s in loud), 100)

    def test_ship_clip_allows_four_seconds(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "ship.wav"
            n = g.I2S_RATE * 4
            self._write(p, g.I2S_RATE, 1, 1, bytes([128] * n))
            pcm = g.convert_wav(p, g.SHIP_PCM_MAX_SAMPLES)
            self.assertEqual(len(pcm), g.SHIP_PCM_MAX_SAMPLES)


class TestWriteOutputs(unittest.TestCase):
    def test_zero_samples_omits_pcm_array(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            header = tmp / "splash_art.h"
            source = tmp / "splash_art.c"
            pixels = [0] * (g.LCD_W * g.LCD_H)
            g.write_outputs(header, source, pixels, [], [])
            h = header.read_text(encoding="ascii")
            c = source.read_text(encoding="ascii")
            self.assertIn("#define SPLASH_BOOT_PCM_SAMPLES  0u", h)
            self.assertIn("#define SPLASH_SHIP_PCM_SAMPLES  0u", h)
            self.assertNotIn("splash_boot_pcm", h)
            self.assertNotIn("splash_boot_pcm", c)
            self.assertNotIn("splash_ship_pcm", h)

    def test_pcm_emits_u8_array(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            header = tmp / "splash_art.h"
            source = tmp / "splash_art.c"
            pixels = [0] * (g.LCD_W * g.LCD_H)
            g.write_outputs(header, source, pixels, [128, 200, 10], [])
            h = header.read_text(encoding="ascii")
            c = source.read_text(encoding="ascii")
            self.assertIn("#define SPLASH_BOOT_PCM_SAMPLES  3u", h)
            self.assertIn("extern const uint8_t splash_boot_pcm", h)
            self.assertIn("const uint8_t splash_boot_pcm[]", c)
            self.assertIn("#define SPLASH_SHIP_PCM_SAMPLES  0u", h)

    def test_ship_pcm_emits_u8_array(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            header = tmp / "splash_art.h"
            source = tmp / "splash_art.c"
            pixels = [0] * (g.LCD_W * g.LCD_H)
            g.write_outputs(header, source, pixels, [], [1, 2, 3, 4])
            h = header.read_text(encoding="ascii")
            c = source.read_text(encoding="ascii")
            self.assertIn("#define SPLASH_SHIP_PCM_SAMPLES  4u", h)
            self.assertIn("extern const uint8_t splash_ship_pcm", h)
            self.assertIn("const uint8_t splash_ship_pcm[]", c)


class TestComposite(unittest.TestCase):
    def test_seal_region_is_bw_not_gold(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            src = tmp / "nes_box.png"
            # Fake a gold disc where the real seal sits, plus a red pixel
            # that must survive outside the badge.
            canvas = Image.new("RGB", (g.LCD_W, g.LCD_H), (8, 8, 8))
            d = ImageDraw.Draw(canvas)
            d.ellipse(
                (g.SEAL_X, g.SEAL_Y, g.SEAL_X + g.SEAL_SIZE - 1, g.SEAL_Y + g.SEAL_SIZE - 1),
                fill=(200, 160, 40),
            )
            canvas.putpixel((10, 10), (255, 0, 0))
            canvas.save(src)
            pixels = g.convert_png(src)
            # Converted RGB565 of the badge centre must be black or white,
            # not gold (gold 200,160,40 -> 0xCCA5-ish).
            cx = g.SEAL_X + g.SEAL_SIZE // 2
            cy = g.SEAL_Y + g.SEAL_SIZE // 2
            pix = pixels[cy * g.LCD_W + cx]
            self.assertIn(pix, (0x0000, 0xFFFF))
            self.assertEqual(pixels[10 * g.LCD_W + 10], g.rgb565(255, 0, 0))
            # Preview is written next to the source.
            self.assertTrue((tmp / "nes_box_320.png").exists())


if __name__ == "__main__":
    unittest.main()
