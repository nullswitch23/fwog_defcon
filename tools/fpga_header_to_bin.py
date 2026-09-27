#!/usr/bin/env python3
"""Convert the reference firmware's FPGA bitstream C header to a raw .bin.

The reference ships freewilimain/fpgabitstreams/fpga_bit_default_v5.h -- a
1 MB C header holding 104,090 bytes as comma-separated hex. This extracts the
bytes so the BSP can .incbin them instead of compiling a million-token array.

Run once; the .bin is checked in. Re-run only when the gateware changes.
"""
import argparse
import hashlib
import re
import sys
from pathlib import Path

BYTE_RE = re.compile(rb"0[xX]([0-9a-fA-F]{1,2})\s*,")
SIZE_RE = re.compile(rb"#define\s+FPGA_BITFILE_SIZE\s+(\d+)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("header", type=Path)
    ap.add_argument("out", type=Path)
    args = ap.parse_args()

    text = args.header.read_bytes()

    m = SIZE_RE.search(text)
    if not m:
        print("error: no FPGA_BITFILE_SIZE in header", file=sys.stderr)
        return 1
    declared = int(m.group(1))

    data = bytes(int(b, 16) for b in BYTE_RE.findall(text))
    if len(data) != declared:
        print(f"error: parsed {len(data)} bytes, header declares {declared}",
              file=sys.stderr)
        return 1

    args.out.write_bytes(data)
    print(f"{args.out}: {len(data)} bytes, "
          f"sha256 {hashlib.sha256(data).hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
