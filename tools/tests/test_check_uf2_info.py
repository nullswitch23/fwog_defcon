"""Unit tests for check_uf2_info.py. No hardware, no build tree.

Built against gen_uf2_info.py's real output wherever possible, so the two
scripts cannot drift into agreeing with each other and with nothing else --
they are the two halves of one wire format.
"""
import os
import struct
import sys
import unittest
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                ".."))

from check_uf2_info import RecordError, check, scan_records   # noqa: E402
from gen_uf2_info import build_record                          # noqa: E402


def make(cpu=0, kind=0, version=17, name=b"bench",
         desc=b"Bench harness", build=b"", ts=0, corrupt_crc=False,
         struct_version=1):
    """A record built by hand, so the malformed cases gen_uf2_info.py refuses
    to produce can still be tested."""
    body = struct.pack("<II H B B H H 32s 128s 32s I",
                       0x4F475746, 0x4F464E49, struct_version,
                       cpu, kind, version, 0, name, desc, build, ts)
    crc = zlib.crc32(body) & 0xFFFFFFFF
    if corrupt_crc:
        crc ^= 1
    return body + struct.pack("<I", crc)


class TestScan(unittest.TestCase):
    def test_finds_a_record_anywhere_in_the_payload(self):
        blob = b"\x00" * 4096 + make() + b"\xff" * 512
        recs = scan_records(blob)
        self.assertEqual(len(recs), 1)
        self.assertEqual(recs[0].name, "bench")
        self.assertEqual(recs[0].app_version, 17)

    def test_finds_none_when_the_record_was_discarded(self):
        """Absence is NOT an exception here: the caller distinguishes 'the
        linker dropped it' from 'the generator is wrong'."""
        self.assertEqual(scan_records(b"\x00" * 8192), [])

    def test_rejects_a_bad_crc(self):
        with self.assertRaises(RecordError):
            scan_records(make(corrupt_crc=True))

    def test_rejects_an_unknown_struct_version(self):
        with self.assertRaises(RecordError):
            scan_records(make(struct_version=2))

    def test_rejects_unterminated_name(self):
        with self.assertRaises(RecordError):
            scan_records(make(name=b"x" * 32))

    def test_rejects_empty_description(self):
        with self.assertRaises(RecordError):
            scan_records(make(desc=b""))

    def test_rejects_a_truncated_record(self):
        with self.assertRaises(RecordError):
            scan_records(make()[:100])

    def test_two_records_are_both_returned(self):
        blob = make(cpu=1, name=b"template") + b"\x00" * 64 + make(cpu=0)
        self.assertEqual(sorted(r.cpu for r in scan_records(blob)), [0, 1])


class TestCheck(unittest.TestCase):
    """check() is the rule; scan_records() is only the parser."""

    def test_accepts_a_real_generated_display_record(self):
        blob = b"\x00" * 4096 + build_record(0, 0, 17, "bench",
                                             "Bench harness", "", 0)
        own, extra = check(blob, "display")
        self.assertEqual((own.name, own.app_version), ("bench", 17))
        self.assertEqual(extra, [])

    def test_rejects_a_display_app_carrying_build_info(self):
        """The whole point of the display build/build_ts rule: a build-varying
        byte changes the image CRC32 and forces a reflash every commit."""
        blob = make(cpu=0, kind=0, build=b"a91bf31", ts=1769558400)
        with self.assertRaises(RecordError) as cm:
            check(blob, "display")
        self.assertIn("build", str(cm.exception))

    def test_accepts_a_bootloader_carrying_build_info(self):
        """The bootloader is a DISPLAY-CPU binary and is exempt: it is
        UF2-flashed and has no CRC-skip path. Keying the rule on cpu alone
        would reject bl_display, which an earlier draft did."""
        blob = build_record(0, 1, 1, "bl", "Display serial bootloader",
                            "e6df789", 1785254462)
        own, _ = check(blob, "display")
        self.assertEqual(own.kind, 1)
        self.assertEqual(own.build, "e6df789")

    def test_main_carrying_a_display_image_is_accepted_and_reported(self):
        blob = (build_record(1, 0, 4, "template", "Main template",
                             "e6df789", 1785254462)
                + b"\x00" * 64
                + build_record(0, 0, 17, "bench", "Bench harness", "", 0))
        own, extra = check(blob, "main")
        self.assertEqual(own.name, "template")
        self.assertEqual([r.name for r in extra], ["bench"])

    def test_a_display_image_must_not_contain_a_main_record(self):
        blob = make(cpu=0) + b"\x00" * 32 + make(cpu=1, name=b"oops")
        with self.assertRaises(RecordError):
            check(blob, "display")

    def test_absence_is_reported_as_a_probable_discard(self):
        with self.assertRaises(RecordError) as cm:
            check(b"\x00" * 8192, "display")
        self.assertIn("discarded", str(cm.exception))

    def test_wrong_cpu_is_rejected(self):
        blob = build_record(1, 0, 4, "template", "Main template", "x", 1)
        with self.assertRaises(RecordError):
            check(blob, "display")


class TestGeneratorRefusals(unittest.TestCase):
    """gen_uf2_info.py refuses to emit records that check() would reject, so
    the failure lands at generate time with a clearer message."""

    def test_refuses_to_truncate_an_over_long_name(self):
        with self.assertRaises(SystemExit):
            build_record(0, 0, 1, "x" * 32, "d", "", 0)

    def test_refuses_an_over_long_description(self):
        with self.assertRaises(SystemExit):
            build_record(0, 0, 1, "n", "d" * 128, "", 0)


if __name__ == "__main__":
    unittest.main()
