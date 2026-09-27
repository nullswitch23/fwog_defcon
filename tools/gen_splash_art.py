#!/usr/bin/env python3
"""Pack the boot splash PNG as a full-panel 320x240 RGB565 array.

The source is NES-box *layout* (black border, framed illustration, title
band, corner seal), a homage to that framing as used by James Rolfe / Angry
Video Game Nerd. The illustration is original. Garbled generator lettering
in the title band is painted out so fwog_splash.c can overlay the bound
app name without fighting baked-in text.

The corner badge is the DEF CON smiley-and-crossbones mark, composited over
the gold Nintendo-ish seal in the source PNG. Loaded from art/defcon_seal.png
(copied from the repo-root DEF-CON-logo.png). Falls back to art/skully_seal.png.

Optional PCM: --wav is boot (3 s dwell). --ship-wav plays on 6 s red power-off,
just before the pack FET opens. Both clips are 8 kHz unsigned 8-bit mono.
There is no boot clip by default. The ship clip is
art/bad_feeling_8k.wav (converted from the Cyberpunk 2077 line).
"""

from pathlib import Path
import argparse
import struct
import wave

from PIL import Image, ImageDraw

LCD_W, LCD_H = 320, 240

# Leave the corner seal on the right; cover the title lettering.
TITLE_X, TITLE_Y = 16, 164
TITLE_W, TITLE_H = 232, 64

# Round badge covering the gold seal on the 320x240 panel. Measured on
# nes_box_320.png: the circular gold mark sits at about (280, 208).
SEAL_X, SEAL_Y, SEAL_SIZE = 250, 174, 64

# Splash dwell is 3 s; ship clip is slightly longer (the 2.9 s quote).
I2S_RATE = 8000
PCM_MAX_SAMPLES = I2S_RATE * 3
SHIP_PCM_MAX_SAMPLES = I2S_RATE * 4


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def emit_u16(out, name, values, per_line):
    out.write(f"const uint16_t {name}[] = {{\n")
    for i in range(0, len(values), per_line):
        row = values[i : i + per_line]
        out.write("    " + ", ".join(str(v) for v in row) + ",\n")
    out.write("};\n\n")


def emit_u8(out, name, values, per_line):
    out.write(f"const uint8_t {name}[] = {{\n")
    for i in range(0, len(values), per_line):
        row = values[i : i + per_line]
        out.write("    " + ", ".join(str(v) for v in row) + ",\n")
    out.write("};\n\n")


