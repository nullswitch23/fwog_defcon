#!/usr/bin/env python3
"""Emit a tiny wiliwasm guest: 32x24 plasma + rainbow LEDs, then return.

No wasi-sdk required. Output is dg_smoke_wasm.h next to this script.
"""
from __future__ import annotations

from pathlib import Path

W, H = 32, 24
FB_OFF = 16
FRAMES = 48
WAIT_MS = 60

# locals in _start
T, FRAME, Y, X, V, LED, R, G, B, TMP = range(10)


def leb(n: int) -> bytes:
    out = bytearray()
    if n < 0:
        raise ValueError("unsigned leb")
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def vec(items: list[bytes]) -> bytes:
    return leb(len(items)) + b"".join(items)


def section(sid: int, payload: bytes) -> bytes:
    return bytes([sid]) + leb(len(payload)) + payload


def i32c(n: int) -> bytes:
    """Signed LEB i32.const."""
    out = bytearray([0x41])
    n = n & 0xFFFFFFFF
    if n >= 0x80000000:
        n -= 0x100000000
    while True:
        b = n & 0x7F
        n >>= 7
        if (n == 0 and (b & 0x40) == 0) or (n == -1 and (b & 0x40)):
            out.append(b)
            return bytes(out)
        out.append(b | 0x80)


def get(i: int) -> bytes:
    return bytes([0x20, i])


def set_(i: int) -> bytes:
    return bytes([0x21, i])


def tee(i: int) -> bytes:
    return bytes([0x22, i])


def add() -> bytes:
    return bytes([0x6A])


def sub() -> bytes:
    return bytes([0x6B])


def mul() -> bytes:
    return bytes([0x6C])


def and_() -> bytes:
    return bytes([0x71])


def or_() -> bytes:
    return bytes([0x72])


def xor() -> bytes:
    return bytes([0x73])


def shl() -> bytes:
    return bytes([0x74])


def shr_u() -> bytes:
    return bytes([0x76])


def lt_u() -> bytes:
    return bytes([0x49])


def ge_u() -> bytes:
    return bytes([0x4F])


def eqz() -> bytes:
    return bytes([0x45])


def call(idx: int) -> bytes:
    return bytes([0x10, idx])


def store8() -> bytes:
    return bytes([0x3A, 0x00, 0x00])  # align 0, offset 0


def block(body: bytes) -> bytes:
    return bytes([0x02, 0x40]) + body + bytes([0x0B])


def loop(body: bytes) -> bytes:
    return bytes([0x03, 0x40]) + body + bytes([0x0B])


def br_if(d: int) -> bytes:
    return bytes([0x0D, d])


def br(d: int) -> bytes:
    return bytes([0x0C, d])


def iff(then: bytes, els: bytes | None = None) -> bytes:
    b = bytes([0x04, 0x40]) + then
    if els is not None:
        b += bytes([0x05]) + els
    return b + bytes([0x0B])


