# ogemu beta pass — 2026-09-12

**Date:** 2026-09-12.
**Role:** beta tester for a legitimate penetration tester — owned hardware,
written rules of engagement, no third-party rolling-code work, no denial of
radio service. Everything below is display-CPU chrome on the host.
**Scope:** `tools/ogemu` only. No physical board, no `fw flash`, no UF2 write,
no USB CDC opened at any baud. Headless JSONL for the whole pass — the tk GUI
(`--gui`) was **not used**, so nothing here is evidence about the GUI.

## Commands run

All from the repo root on Windows (PowerShell), cwd `C:\opt\wiliOGbsp`.

| # | Command | Exit |
|---|---|---|
| 1 | `python tools/ogemu/ogemu.py --app hostdeck --script tools/ogemu/scripts/smoke.jsonl --dump build-emu/hostdeck.png` | 0 |
| 2 | `python tools/ogemu/ogemu.py --app hostdeck --script tools/ogemu/scripts/world.jsonl --dump build-emu/hostdeck_world.png` | 0 |
| 3 | `python tools/ogemu/ogemu.py --app voltpet --script tools/ogemu/scripts/voltpet.jsonl --dump build-emu/voltpet.png` | 0 |
| 4 | `python tools/ogemu/ogemu.py --app micscope --script tools/ogemu/scripts/micscope.jsonl --dump build-emu/micscope.png` (**before** the dcblock fix) | **1** — `LNK2019 fwog_dcblock_inplace` |
| 5 | same command, after adding `dcblock.c` to `OGEMU_COMMON` | 0 |
| 6 | `ctest --test-dir build-emu --output-on-failure` | 0 — 4/4 passed |
| 7 | `build-emu\ogemu_micscope.exe --tick 200 --expect "rms  4242"` (negative control) | **1** — `expect_text missed` |
| 8 | `ogemu.py --app {template,hostdeck,voltpet,micscope} -- --tick 50` | 0 each — all four targets link after the fix |

Run 7 exists because a green test suite is worthless if the assertion cannot
fail. `expect_text` does exit non-zero on a miss, so ctest has teeth.

## Script results

| Script | Result | What the overlay actually proved |
|---|---|---|
| `scripts/smoke.jsonl` (HostDeck) | pass | Nine repainted lines: title, `page 1 / 2   HID later`, all five colour legends, the selected macro `Play`, and the footer `This PC only. Not an injector.` A green tap logged `MACRO page=0 slot=0 Play -> Consumer Play/Pause (HID not enumerated in v001)` — the macro state machine advances with no HID stack present. |
| `scripts/world.jsonl` (HostDeck) | pass | `world accel mg 0 0 1000`, a shake, `mic_rms 2400`, `rssi -72 dBm hz 433920000`, `ook … edges 8 first 400 us`. **The panel text is byte-identical to run 1.** The script proves the inject bus fired; it proves nothing about any app reading it. See the false-confidence note below. |
| `scripts/voltpet.jsonl` | pass | Activity grid (`>Train`/`Sleep`/`Fight`/`Eat`/`Relics`/`Story`), stat line `hunger 40 energy 80 happy 70`, `radio quiet -- waiting for a wave`, then after `{"cmd":"rf","art":0}` the relic card `Boots of Looping Joy` + `Green keeps it. Gray opens bag.` This is the only app in the tree whose radio chrome ogemu can drive end to end. |
| `scripts/micscope.jsonl` (**new**) | pass | `rms     0  peak     0` at boot, then `rms  2400  peak  2400  3968 Hz` and `rms  9000  peak  9000  3968 Hz` after two `mic_rms` injects, plus `PDM 8 kHz  clap the room` and the footer. First script in which a world-bus sensor value reaches the panel. |

## Findings

### P0 — the world bus could only drive one app's radio protocol (fixed same night)

`ogemu_world_rf_burst()` hardcoded VoltPet's `vp_rf_t`. `ogemu_world_rssi()`
stored a value and printed a `DIAG` line; no pentest-adjacent radio app
could see it. BandScope painted `waiting for main...` until a 128-byte
`BS_MSG_ST` arrived. Same for ISMburst, TwinFox, TireEar, FobReplay,
InertialTrailRF.

