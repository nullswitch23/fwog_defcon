import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import genimage  # noqa: E402


class TestCrcAgreesWithC(unittest.TestCase):
    # These are the exact values tests/test_image_crc.c asserts against
    # fwog_crc32(). The bootloader verifies the image with the C
    # implementation and main announces the CRC computed with this one, so a
    # divergence would make every update fail its verify with no clue why.
    def test_check_value(self):
        self.assertEqual(genimage.crc32_ieee(b"123456789"), 0xCBF43926)

    def test_empty(self):
        self.assertEqual(genimage.crc32_ieee(b""), 0x00000000)

    def test_zeros_and_ones(self):
        self.assertEqual(genimage.crc32_ieee(b"\x00" * 32), 0x190A55AD)
        self.assertEqual(genimage.crc32_ieee(b"\xff" * 32), 0xFF6CAB0B)

    def test_ramp(self):
        self.assertEqual(genimage.crc32_ieee(bytes(range(256))), 0x29058C73)


class TestSanitizeVersion(unittest.TestCase):
    def test_plain(self):
        self.assertEqual(genimage.sanitize_version("v1.2.3"), "v1.2.3")

    def test_strips_and_defaults(self):
        self.assertEqual(genimage.sanitize_version("  v1  "), "v1")
        self.assertEqual(genimage.sanitize_version(""), "dev")
        self.assertEqual(genimage.sanitize_version(None), "dev")
        self.assertEqual(genimage.sanitize_version("   "), "dev")

    def test_truncates_to_fit_char32(self):
        # git describe on a tagged build overruns char[32] easily, and an
        # over-long initializer is a compile error, not a truncation.
        long = "v1.2.3-rc4-1234-gdeadbeefcafebabe-dirty"
        out = genimage.sanitize_version(long)
        self.assertEqual(len(out), genimage.VERSION_MAX)
        self.assertTrue(long.startswith(out))

    def test_escapes_c_string_metacharacters(self):
        self.assertEqual(genimage.sanitize_version('a"b'), 'a\\"b')
        self.assertEqual(genimage.sanitize_version("a\\b"), "a\\\\b")

    def test_drops_control_characters(self):
        # A newline in the version would break the generated line in two.
        self.assertEqual(genimage.sanitize_version("v1\nv2"), "v1v2")


class TestRender(unittest.TestCase):
    def test_asm_uses_absolute_forward_slash_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            b = pathlib.Path(tmp) / "app.bin"
            b.write_bytes(b"\x01\x02")
            s = genimage.render_asm(str(b))
            self.assertIn(".incbin", s)
            self.assertIn(b.resolve().as_posix(), s)
            self.assertNotIn("\\", s)          # no backslashes for the assembler
            self.assertIn(".global fwog_display_image", s)
            self.assertIn("fwog_display_image_end:", s)
            self.assertIn(".balign 4", s)

    def test_c_uses_designated_initializers(self):
        s = genimage.render_c(1234, 0xDEADBEEF, 99, "v1")
        self.assertIn(".size     = 1234u", s)
        self.assertIn(".crc32    = 0xDEADBEEFu", s)
        self.assertIn(".build_ts = 99u", s)
        self.assertIn('.version  = "v1"', s)
        self.assertIn('#include "display_update/display_image.h"', s)


class TestGenerate(unittest.TestCase):
    def test_writes_both_files_with_correct_size_and_crc(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            data = bytes(range(256)) * 3
            b = root / "template_display.bin"
            b.write_bytes(data)
            out = root / "gen"

            size, crc, ts, ver = genimage.generate(str(b), str(out), "v9")

            self.assertEqual(size, len(data))
            self.assertEqual(crc, genimage.crc32_ieee(data))
            self.assertEqual(ts, int(b.stat().st_mtime))
            self.assertEqual(ver, "v9")
            self.assertTrue((out / "display_image.S").exists())
            self.assertTrue((out / "display_image.c").exists())
            self.assertIn(f".size     = {len(data)}u",
                          (out / "display_image.c").read_text())

    def test_deterministic_for_the_same_input(self):
        # The generated files must not change when nothing changed, or main
        # relinks on every build and CMake's dependency tracking is moot.
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            b = root / "app.bin"
            b.write_bytes(b"hello world")
            genimage.generate(str(b), str(root / "a"), "v1")
            genimage.generate(str(b), str(root / "b"), "v1")
            for name in ("display_image.c",):
                self.assertEqual((root / "a" / name).read_text(),
                                 (root / "b" / name).read_text())

    def test_rejects_an_empty_bin(self):
        # An empty .bin means the display app did not link. Failing here is
        # far better than main shipping a zero-byte image and the display
        # bootloader rejecting it forever.
        with tempfile.TemporaryDirectory() as tmp:
            b = pathlib.Path(tmp) / "app.bin"
            b.write_bytes(b"")
            with self.assertRaises(ValueError):
                genimage.generate(str(b), tmp, "v1")


if __name__ == "__main__":
    unittest.main()
