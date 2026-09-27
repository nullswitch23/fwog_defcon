# ogemu — FreeWili OG display-CPU UI emulator (v001)

Host-side **320×240 ST7789** harness so Cursor agents can iterate a display
app without the dual-RP2040 board. It is **not** a cycle-accurate RP2040,
not QEMU, and not a radio stack.

v001 emulates the **display CPU UI**: framebuffer, five buttons, virtual
time, PNG + text overlay dump. Radios, FatFs, Bottlenose C6, the inter-CPU
link, TinyUSB, 1200-baud BOOTSEL, and the FPGA bitstream are **out of
scope**.

## Without an agent

The GUI is stdlib tkinter — nothing to compile. cmake builds the C harness
into `build-emu/`. That is part of the normal host pipeline, not `fw build`
(the Pico SDK / Arm preset cannot compile it):

```
python tools/fw.py test                 # tests/ CTest + ogemu CTest + tools/tests/
python tools/fw.py emu --gui            # build then open HostDeck's panel
python tools/fw.py emu voltpet --gui
```

`--print` shows the cmake/ctest lines instead of running them. `fw emu`
without `--gui` configures, builds every `ogemu_*` target, and runs the
ogemu smokes. Same host compiler as `fw test` (MSVC `cl` via vcvars, or
MinGW `C:\msys64\mingw64\bin\gcc.exe` when that exists).

## One agent iteration

From the repo root, on Windows or anywhere with a host C compiler:

```
python tools/ogemu/ogemu.py --app hostdeck --script tools/ogemu/scripts/smoke.jsonl --dump build-emu/hostdeck.png
```

`ogemu.py` uses the same `fw.host_env()` / toolchain pins as `fw test`.

That configures `build-emu/` (no Pico SDK), builds `ogemu_hostdeck`, runs
the **HostDeck** display `main.c` against RAM ST7789 + injected buttons, and
writes:

| File | What an agent reads |
|---|---|
| `build-emu/hostdeck.png` | 320×240 RGB panel |
| `build-emu/hostdeck.txt` | last `lcd_text_draw_padded` strings (grep `HostDeck`, no OCR) |
| stdout | `DIAG()` lines, `TEXT x=… "…"` overlay, `MACRO …` chords |

Then edit `apps/<name>/display/main.c`, re-run the same command, read the
new PNG/text. Do **not** `fw flash` a display UF2.

`--no-build` skips cmake if `build-emu/ogemu_hostdeck.exe` already exists.

## Buttons

Hardware colours, same bits as `fwog_buttons_poll()`.

| Colour | Script / argv | Keyboard (v002 window; documented now) |
|---|---|---|
| Green | `press green` / `--press green` | `G` or Enter |
| Yellow | `press yellow` | `Y` or Left |
| Blue | `press blue` | `B` or Right |
| Gray | `press gray` (also `grey`, `h`) | `H` or Up |
| Red | `press red` | `R` or Down |

A tap that the app sees as `released` is press + a few ms + release
(debounce is 5 ms):

```
press green
tick 30
release green
tick 30
```

`hold red 6000` presses red and waits debounce+6000 ms so
`fwog_power_poll()` can fire ship. v001 **logs** `ogemu: ship would run`
and keeps the process up instead of killing it.

## Script format

JSONL (`tools/ogemu/scripts/smoke.jsonl`):

```json
{"cmd": "tick", "ms": 200}
{"cmd": "expect_text", "contains": "HostDeck"}
{"cmd": "press", "btn": "green"}
{"cmd": "tick", "ms": 30}
{"cmd": "release", "btn": "green"}
{"cmd": "hold", "btn": "red", "ms": 6000}
{"cmd": "dump"}
{"cmd": "quit"}
```

Same commands as plain lines: `tick 200`, `press green`, `expect_text HostDeck`.

## World bus (synthetic sensors / RF)

The LCD is the device. Accel, mic, RSSI, and OOK edges are a **separate**
inject bus — the same JSONL as buttons, not a dual-CPU QEMU.

```json
{"cmd": "accel", "x": 0, "y": 0, "z": 1000}
{"cmd": "shake", "ms": 200}
{"cmd": "mic_rms", "rms": 2400}
{"cmd": "mic_tone", "hz": 1000, "rms": 9000}
{"cmd": "rssi", "dbm": -72, "hz": 433920000}
{"cmd": "bs_status", "band": 1, "peak_dbm": -48, "peak_hz": 433920000}
{"cmd": "ib_status", "state": 3, "rssi": -62, "guess": 9, "label": "owned fob"}
{"cmd": "tf_status", "rssi0": -80, "rssi1": -42}
{"cmd": "te_status", "bursts": 3, "rssi": -55, "in_burst": 1}
{"cmd": "fob_status", "state": 4, "unused": 2, "rssi": -70}
{"cmd": "trf_status", "rssi0": -70, "hz0": 315000000, "rssi1": -88, "hz1": 433920000}
{"cmd": "rf", "art": 0}
{"cmd": "ook", "file": "tools/ogemu/scripts/fan.ook"}
```