**Fix.** Typed commands built from the real `*_proto.h` headers:
`bs_status`, `ib_status`, `tf_status`, `te_status`, `fob_status`,
`trf_status`, plus `rf` still for VoltPet. MSVC `pragma pack` was added
to those headers so `cl.exe` matches the RP2040 wire size. `rssi` remains
DIAG-only on purpose — a script that greps `world rssi` is still checking
the harness.

**Legitimacy.** On an engagement the screen is the instrument of record:
the band label and the peak-frequency readout are how a tester confirms
they are still inside the frequencies the rules of engagement authorize,
and the hit list is what gets transcribed into the report. This is
strictly receive-side display rendering fed by synthetic frames on the
host — no transmitter, no PHY, no new capability.

### P1 — 23 of the 24 display halves need no new stub, and four are wired

`apps/` holds 24 `display/main.c` files. Counting their `hardware/*.h`
includes, **23 of 24 reference nothing beyond `hardware/pio.h`**, which ogemu
already stubs; `apps/smoke` is the sole exception (`clocks.h`, `uart.h`).
MicScope needed no new stub at all — only a missing `.c` in the common list.
Header-eligibility is not the whole cost (`bl` pulls the flash/jump halves and
`lvgl` needs an LVGL target), but for the pentest-adjacent set — BandScope,
ISMburst, FobReplay, TwinFox, TireEar, InertialTrailRF, OpticClick, HeaderKit,
TtyGlass, TalkClip — the cost is one `ogemu_add_app()` line plus a JSONL. Ten
targets are wired today (`hostdeck`, `template`, `voltpet`, `micscope`,
`bandscope`, `ismburst`, `twinfox`, `tireear`, `fobreplay`,
`inertialtrailrf`). Radio chrome for the pentest-adjacent set is P0, now
closed. OpticClick still needs IR stubs; TalkClip/HeaderKit/TtyGlass are
habit plus a script.

**Legitimacy.** A field kit is judged on whether the operator can trust what
the panel says while standing in a plant room with one hand free. Chrome
defects found on a desk cost a minute; the same defect found on site costs a
site visit, and a tester who has lost confidence in the display starts
guessing. Wiring more apps buys reliability for tools already in the catalog
and creates no new RF or host capability whatsoever.

### P1 — `mic_rms` and `rssi` both invite false confidence, in different ways

Two distinct traps, both now measured rather than assumed:

- `rssi` is **store + DIAG only**. `world.jsonl` printing
  `world rssi -72 dBm hz 433920000` looks like an RF test and is not one; the
  HostDeck panel was byte-identical with and without it. Any future script
  that "checks RSSI" by grepping stdout is checking the harness, not the app.
- `mic_rms` does reach the panel, but the harness `pdm_mic_decode()` returns an
  alternating `±rms` square wave. That is energy at Nyquist, so
  `fwog_spectrum_dominant_bin()` pins at bin 127 and MicScope reports
  **3968 Hz for every injected level** unless `mic_tone` is used. `mic_tone`
  generates a sine on the 8 kHz grid; `scripts/micscope_tone.jsonl` expects
  `1000 Hz` and is in ctest. `mic_rms` still exists for RMS/peak.

**Legitimacy.** MicScope and TalkClip are the acoustic half of a site survey —
is this room quiet enough to talk in, is something humming in the ceiling, is
a recording device running. The failure mode of a blank spectrogram is a
tester concluding a room is clean, and it is exactly the conclusion a harness
that "passed" would support. Recording this limit in the README is the minimum
honest outcome; stimulus that exercises the FFT is the real one. Passive
listening on a microphone the tester owns, in a room they are authorized to
be in, needs no further justification.

### P2 — `expect_text` cannot assert absence, so guardrail footers are untestable