def emit_body() -> bytes:
    # pixel: v = (x*19 + y*13 + t*11) & 255
    # rgb332 packed into one byte, stored at FB_OFF + y*W + x
    pix = (
        get(X)
        + i32c(19)
        + mul()
        + get(Y)
        + i32c(13)
        + mul()
        + add()
        + get(T)
        + i32c(11)
        + mul()
        + add()
        + get(X)
        + get(Y)
        + mul()
        + xor()
        + i32c(255)
        + and_()
        + set_(V)
        # rrr = v>>5, ggg = (v+x)>>2, bb = (v+3y) ; pack
        + get(V)
        + i32c(5)
        + shr_u()
        + i32c(7)
        + and_()
        + i32c(5)
        + shl()
        + get(V)
        + get(X)
        + add()
        + i32c(2)
        + shr_u()
        + i32c(7)
        + and_()
        + i32c(2)
        + shl()
        + or_()
        + get(V)
        + get(Y)
        + i32c(3)
        + mul()
        + add()
        + i32c(3)
        + and_()
        + or_()
        + set_(TMP)
        + i32c(FB_OFF)
        + get(Y)
        + i32c(W)
        + mul()
        + add()
        + get(X)
        + add()
        + get(TMP)
        + store8()
        + get(X)
        + i32c(1)
        + add()
        + tee(X)
        + i32c(W)
        + lt_u()
        + br_if(0)
    )
    row = (
        i32c(0)
        + set_(X)
        + loop(pix)
        + get(Y)
        + i32c(1)
        + add()
        + tee(Y)
        + i32c(H)
        + lt_u()
        + br_if(0)
    )

    # hue wheel for LED `led`, hue = (t*10 + led*37) & 255
    hue = get(T) + i32c(10) + mul() + get(LED) + i32c(37) + mul() + add() + i32c(255) + and_() + set_(TMP)
    # if hue < 85: r=255-hue*3, g=hue*3, b=0
    # elif hue < 170: r=0, g=255-(hue-85)*3, b=(hue-85)*3
    # else: r=(hue-170)*3, g=0, b=255-(hue-170)*3
    wheel = (
        hue
        + get(TMP)
        + i32c(85)
        + lt_u()
        + iff(
            i32c(255)
            + get(TMP)
            + i32c(3)
            + mul()
            + sub()
            + set_(R)
            + get(TMP)
            + i32c(3)
            + mul()
            + set_(G)
            + i32c(0)
            + set_(B),
            get(TMP)
            + i32c(170)
            + lt_u()
            + iff(
                i32c(0)
                + set_(R)
                + i32c(255)
                + get(TMP)
                + i32c(85)
                + sub()
                + i32c(3)
                + mul()
                + sub()
                + set_(G)
                + get(TMP)
                + i32c(85)
                + sub()
                + i32c(3)
                + mul()
                + set_(B),
                get(TMP)
                + i32c(170)
                + sub()
                + i32c(3)
                + mul()
                + set_(R)
                + i32c(0)
                + set_(G)
                + i32c(255)
                + get(TMP)
                + i32c(170)
                + sub()
                + i32c(3)
                + mul()
                + sub()
                + set_(B),
            ),
        )
        + get(LED)
        + get(R)
        + get(G)
        + get(B)
        + i32c(0)
        + i32c(0)
        + call(1)  # setBoardLED
        + get(LED)
        + i32c(1)
        + add()
        + tee(LED)
        + i32c(7)
        + lt_u()
        + br_if(0)
    )

    frame = (
        i32c(0)
        + set_(Y)
        + loop(row)
        + i32c(FB_OFF)
        + i32c(W)
        + i32c(H)
        + call(2)  # showGfx
        + i32c(0)
        + set_(LED)
        + loop(wheel)
        + i32c(WAIT_MS)
        + call(0)  # waitms
        + get(T)
        + i32c(7)
        + add()
        + set_(T)
        + get(FRAME)
        + i32c(1)
        + add()
        + tee(FRAME)
        + i32c(FRAMES)
        + lt_u()
        + br_if(0)
    )

    expr = i32c(0) + set_(T) + i32c(0) + set_(FRAME) + loop(frame)
    locals_decl = vec([bytes([10, 0x7F])])  # 10 x i32
    inner = locals_decl + expr + bytes([0x0B])
    return leb(len(inner)) + inner


def build() -> bytes:
    # type 0: []->[]          _start
    # type 1: [i32]->[]       waitms
    # type 2: [i32 x6]->[]    setBoardLED
    # type 3: [i32 x3]->[]    showGfx
    t0 = bytes([0x60, 0x00, 0x00])
    t1 = bytes([0x60, 0x01, 0x7F, 0x00])
    t2 = bytes([0x60, 0x06, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x00])
    t3 = bytes([0x60, 0x03, 0x7F, 0x7F, 0x7F, 0x00])
    types = section(1, vec([t0, t1, t2, t3]))

    def imp(mod: str, name: str, tidx: int) -> bytes:
        mb, nb = mod.encode(), name.encode()
        return leb(len(mb)) + mb + leb(len(nb)) + nb + bytes([0x00, tidx])

    imports = section(
        2,
        vec(
            [
                imp("wiliwasm", "waitms", 1),
                imp("wiliwasm", "setBoardLED", 2),
                imp("wiliwasm", "showGfx", 3),
            ]
        ),
    )
    functions = section(3, vec([bytes([0x00])]))  # _start : type 0
    memory = section(5, vec([bytes([0x00, 0x01])]))  # 1 page
    export_name = b"_start"
    exports = section(
        7,
        vec([leb(len(export_name)) + export_name + bytes([0x00, 0x03])]),
    )
    code = section(10, vec([emit_body()]))
    return (
        b"\x00asm\x01\x00\x00\x00"
        + types
        + imports
        + functions
        + memory
        + exports
        + code
    )


def main() -> None:
    blob = build()
    here = Path(__file__).resolve().parent
    wasm_path = here / "smoke.wasm"
    hdr_path = here / "dg_smoke_wasm.h"
    wasm_path.write_bytes(blob)
    lines = [
        "/* Generated by gen_smoke_wasm.py - do not edit. */",
        "#ifndef DG_SMOKE_WASM_H",
        "#define DG_SMOKE_WASM_H",
        "#include <stdint.h>",
        f"#define DG_SMOKE_WASM_LEN {len(blob)}u",
        "static const uint8_t k_dg_smoke_wasm[DG_SMOKE_WASM_LEN] = {",
    ]
    row = []
    for i, b in enumerate(blob):
        row.append(f"0x{b:02x}")
        if len(row) == 12:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines += ["};", "#endif", ""]
    hdr_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {wasm_path.name} ({len(blob)} bytes) and {hdr_path.name}")


if __name__ == "__main__":
    main()
