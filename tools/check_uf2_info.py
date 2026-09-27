"""Prove the UF2 info record survived the link, and says something true.

This is the only check in the tree that inspects the ARTEFACT rather than the
source, which is what makes it the only one that can catch:

  - the linker discarding a record nothing references,
  - a generator that wrote a CRC over the wrong bytes,
  - a display application that picked up build-varying bytes it must not have.

The first of those is not hypothetical. On the first build of this feature the
record was absent from all eight binaries: __attribute__((retain)) is ignored
by this toolchain and --gc-sections dropped it. Nothing else in the build
noticed -- every app compiled, linked and produced a UF2. This script is what
turned that into a failure.

It reads the .bin, not the .uf2: pico_add_extra_outputs() already produces the
flat image, so there are no 512-byte blocks to reassemble.

See bsp/common/uf2_info.h for the record's layout and purpose.
"""
import argparse
import struct
import sys
import zlib

MAGIC = b"FWGOINFO"
SIZE = 216

# BODY is the 212 bytes the CRC covers (11 fields); FULL adds the trailing
# crc32 for a 216-byte record (12 fields). Unpacking a full record with BODY
# raises struct.error, so keep these distinct. gen_uf2_info.py spells BODY
# identically.
BODY = "<II H B B H H 32s 128s 32s I"
FULL = BODY + " I"

CPU_NAME = {0: "display", 1: "main"}
KIND_NAME = {0: "app", 1: "bootloader"}


class RecordError(Exception):
    """A record was found and is not usable, OR a required one is missing."""


def _text(raw, off, what):
    if b"\x00" not in raw:
        raise RecordError(
            "record at 0x%X: %s is not NUL-terminated" % (off, what))
    return raw.split(b"\x00", 1)[0].decode("ascii", "replace")


class Record(object):
    def __init__(self, off, fields):
        (_m0, _m1, self.struct_version, self.cpu, self.kind,
         self.app_version, _reserved, name, desc, build, self.build_ts,
         self.crc32) = fields
        self.offset = off
        self.name = _text(name, off, "name")
        self.description = _text(desc, off, "description")
        self.build = _text(build, off, "build")

    def __repr__(self):
        return "<%s %s %s %03d>" % (CPU_NAME.get(self.cpu, "?"),
                                    KIND_NAME.get(self.kind, "?"),
                                    self.name, self.app_version)


def scan_records(blob):
    """Every valid record in `blob`, in offset order.

    A malformed record RAISES; a blob with no records returns []. Those are
    different failures and the caller distinguishes them: absence means the
    linker dropped it, malformation means the generator is wrong."""
    out = []
    start = 0
    while True:
        off = blob.find(MAGIC, start)
        if off < 0:
            return out
        start = off + 1
        if off + SIZE > len(blob):
            raise RecordError("record at 0x%X is truncated" % off)
        chunk = blob[off:off + SIZE]
        want = zlib.crc32(chunk[:SIZE - 4]) & 0xFFFFFFFF
        got = struct.unpack("<I", chunk[SIZE - 4:])[0]
        if got != want:
            raise RecordError(
                "record at 0x%X: crc32 is 0x%08X, computed 0x%08X"
                % (off, got, want))
        r = Record(off, struct.unpack(FULL, chunk))
        if r.struct_version != 1:
            raise RecordError("record at 0x%X: struct_version %d is not 1"
                              % (off, r.struct_version))
        if r.cpu not in CPU_NAME:
            raise RecordError("record at 0x%X: cpu %d is not 0 or 1"
                              % (off, r.cpu))
        if r.kind not in KIND_NAME:
            raise RecordError("record at 0x%X: kind %d is not 0 or 1"
                              % (off, r.kind))
        if r.app_version > 999:
            raise RecordError("record at 0x%X: app_version %d exceeds 999"
                              % (off, r.app_version))
        if not r.name:
            raise RecordError("record at 0x%X: name is empty" % off)
        if not r.description:
            raise RecordError("record at 0x%X: description is empty" % off)
        out.append(r)


def check(blob, expect_cpu):
    """The rule, separated from argv so it is testable.

    Returns (own_record, extra_records)."""
    recs = scan_records(blob)
    if not recs:
        raise RecordError(
            "no FWGOINFO record found. The linker most likely discarded it -- "
            "check that fwog_add_uf2_info() still passes "
            "-Wl,--undefined=fwog_uf2_info and that gen_uf2_info.py ran. "
            "This exact failure happened once already; see uf2_info.h.")

    want = 1 if expect_cpu == "main" else 0
    own = [r for r in recs if r.cpu == want]
    if len(own) != 1:
        raise RecordError(
            "expected exactly one %s record, found %d (%r)"
            % (expect_cpu, len(own), recs))

    r = own[0]
    # A display APPLICATION's bytes must be a pure function of its source: the
    # main-side updater skips the transfer by comparing the image CRC32, so a
    # build-varying byte forces a reflash on every commit.
    #
    # kind is part of the condition, not decoration. The BOOTLOADER is also a
    # display-CPU binary and DOES carry build info -- it is UF2-flashed and
    # has no CRC-skip path.
    if r.cpu == 0 and r.kind == 0 and (r.build or r.build_ts):
        raise RecordError(
            "display application record carries build=%r build_ts=%d; both "
            "must be empty or the image CRC32 changes every commit and forces "
            "a reflash. See display_update.c and uf2_info.h."
            % (r.build, r.build_ts))

    extra = [x for x in recs if x.cpu != want]
    # A main UF2 embeds the display image, so the display's own record is
    # present too and is expected. A display image must NOT contain a main
    # record -- nothing embeds anything into it.
    if expect_cpu == "display" and extra:
        raise RecordError(
            "a display image must not contain a main record; found %d (%r)"
            % (len(extra), extra))
    return r, extra


def main():
    p = argparse.ArgumentParser(description="Check a built image's UF2 info "
                                            "record.")
    p.add_argument("--bin", required=True)
    p.add_argument("--expect-cpu", required=True, choices=("display", "main"))
    a = p.parse_args()

    try:
        with open(a.bin, "rb") as f:
            blob = f.read()
    except OSError as e:
        print("check_uf2_info: %s" % e, file=sys.stderr)
        return 1

    try:
        own, extra = check(blob, a.expect_cpu)
    except RecordError as e:
        print("%s: %s" % (a.bin, e), file=sys.stderr)
        return 1

    note = ""
    if extra:
        note = " (carrying %s)" % ", ".join(
            "%s %s %03d" % (CPU_NAME[r.cpu], r.name, r.app_version)
            for r in extra)
    print("uf2 info: %s %s %s %03d%s"
          % (a.expect_cpu, KIND_NAME[own.kind], own.name, own.app_version,
             note))
    return 0


if __name__ == "__main__":
    sys.exit(main())