Several apps carry a scope reminder in the footer — ISMburst's
`Own gadgets only. Not a jammer.`, FobReplay's `Queue replays unused codes
only.` / `Heard rolling codes stay dead.`, OpticClick's `Your TVs. Not
Sony/RC5.`, ToneBox's `Into a speaker. Not a telephone.`, InertialTrail's
`Not GPS. Pedometer + a map.` Some are painted on one page only. There is no
`expect_no_text` and no way to scope an expectation to the current frame
rather than the 48-line ring, so "the guardrail line is on screen in every
mode, and the transmit page is never reachable without it" cannot be asserted.

**Legitimacy.** Those strings are the on-device statement of the engagement's
limits, and they are also what a client or a colleague reads over the
tester's shoulder. A footer that silently stops being painted when a mode is
refactored weakens the tool's own account of what it does. Making it a
mechanically checked property is a governance win with no capability change.

### P2 — `dcblock.c` was missing from `OGEMU_COMMON` (fixed, run 4 → run 5)

`OGEMU_COMMON` carried `cic.c` and `spectrum.c` but not `dcblock.c`, so any
app calling `fwog_dcblock_inplace()` — the documented companion to the PDM
path — failed at link, not at configure. Fixed this session.

### P2 — Windows: the README's bare-`cmake` recipe does not work outside `vcvars64`

`cmake --build build-emu` from a plain PowerShell prompt dies with
`bsp/common/crc.h(6): fatal error C1083: Cannot open include file:
'stddef.h'`, because MSVC has no environment. `ogemu.py` loads
`vcvars64.bat` (and, later the same night, so do `fw emu` / `fw test`
via `fw.host_env()`). Note the sharp edge this creates for ctest:
**`ctest` will happily run stale `.exe` files** that an earlier
`ogemu.py` invocation built, so a green ctest run does not by itself
prove the current sources compile. Run 8 exists to close that gap.
`fw emu` without `--gui` now rebuilds then ctests, which is the
non-stale path.

## Refused on this pass — policy has since moved

Three things this pass made tempting. None were implemented *in the
harness*. [retired.md](retired.md) now treats authorized professional
pentest use as passing (1); silicon still limits what a UI emulator
should grow.

- **HID enumeration in the harness so HostDeck macros actually type.**
  HostDeck v001 has no HID stack; the helper types via SendInput. Teaching
  ogemu to synthesize keystrokes is still more mechanism than a UI
  harness needs — `MACRO … (HID not enumerated in v001)` tests the state
  machine. That is an emulator-scope choice, not a catalog forbid of
  authorized HID on device.
- **A timing-accurate "blocked while captured" replay command for FobReplay.**
  Occupying a receiver while recording is the jam-leg topology. ogemu has
  no radio; a world-bus jam-leg would only simulate one. Authorized
  vehicle RF tests are in-scope on device (retired.md). This harness still
  does not need that command to smoke QUEUE.
- **A counter/cipher predictor helper for `apps/ismburst/decode.c`.**
  Decoding a fixed-code gadget is this app. Cipher-breaking next-code
  helpers remain a separate crypto question in retired.md — they are not
  unblocked merely because RF occupation passed (1).

None of these is blocked by silicon, which is why they are written down rather
than assumed impossible.

## Workflow notes

**What the automation caught by itself.** The `dcblock.c` omission surfaced as
a link error the moment a fourth app was wired — a configure-clean, link-dirty
failure that no amount of reading `CMakeLists.txt` would have suggested. The
Nyquist stimulus surfaced from comparing a predicted overlay string against
the real one: the `3968 Hz` never moved between a 2400 and a 9000 inject, and
that single stuck field is the whole finding. Both were found without a board.

**False confidence, concretely.** `world.jsonl` passes on HostDeck while
HostDeck ignores every sensor it injects; the `DIAG` lines are the harness
talking to itself. Assert on `TEXT`/overlay content, never on a `world …` log
line. Before this session no script in the tree asserted a sensor value on the
panel.

**Overlay ring.** `LCD_TEXT_LOG_CAP` is 48 lines and dirty-flag chrome is
drawn once, so titles age out — `MicScope` is painted behind
`s_chrome_dirty` and is absent from a late dump, exactly as `VoltPet` is.
Every expectation in `micscope.jsonl` targets a line that is repainted every
frame. This is the single most common way to write an expectation that passes
for the wrong reason, or fails for no reason.

**What ogemu cannot see.** Radios (both CC1101s; `rssi` is a variable, not a
receiver), FatFs and the microSD card, the Bottlenose ESP32-C6 and its UART
protocol, TinyUSB and therefore the CDC console and 1200-baud BOOTSEL, the
FPGA bitstream, the main CPU entirely (`apps/*/main/main.c` is not compiled,
link TX is DIAG-dropped), the WS2812 bar as light, real ST7789 timing, and the
charger — `fwog_power_poll()` runs its real 6 s ship machine but writes no
register, so `hold red 6000` logs `ship would run` and the process lives on.
A green ogemu run says the chrome and the state machine are right. It says
nothing about whether the board works.

## Shipped this session

One change, option A from the brief.

| File | Change |
|---|---|
| `tools/ogemu/CMakeLists.txt` | `ogemu_add_app(micscope)`; `${BSP}/common/dsp/dcblock.c` added to `OGEMU_COMMON`; `add_test(ogemu_micscope_smoke)` wired into the existing `WORKING_DIRECTORY` property |
| `tools/ogemu/scripts/micscope.jsonl` | new — five expectations across two `mic_rms` levels |
| `tools/ogemu/README.md` | MicScope in "Which app is emulated" with the Nyquist/blank-spectrogram limit stated; `ogemu_add_app(micscope)` in the how-to; the `vcvars64` gotcha and the fourth ctest |
| `docs/apps/ogemu-beta.md` | this page |

No app source changed, so no `VERSION` bump: MicScope stays 001 and the
FwOGapp contract is untouched. Nothing was committed.

**Re-proved by:** run 5 (`exit 0`, five `expect_text ok` lines), run 6
(`ctest` 4/4 including `ogemu_micscope_smoke`), run 7 (the negative control
exits 1, so the new test can fail), and run 8 (all four targets, MicScope and
the three incumbents, link with `dcblock.c` added).

## After the pass (same night)

While this page was being written, `fw test` grew an ogemu
configure/build/ctest phase and `fw emu [--gui]` became the no-agent entry
point. Both call `fw.host_env()` (including a PATH/Path merge so `cl.exe`
is actually on PATH after vcvars). The P2 note that bare cmake dies outside
a VS prompt is still true; the documented workaround is now `fw emu` /
`fw test`, not only `ogemu.py`.

This file came from a Cursor Opus High agent running in the background
with no GUI, no board, and a one-improvement cap. The parent chat did not
wait on it. That split worked: MicScope + this page landed without
colliding with `fw emu` except both touching `tools/ogemu/README.md`,
which still reads as one document.

What the first agent loop could not do: it never opened `--gui`, so that
pass is not evidence about the tkinter panel. P0 (typed `bs_status` /
`ib_status` / …) was closed in the parent chat the same night — see
**Second pass** below. HID enumeration, a FobReplay jam-leg, and an
ISMburst rolling-code predictor were refused *for this emulator pass*.
Device-side authorized HID and RF occupation are no longer catalog-
retired; see [retired.md](retired.md).

## Second pass — typed radio frames + mic_tone (same night)

Closed P0 and the MicScope FFT gap. No board, no flash, no CDC. Refusals
unchanged.

| Command | Result |
|---|---|
| `python tools/fw.py emu` | 0 — 11/11 ogemu ctest (HostDeck×2, VoltPet, MicScope rms+tone, BandScope, ISMburst, TwinFox, TireEar, FobReplay, TrailRF) |
| `ogemu.py --app bandscope --listen 9321` then `bandscope.jsonl` on that socket | 0 — same JSONL the GUI Send box uses; `peak -48 dBm  @ 433.92 MHz` and `1  433.92 MHz  -48 dBm` on the overlay |

Wired: `ogemu_add_app` for bandscope, ismburst, twinfox, tireear, fobreplay,
inertialtrailrf (plus `pedometer.c`). `fwog_ioexp_link_set_antennas` is a
HOST_TEST stub so BandScope can change bands without I2C.

GUI chips added for `mic_tone`, `bs_status`, `tf_status`. The tkinter
window itself was not opened this pass; `--listen` is the identical world
bus.

**Caught.** `pump_immediate()` listed world-bus kinds by enum and silently
dropped anything new — first rebuild had 7/11 red with no `world bs_status`
DIAG at all. Adding the new kinds to that switch was the whole fix. The
overlay ring still keeps `waiting for main...` after a successful frame
(BandScope live dump shows both lines); assert the *new* peak string, not
the absence of the old one.

**Missed / still out of scope.** OpticClick (IR PIO, no `*_proto.h`),
TalkClip, HeaderKit, TtyGlass. No `expect_no_text`, so guardrail footers
remain untestable. No HID, no jam-leg, no rolling-code predictor.