def draw_skully(size: int = 256) -> Image.Image:
    """Original B&W skull-in-circle. Drawn, not downloaded."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = size - 1
    white = (255, 255, 255, 255)
    black = (0, 0, 0, 255)

    d.ellipse((0, 0, s, s), fill=white)
    ring = max(2, size // 16)
    d.ellipse((ring, ring, s - ring, s - ring), fill=black)
    inner = ring + max(1, size // 64)
    d.ellipse((inner, inner, s - inner, s - inner), outline=white)

    cx = size / 2.0
    cy = size / 2.0

    cr_w, cr_h = size * 0.64, size * 0.54
    cr_y = cy - size * 0.07
    d.ellipse(
        (cx - cr_w / 2, cr_y - cr_h / 2, cx + cr_w / 2, cr_y + cr_h / 2),
        fill=white,
    )

    # Temples / zygomatic arches so the jaw reads as one skull, not two blobs.
    jw, jh = size * 0.44, size * 0.34
    jy = cy + size * 0.20
    d.polygon(
        [
            (cx - cr_w * 0.38, cr_y + cr_h * 0.15),
            (cx - jw * 0.52, jy - jh * 0.15),
            (cx - jw * 0.18, jy - jh * 0.35),
            (cx + jw * 0.18, jy - jh * 0.35),
            (cx + jw * 0.52, jy - jh * 0.15),
            (cx + cr_w * 0.38, cr_y + cr_h * 0.15),
        ],
        fill=white,
    )
    d.ellipse(
        (cx - jw / 2, jy - jh / 2, cx + jw / 2, jy + jh / 2),
        fill=white,
    )

    # Eye sockets.
    ew, eh = size * 0.17, size * 0.21
    eye_y = cr_y + size * 0.01
    eye_dx = size * 0.145
    for side in (-1.0, 1.0):
        ex = cx + side * eye_dx
        d.ellipse(
            (ex - ew / 2, eye_y - eh / 2, ex + ew / 2, eye_y + eh / 2),
            fill=black,
        )

    # Nasal cavity: inverted triangle with a slight curve at the top.
    ny = cr_y + size * 0.14
    nw = size * 0.09
    nh = size * 0.15
    d.polygon(
        [
            (cx, ny),
            (cx - nw, ny + nh),
            (cx + nw, ny + nh),
        ],
        fill=black,
    )

    # Short square teeth, not a tall grill.
    tooth_top = jy - size * 0.02
    tooth_bot = jy + size * 0.07
    gap = max(1, size // 64)
    d.rectangle(
        (cx - size * 0.15, tooth_top - gap, cx + size * 0.15, tooth_top + gap),
        fill=black,
    )
    for i in range(-2, 3):
        tx = cx + i * size * 0.06
        d.rectangle((tx - gap, tooth_top, tx + gap, tooth_bot), fill=black)
    return img


def _to_luma(im: Image.Image) -> Image.Image:
    """Hard B&W: anything not near-white becomes black, keep alpha."""
    rgba = im.convert("RGBA")
    px = list(rgba.getdata())
    out = []
    for r, g, b, a in px:
        if a < 16:
            out.append((0, 0, 0, 0))
            continue
        luma = (r * 299 + g * 587 + b * 114) // 1000
        v = 255 if luma >= 128 else 0
        out.append((v, v, v, 255))
    rgba.putdata(out)
    return rgba


def load_seal(art_dir: Path) -> Image.Image:
    """DEF CON mark first; skull only if that file is missing."""
    for name in ("defcon_seal.png", "skully_seal.png"):
        path = art_dir / name
        if path.exists():
            return _to_luma(Image.open(path))
    badge = draw_skully(256)
    (art_dir / "skully_seal.png").parent.mkdir(parents=True, exist_ok=True)
    badge.save(art_dir / "skully_seal.png")
    return badge


def paste_seal(fitted: Image.Image, art_dir: Path) -> None:
    """Erase the gold Nintendo-ish seal, then stamp the DEF CON mark."""
    badge = load_seal(art_dir)
    badge = badge.resize((SEAL_SIZE, SEAL_SIZE), Image.Resampling.LANCZOS)
    badge = _to_luma(badge)
    circ = Image.new("L", (SEAL_SIZE, SEAL_SIZE), 0)
    ImageDraw.Draw(circ).ellipse((0, 0, SEAL_SIZE - 1, SEAL_SIZE - 1), fill=255)
    black = Image.new("RGB", (SEAL_SIZE, SEAL_SIZE), (0, 0, 0))
    fitted.paste(black, (SEAL_X, SEAL_Y), circ)
    fitted.paste(badge.convert("RGB"), (SEAL_X, SEAL_Y), circ)


def convert_png(path: Path) -> list[int]:
    image = Image.open(path).convert("RGB")
    fitted = image.resize((LCD_W, LCD_H), Image.Resampling.LANCZOS)
    # Solid black title band so overlay text reads. Keep the seal column.
    for y in range(TITLE_Y, TITLE_Y + TITLE_H):
        for x in range(TITLE_X, TITLE_X + TITLE_W):
            fitted.putpixel((x, y), (0, 0, 0))
    paste_seal(fitted, path.parent)
    preview = path.parent / "nes_box_320.png"
    fitted.save(preview)
    return [rgb565(r, g, b) for r, g, b in fitted.getdata()]


def _mono_frames(raw: bytes, channels: int, sampwidth: int) -> list[float]:
    """Interleaved PCM to mono floats in [-1, 1]."""
    n = len(raw) // sampwidth
    if sampwidth == 1:
        samples = struct.unpack(f"{n}B", raw)
        floats = [(s - 128) / 128.0 for s in samples]
    elif sampwidth == 2:
        samples = struct.unpack(f"<{n}h", raw)
        floats = [s / 32768.0 for s in samples]
    else:
        raise ValueError(f"unsupported WAV sample width {sampwidth}")
    if channels == 1:
        return floats
    if channels < 1:
        raise ValueError("WAV has no channels")
    mono = []
    for i in range(0, len(floats) - channels + 1, channels):
        mono.append(sum(floats[i : i + channels]) / channels)
    return mono


def resample_linear(samples: list[float], src_hz: int, dst_hz: int) -> list[float]:
    if src_hz == dst_hz or not samples:
        return list(samples)
    if src_hz <= 0 or dst_hz <= 0:
        raise ValueError("sample rate must be positive")
    out_n = max(1, int(round(len(samples) * dst_hz / src_hz)))
    out = []
    scale = (len(samples) - 1) / max(1, out_n - 1)
    for i in range(out_n):
        pos = i * scale
        lo = int(pos)
        hi = min(lo + 1, len(samples) - 1)
        frac = pos - lo
        out.append(samples[lo] * (1.0 - frac) + samples[hi] * frac)
    return out


def convert_wav(
    path: Path, max_samples: int, peak_normalize: bool = False
) -> list[int]:
    """8 kHz unsigned 8-bit mono, truncated to max_samples.

    peak_normalize stretches the clip to ~97% of full scale so a quiet
    voice line is not still whisper-quiet after 8-bit expansion (*50 in
    i2s_audio_expand_8bit). Used for the ship clip.
    """
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        sampwidth = wav.getsampwidth()
        rate = wav.getframerate()
        raw = wav.readframes(wav.getnframes())
    mono = _mono_frames(raw, channels, sampwidth)
    pcm = resample_linear(mono, rate, I2S_RATE)
    if len(pcm) > max_samples:
        pcm = pcm[:max_samples]
    if peak_normalize and pcm:
        peak = max(abs(s) for s in pcm)
        if peak > 1e-6:
            scale = 0.97 / peak
            pcm = [s * scale for s in pcm]
    # Unsigned 8-bit, matching i2s_audio_expand_8bit()'s 0-255 source.
    out = []
    for s in pcm:
        if s < -1.0:
            s = -1.0
        if s > 1.0:
            s = 1.0
        out.append(int(round((s + 1.0) * 127.5)))
    return [min(255, max(0, v)) for v in out]


def write_outputs(
    header: Path,
    source: Path,
    pixels: list[int],
    boot_pcm: list[int],
    ship_pcm: list[int],
) -> None:
    assert len(pixels) == LCD_W * LCD_H
    with header.open("w", encoding="ascii", newline="\n") as out:
        out.write("/* Generated by tools/gen_splash_art.py. */\n")
        out.write("#ifndef FWOG_SPLASH_ART_H\n#define FWOG_SPLASH_ART_H\n")
        out.write("#include <stdint.h>\n\n")
        out.write(f"#define SPLASH_W        {LCD_W}u\n")
        out.write(f"#define SPLASH_H        {LCD_H}u\n")
        out.write(f"#define SPLASH_PIXELS   {LCD_W * LCD_H}u\n")
        out.write(f"#define SPLASH_TITLE_X  {TITLE_X + 4}u\n")
        out.write(f"#define SPLASH_TITLE_Y  {TITLE_Y + 4}u\n")
        out.write(f"#define SPLASH_BOOT_PCM_SAMPLES  {len(boot_pcm)}u\n")
        out.write(f"#define SPLASH_BOOT_PCM_8BIT     1\n")
        out.write(f"#define SPLASH_SHIP_PCM_SAMPLES  {len(ship_pcm)}u\n")
        out.write(f"#define SPLASH_SHIP_PCM_8BIT     1\n")
        out.write("\nextern const uint16_t splash_image[SPLASH_PIXELS];\n")
        if boot_pcm:
            out.write("extern const uint8_t splash_boot_pcm[SPLASH_BOOT_PCM_SAMPLES];\n")
        if ship_pcm:
            out.write("extern const uint8_t splash_ship_pcm[SPLASH_SHIP_PCM_SAMPLES];\n")
        out.write("\n#endif\n")

    with source.open("w", encoding="ascii", newline="\n") as out:
        out.write("/* Generated by tools/gen_splash_art.py. */\n")
        out.write('#include "lcd/splash_art.h"\n\n')
        emit_u16(out, "splash_image", pixels, 12)
        if boot_pcm:
            emit_u8(out, "splash_boot_pcm", boot_pcm, 16)
        if ship_pcm:
            emit_u8(out, "splash_ship_pcm", ship_pcm, 16)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("png", type=Path)
    parser.add_argument("header", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument(
        "--wav",
        type=Path,
        default=None,
        help="optional boot clip; omitted leaves SPLASH_BOOT_PCM_SAMPLES 0",
    )
    parser.add_argument(
        "--ship-wav",
        type=Path,
        default=None,
        help="optional power-off clip; omitted leaves SPLASH_SHIP_PCM_SAMPLES 0",
    )
    args = parser.parse_args()

    pixels = convert_png(args.png)
    boot_pcm: list[int] = []
    ship_pcm: list[int] = []
    if args.wav is not None:
        boot_pcm = convert_wav(args.wav, PCM_MAX_SAMPLES)
    if args.ship_wav is not None:
        ship_pcm = convert_wav(
            args.ship_wav, SHIP_PCM_MAX_SAMPLES, peak_normalize=True
        )
    write_outputs(args.header, args.source, pixels, boot_pcm, ship_pcm)


if __name__ == "__main__":
    main()
