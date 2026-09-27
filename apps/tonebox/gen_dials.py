#!/usr/bin/env python3
import pathlib

root = pathlib.Path(__file__).resolve().parent
dial = root / "dial_files"
out = root / "tb_dials_data.c"
files = [
    ("tb_dial_national_kp1", "national_call_kp1"),
    ("tb_dial_intl_kp2", "international_transit_kp2"),
    ("tb_dial_bf_pin2", "bruteforce_pin_2_with_dial"),
    ("tb_dial_phone_tpl", "030606131.txt"),
]


def c_escape(text: str) -> str:
    out = []
    for ch in text:
        o = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ch == "\r":
            out.append("\\r")
        elif o < 32 or o == 127:
            out.append(f"\\x{o:02x}")
        else:
            out.append(ch)
    return "".join(out)


lines = ['#include "tb_dials.h"\n']
for sym, fn in files:
    data = (dial / fn).read_bytes()
    text = data.decode("utf-8", errors="replace")
    lines.append(f'const char {sym}[] = "{c_escape(text)}";\n')
    lines.append(f"const unsigned {sym}_len = {len(data)}u;\n\n")
lines.append(
    "const tb_dial_t tb_builtin_dials[] = {\n"
    '    { "national KP1", tb_dial_national_kp1, tb_dial_national_kp1_len },\n'
    '    { "intl transit KP2", tb_dial_intl_kp2, tb_dial_intl_kp2_len },\n'
    '    { "bf pin 2+dial", tb_dial_bf_pin2, tb_dial_bf_pin2_len },\n'
    "};\n"
    "const unsigned tb_builtin_dials_n =\n"
    "    (unsigned)(sizeof tb_builtin_dials / sizeof tb_builtin_dials[0]);\n"
)
out.write_text("".join(lines), encoding="utf-8")
print("wrote", out)