Plain lines: `accel 0 0 1000`, `shake 200`, `mic_rms 2400`,
`rssi -72 433920000`, `ook tools/ogemu/scripts/fan.ook`.

| Command | What the display app sees |
|---|---|
| `accel` / `shake` | `lis3dh_process()` millig (HOST_TEST shell) |
| `mic` / `mic_rms` | next `pdm_mic_take_raw_buffer` + a **Nyquist square wave** at that RMS |
| `mic_tone` | same, but a sine at `hz` on the 8 kHz grid (MicScope FFT) |
| `rssi` | stored + `DIAG` only — no panel unless a typed status frame follows |
| `bs_status` / `ib_status` / `tf_status` / `te_status` / `fob_status` / `trf_status` / `ph_status` / `bd_status` / `lf_status` / `am_status` | packed `*_MSG_ST` on the link RX stub, from the real `*_proto.h` |
| `rf` / `relic` | VoltPet `VP_MSG_RF` |
| `ook` | µs-per-line edge file, stored + `DIAG` (not a radio PHY) |
| `link` | raw payload hex, last resort |

Typed status frames are **receive-side chrome**. They do not transmit, do not jam, and do not predict rolling codes. `fob_status` can show an unused-code queue; it cannot model a receiver that missed a burst because something jammed it.

Recorded script:

```
python tools/ogemu/ogemu.py --app hostdeck --script tools/ogemu/scripts/world.jsonl --dump build-emu/hostdeck_world.png
```

Stdout should include `world accel`, `mic_rms`, `rssi`, `ook … edges`.

## Live inject (`--stdin` / `--listen`)

Same JSONL, streamed while the app loop is running. Empty queue does **not**
auto-quit; the process waits (1 ms wall sleep per idle `sleep_ms`).

Stdin (EOF sends `quit`):

```
python tools/ogemu/ogemu.py --app hostdeck --stdin --dump build-emu/hostdeck.png < tools/ogemu/scripts/world.jsonl
```

TCP on loopback until a `quit` command:

```
python tools/ogemu/ogemu.py --app hostdeck --listen 9320 --dump build-emu/hostdeck.png
python tools/ogemu/bench.py --port 9320 --script tools/ogemu/scripts/world.jsonl
python tools/ogemu/bench.py --port 9320 --rssi -72 --shake --mic 2400 --quit
```

`--frame FILE.ppm` writes a live P6 PPM of the panel (~20 Hz) so the tk GUI
can blit it. `gui.py` passes that flag for you.

`bench.py` without `--port` writes JSONL to stdout (pipe into `--stdin`).
Never open an RP2040 CDC at 1200 baud from this tool.

## Argv instead of a file

```
python tools/ogemu/ogemu.py --app hostdeck --dump build-emu/hostdeck.png -- --tick 200 --press green --tick 30 --release green --tick 30 --expect HostDeck
```

`tick` is **virtual** milliseconds. `sleep_ms()` in the app does not wait
on the wall clock; it advances the harness clock and consumes script time.

## GUI (clickable panel)

The LCD and the five colour buttons are the device. JSONL on the right is
the world bus (accel, mic, RSSI, OOK) — same lines as `bench.py`.

```
python tools/fw.py emu --gui
python tools/fw.py emu voltpet --gui
```

`ogemu.py --gui` still works and will cmake if the binary is missing. `fw emu --gui` builds first (same as `fw test`) then launches with `--no-build`.

Mouse-down / mouse-up on a colour is `press` / `release` (holds work).
`G` `Y` `B` `H` `R` and the arrow keys do the same, unless the JSONL box
has focus. Type one JSON object per line and **Send** or Ctrl+Enter.
Preset chips send `shake`, `mic_rms`, `rssi`, etc.

Headless remains the default. The GUI is tkinter (stdlib). Closing the
window sends `quit`. Never open an RP2040 CDC from this window.

## Which app is emulated

**HostDeck** (`apps/hostdeck/display/main.c`) is the original chrome target.

