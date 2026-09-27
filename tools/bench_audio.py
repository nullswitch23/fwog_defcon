#!/usr/bin/env python3
"""Closed-loop audio helpers for bench-testing the FreeWili OG display CPU.

Two loops, both of which close through the PC's own sound hardware:

  play_tone()   PC speakers -> the board's PDM microphone.  The board reports
                what it heard on its USB CDC console; this side only has to
                emit a known tone.
  record()      the board's I2S speaker -> a PC microphone.  This side records
                and reports the dominant frequency, so "did the speaker play
                440 Hz" becomes a measurement rather than someone listening.

`dominant_freq()` is a plain Goertzel-free FFT peak-pick over a Hann window.
scipy is not installed on this machine; numpy alone is enough.

Nothing here touches the board.  Keeping the two halves separate means a
failure is attributable: if the PC loopback self-test passes and the board
loop does not, the board is at fault, not the sound setup.
"""
import sys

import numpy as np
import sounddevice as sd

SR = 48000


def dominant_freq(samples, sr=SR, lo=100.0, hi=8000.0):
    """Peak frequency in `samples` within [lo, hi], plus its relative strength.

    Returns (hz, snr) where snr is the peak bin's magnitude divided by the
    median magnitude in band -- a crude but honest "is this a tone or is this
    room noise" number.  A pure tone lands in the hundreds; silence lands
    near 1.
    """
    x = np.asarray(samples, dtype=np.float64).ravel()
    if x.size == 0:
        return 0.0, 0.0
    x = x - x.mean()
    win = np.hanning(x.size)
    mag = np.abs(np.fft.rfft(x * win))
    freqs = np.fft.rfftfreq(x.size, 1.0 / sr)
    band = (freqs >= lo) & (freqs <= hi)
    if not band.any():
        return 0.0, 0.0
    mag_b, freqs_b = mag[band], freqs[band]
    k = int(np.argmax(mag_b))
    med = float(np.median(mag_b)) or 1e-12
    return float(freqs_b[k]), float(mag_b[k] / med)


def rms(samples):
    x = np.asarray(samples, dtype=np.float64).ravel()
    return float(np.sqrt(np.mean(x * x))) if x.size else 0.0


def play_tone(hz, seconds, device=None, amplitude=0.35, blocking=True):
    """Emit a sine at `hz` from a PC output device."""
    n = int(SR * seconds)
    t = np.arange(n, dtype=np.float64) / SR
    # Ramp the first and last 5 ms so the speaker is not asked for a step.
    sig = amplitude * np.sin(2.0 * np.pi * hz * t)
    ramp = max(1, int(SR * 0.005))
    env = np.ones(n)
    env[:ramp] = np.linspace(0.0, 1.0, ramp)
    env[-ramp:] = np.linspace(1.0, 0.0, ramp)
    sd.play((sig * env).astype(np.float32), SR, device=device, blocking=blocking)


def record(seconds, device=None):
    """Capture mono from a PC input device and return a float32 array."""
    buf = sd.rec(int(SR * seconds), samplerate=SR, channels=1, device=device,
                 dtype="float32", blocking=True)
    return buf.ravel()


def selftest(out_device=None, in_device=None, hz=1000.0, seconds=1.0):
    """Prove the PC's own speaker->mic path works before blaming the board.

    Plays a tone and records at the same time.  Returns (ok, detected_hz, snr).
    """
    n = int(SR * seconds)
    t = np.arange(n, dtype=np.float64) / SR
    sig = (0.35 * np.sin(2.0 * np.pi * hz * t)).astype(np.float32)
    rec = sd.playrec(sig, samplerate=SR, channels=1,
                     device=(in_device, out_device), dtype="float32",
                     blocking=True).ravel()
    # Drop the first 100 ms: output latency means the tone has not arrived yet.
    rec = rec[int(SR * 0.1):]
    f, snr = dominant_freq(rec)
    ok = abs(f - hz) < 25.0 and snr > 20.0
    return ok, f, snr, rms(rec)


if __name__ == "__main__":
    out_dev = int(sys.argv[1]) if len(sys.argv) > 1 else None
    in_dev = int(sys.argv[2]) if len(sys.argv) > 2 else None
    ok, f, snr, level = selftest(out_dev, in_dev)
    print("PC loopback: %s  detected=%.1f Hz  snr=%.1f  rms=%.4f"
          % ("PASS" if ok else "FAIL", f, snr, level))
    sys.exit(0 if ok else 1)
