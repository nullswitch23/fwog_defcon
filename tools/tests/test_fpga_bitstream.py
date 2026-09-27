"""The iCE40 bitstream is frozen. This is what freezes it.

WHY THIS EXISTS

On 2026-07-28 bsp/main_cpu/fpga/'s bitstream was REPLACED, because the one
there had come from the multi-target reference tree and was FreeWili 2
gateware. It had the same 104090 bytes, the same symbol name and the same size
constant as FW1's, with 41.4% of its bytes different -- two genuinely
different designs, indistinguishable by everything the tree checked.

Nothing caught it, and nothing could have:

  - Size alone does not: the two are byte-for-byte the same length. The
    SHA-256 below is the load-bearing half of this test.
  - CDONE=1 does not (the hardware record). It reports structural acceptance of a
    well-formed bitstream, so it comes up identically for the wrong gateware.
    Only provenance can tell them apart, and provenance in a comment is not
    checkable.

WHY PYTHON AND NOT A HOST C TEST

The bytes are not a C array. fpga_bitstream.S pulls them in with .incbin, so a
C test would need the tests/ tree to enable_language(ASM) -- and that tree
deliberately supports a Windows fallback onto cl.exe, which cannot assemble
it. Checking the file .incbin embeds tests the same bytes without that cost.

The digest here is the one recorded in fpga_bitstream.h's provenance note.
There is deliberately ONE number rather than a second checksum in another
form, so the two cannot drift apart.
"""
import hashlib
import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
FPGA_DIR = os.path.join(HERE, "..", "..", "bsp", "main_cpu", "fpga")

BIN_PATH = os.path.join(FPGA_DIR, "fpga_default_v5.bin")
HEADER_PATH = os.path.join(FPGA_DIR, "fpga_bitstream.h")
ASM_PATH = os.path.join(FPGA_DIR, "fpga_bitstream.S")

# The FreeWili 1-only firmware repository at tag spartahackFw1final -- the last
# FreeWili 1 release, and the gateware a shipped OG actually runs. It is the
# authority for anything OG. See fpga_bitstream.h for the full
# provenance argument and for the digest of the bitstream this REPLACED.
EXPECTED_SIZE = 104090
EXPECTED_SHA256 = \
    "8a5c1ecad97bca9862dd7ccc0130e72454dc6b1bb0231ca3b16e25ca1e239a91"

# The bitstream this replaced, from the multi-target tree. Named so that
# "restoring" it is a loud failure rather than a quiet regression.
FW2_SHA256 = \
    "113da0ba5e8aa4b212f1de43e8900c771dc5340473f5eb6a87dd3b8318cc5589"


def _bitstream_bytes():
    with open(BIN_PATH, "rb") as f:
        return f.read()


class TestFpgaBitstream(unittest.TestCase):
    def test_size_is_pinned(self):
        self.assertEqual(len(_bitstream_bytes()), EXPECTED_SIZE)

    def test_sha256_is_pinned(self):
        got = hashlib.sha256(_bitstream_bytes()).hexdigest()
        self.assertEqual(
            got, EXPECTED_SHA256,
            "the FPGA bitstream's bytes changed. This is not a test to "
            "update: read fpga_bitstream.h's provenance note first. A "
            "bitstream of identical size and symbol name has already been "
            "swapped for FreeWili 2 gateware once.")

    def test_is_not_the_freewili2_bitstream_that_was_removed(self):
        """The specific regression, called out by name.

        CDONE came up for this one too, which is exactly why it survived."""
        got = hashlib.sha256(_bitstream_bytes()).hexdigest()
        self.assertNotEqual(
            got, FW2_SHA256,
            "this is the FreeWili 2 bitstream removed on 2026-07-28. It "
            "configures the part and reports CDONE=1, and it is still wrong.")

    def test_header_size_constant_matches_the_file(self):
        """The constant and the bytes are two places the size is stated.

        A mismatch means the loader would program a length that is not the
        file's, which no runtime check on this board would report."""
        with open(HEADER_PATH) as f:
            src = f.read()
        m = re.search(r"#define\s+FWOG_FPGA_BITSTREAM_SIZE\s+(\d+)u", src)
        self.assertIsNotNone(m, "FWOG_FPGA_BITSTREAM_SIZE not found")
        self.assertEqual(int(m.group(1)), EXPECTED_SIZE)

    def test_asm_embeds_the_file_this_test_checks(self):
        """Without this, the test could pin a file nothing includes."""
        with open(ASM_PATH) as f:
            src = f.read()
        self.assertIn('.incbin "fpga_default_v5.bin"', src)

    def test_header_records_the_digest_this_test_pins(self):
        """One number, in two places that must agree: the provenance note a
        human reads and the constant a machine checks."""
        with open(HEADER_PATH) as f:
            src = f.read()
        self.assertIn(EXPECTED_SHA256, src)


if __name__ == "__main__":
    unittest.main()