**VoltPet** (`apps/voltpet/display/main.c` plus `vp_art.c`) is wired for the
GUI. Shake and `mic_rms` feed the HOST_TEST accel/mic shells. Relics need a
stub-main RF frame: `{"cmd": "rf", "art": 0, "dbm": -72}` (GUI chip **rf relic**).
FatFs save and a real CC1101 are still absent.

**MicScope** (`apps/micscope/display/main.c`) is the one wired app that
*consumes* a world-bus sensor: `mic_rms` reaches its `rms`/`peak` readout, so
`scripts/micscope.jsonl` asserts panel text rather than a DIAG line. `mic_rms`
is a Nyquist square wave (dominant tone pins at 3968 Hz, spectrogram blank).
`mic_tone` is a real sine — `scripts/micscope_tone.jsonl` expects `1000 Hz`.
Gray hold (≥700 ms) takes a quiet cal and the bars become a dB SPL
approximation (quiet = 35 dB). `scripts/micscope_shots.jsonl` writes idle /
cal / tone / freeze into `docs/apps/micscope/`. Hardware persists
`mscope.cal` on main FatFs; ogemu is RAM-only.

Radio chrome (BandScope, ISMburst, TwinFox, TireEar, FobReplay, TrailRF) is
driven by the typed `*_status` commands above, not by `rssi`. VoltPet relics
stay on `rf`.

**ISMburst** (`apps/ismburst/display/main.c`) is capture-then-decode chrome.
`ib_status` injects state/RSSI/guess/hex. The smoke is
`scripts/ismburst.jsonl`; `scripts/ismburst_shots.jsonl` writes idle / armed /
capture / 2-FSK park panels into `docs/apps/ismburst/`.

`template` is also wired (`ogemu_template`) so the empty-loop scaffold
builds; it has no LCD chrome.

**KitHome** (`apps/kithome/display/main.c`) is the landing image: HostDeck,
MicScope, and BandScope behind one cursor. `scripts/kithome.jsonl` opens
HostDeck, yellow-holds home, then opens MicScope.

**AirMaraud** (`apps/airmaraud/display/main.c`) is receive-side WIFIPROOF
chrome. `am_status` queues an `am_status_t` on the link stub (`hello`,
`wp_on`, `sweep`, `flash`, canned APs with SSIDs). It does not TX and
does not talk to a C6. `scripts/airmaraud.jsonl` is the smoke;
`scripts/airmaraud_shots.jsonl` dumps the scan / freeze / sweep / flash
panels into `docs/apps/airmaraud/`.

**BleDeck** (`apps/bledeck/display/main.c`) is Bottlenose HID chrome.
`bd_status` injects hello/adv/conn/lock/flash. `scripts/bledeck.jsonl` is
the smoke; `scripts/bledeck_shots.jsonl` dumps the four HID pages and C6
flash panels into `docs/apps/bledeck/`.

**LanFerry** (`apps/lanferry/display/main.c`) is Bottlenose SoftAP chrome.
`lf_status` injects hello/ap/clients/nfiles/flash plus canned SSID, password, and
last file name. `scripts/lanferry.jsonl` is the smoke; `scripts/lanferry_shots.jsonl`
dumps wait / AP / mailbox / pipe / WIFI QR / C6 flash panels into `docs/apps/lanferry/`.

**BattleBridge** (`apps/battlebridge/display/main.c`) is Bottlenose arena
chrome. `bb_status` injects hello/game_on/players/phase/pass/flash. The smoke
is `scripts/battlebridge.jsonl`; `scripts/battlebridge_shots.jsonl` dumps wait /
lobby / active / play / WIFI QR / C6 flash panels into `docs/apps/battlebridge/`.

**HeaderKit** (`apps/headerkit/display/main.c`) is the safe breakout reference.
`hk_status` injects I2C addresses plus I/O/Hi-Z status. The smoke visits the
scan, signal pages, and Gray-button physical header map;
`scripts/headerkit_shots.jsonl` writes the README captures into
`docs/apps/headerkit/`.

**QwiicBench** (`apps/qwiicbench/display/main.c`) is the I2C sensor bench.
`qg_status` injects `io_ok`, a channel `name`/`val`, and a sparkline `plot`.
Unknown `0xNN` rows take a Gray-hold T9 nickname (RAM table, `qg_nick`).
The smoke is `scripts/qwiicbench.jsonl`; `scripts/qwiicbench_shots.jsonl`
writes empty / BME280 / present captures into `docs/apps/qwiicbench/`.
Real sensors are on the OG 3.3 V header, not ogemu.

**RigGlass** (`apps/rigglass/display/main.c`) is the host-load panel.
`rg_status` injects `host` plus `cpu`/`ram`/`net`/`gpu`/`tmp` triples
(`now,2m,10m`). The smoke is `scripts/rigglass.jsonl`. Live numbers come
from `python tools/rigglass/rigglass.py` on main CDC, not ogemu.

**InertialTrail** (`apps/inertialtrail/display/main.c`) is the pedometer
and origin-fixed stride map. `accel` / `shake` feed the LIS3DH shell. The
smoke starts a hold-still cal; `scripts/inertialtrail_shots.jsonl` writes
idle / pedo / map captures into `docs/apps/inertialtrail/`.

**InertialTrailRF** (`apps/inertialtrailrf/display/main.c`) is the same
step clock plus two parked 15 s RSSI strips (Gray top / Blue bottom).
`scripts/inertialtrailrf.jsonl` is the smoke;
`scripts/inertialtrailrf_shots.jsonl` writes idle / walk captures into
`docs/apps/inertialtrailrf/`.

## What is stubbed

| Surface | v001 behaviour |
|---|---|
| ST7789 `fill_rect` / `blit` / `clear` | RAM RGB565, same clip as hardware |
| `lcd_text_draw_padded` | real 6×8 font into that RAM + text log |
| Five buttons | `fwog_buttons_inject`; no GPIO |
| `fwog_power_poll` | real 6 s ship machine; **does not** write the charger |
| `fwog_splash_boot` | no-op (skips the 3 s box-art dwell) |
| WS2812 | colour buffer only; `process()` is a no-op |
| `board_init` | policy symbol + button init; no I2C, no clocks |
| Main CPU, CC1101, FatFs, C6 | not present |
| Link UART | TX DIAG-dropped; RX is a byte queue (`rf` / `link` JSONL) |
| Accel / PDM | HOST_TEST shells fed by the world bus (`accel`, `mic_rms`, `mic_tone`) |
| TinyUSB / 1200-baud BOOTSEL / FPGA | not present; never UF2-flash from here |

## How to add an app

In `tools/ogemu/CMakeLists.txt`:

```cmake
ogemu_add_app(hostdeck)
ogemu_add_app(voltpet ${REPO}/apps/voltpet/vp_art.c)
ogemu_add_app(micscope)
ogemu_add_app(bandscope)
ogemu_add_app(kithome)
ogemu_add_app(diskglass ${REPO}/apps/diskglass/dg_file.c)
ogemu_add_app(airmaraud)
```

Rules:

- Compiles `apps/<name>/display/main.c` as `fwog_app_main` with `HOST_TEST=1`.
- No Pico SDK. Host compiler is whatever `fw test` uses (MSVC or MinGW).
- The app must call `sleep_ms()` in its loop (every display template does);
  that is how the harness advances time and exits.
- Extra `#include "hardware/….h"` beyond `pio.h` / `pico/stdlib.h` needs a
  stub in `tools/ogemu/include/`. Keep those stubs tiny.
- Main-CPU `apps/<name>/main/main.c` is not compiled. Link TX is DIAG-dropped.
  `rf` / `link` JSONL queues a framed payload on display RX (VoltPet relics).

Then:

```
python tools/ogemu/ogemu.py --app <name> --dump build-emu/<name>.png
```

## CMake without the wrapper

```
cmake -S tools/ogemu -B build-emu
cmake --build build-emu --target ogemu_hostdeck
./build-emu/ogemu_hostdeck --script tools/ogemu/scripts/smoke.jsonl --dump build-emu/hostdeck.png
```

`ctest --test-dir build-emu` (or `fw emu` / `fw test`) runs HostDeck,
VoltPet, MicScope (rms + tone), BandScope, ISMburst, TwinFox, TireEar,
FobReplay, InertialTrail, InertialTrailRF, PingHalo, BleDeck, LanFerry, BattleBridge,
HeaderKit, AirMaraud, and DiskGlass smokes.

On Windows those two bare `cmake` lines only work from a shell that has
already run `vcvars64.bat`. Outside one, MSVC cannot find `stddef.h` and the
build dies in `bsp/common/crc.h`. `fw emu`, `fw test`, and `ogemu.py` all
load that environment (`fw.host_env()`), which is why they are the entry
points — not a raw `cmake --build`. `ctest` will still run stale `.exe`
files if you skip the build step.

## Later (not this drop)

- Dual-CPU link loopback (save/load still needs a stub main, not just RF inject)
- LVGL apps
- Real `fwog_splash_boot` art dwell
- FatFs / Bottlenose C6
