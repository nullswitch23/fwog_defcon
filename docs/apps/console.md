# USB CDC console — talking to the OG from a PC

How to attach a terminal, which CPU you landed on, and which apps actually
put **data** on that wire versus a boot banner. Source of truth:
`tools/fw.py` (`do_console`, `_pick_console_port`, `_pick_cpu_port`,
`_touch_port`), `bsp/common/diag.h`, and `AGENTS.md`.

## Exact commands

Needs pyserial (`python -m pip install pyserial`). From the repo root:

```
python tools/fw.py console
python tools/fw.py console --port COM12
python tools/fw.py console --product "FWOG display "
python tools/fw.py console --product "FWOG main "
python tools/fw.py console --product "FWOG display hostdeck"
python tools/fw.py console --product ""
```

`--print` shows the attach line without opening a port.

There is **no `--cpu` flag on `console`.** That flag belongs to
`fw bootsel` and `fw new-app`. To pick a CPU on the console you pass a
USB product substring (or `--port`). The equivalent of “the display CDC”
is `--product "FWOG display "` (trailing space is load-bearing in
`fw.py`’s CPU prefixes). Some app pages still write `fw console --cpu
display`; that is not a flag this command accepts.

Default `--product` is `FWOG display bl` — the **display bootloader**,
not a running application. Bare `fw console` with both CPUs enumerated
will either attach to the bootloader (only after ~10 s of main silence)
or refuse with “multiple matching ports.” For an app, name it:

| You want | Command |
|---|---|
| Display app CDC | `python tools/fw.py console --product "FWOG display "` |
| Main app CDC | `python tools/fw.py console --product "FWOG main "` |
| One named app | `--product "FWOG display inertialtrail"` (or `hostdeck`, `kithome`, …) |
| Known COM | `--port COM12` (skips the filter; still not 1200 baud) |
| Only one RP2040 present | `--product ""` matches any FreeWili CDC |

`fw console` opens at **115200**. USB CDC ignores UART baud; pyserial
still wants a number. Type lines and they go to the device; Ctrl-C exits.

### Identifying the two CDCs

Both RP2040s enumerate **USB CDC**. After `fwog_usb_ids()` / `fwog_usb_product()`:

| CPU | VID:PID | Product string |
|---|---|---|
| Display (and the bootloader) | `093C:2055` | `FWOG display <name> <ver>` e.g. `FWOG display hostdeck 001` |
| Main | `093C:2054` | `FWOG main <name> <ver>` e.g. `FWOG main bench 001` |

`093C` is Intrepid Control Systems. Older images still on the SDK default
`2E8A:000A` with product `"Pico"` cannot be told apart; `fw.py` falls back
to asking. Identification order is **PID first**, product-string prefix
second (`FWOG display ` / `FWOG main `). On Windows, usbser.sys hides the
product string from pyserial; `fw.py` fills it from SetupAPI.

`fw bootsel --cpu display|main` (or `--port P`) is how you reboot **one**
CPU into UF2 without a button. Put only one CPU in BOOTSEL at a time.

### 1200 baud is BOOTSEL, not a console

Opening that same CDC at **1200 baud** is the Pico SDK reset path
(`PICO_USB_RESET_MAGIC_BAUD_RATE` → `rom_reset_usb_boot_extra()`). Merely
opening is enough; there is no DTR handshake. That is `fw flash` / `fw
bootsel`, and it is on by default in every binary here. **Do not open a
console at 1200.** Host helpers (`tools/hostdeck/`) refuse 1200
explicitly.

### UART0 is never this console

UART0 on both CPUs is the **inter-CPU link**. Stdio is forced off UART
(`PICO_STDIO_UART 0`, `fwog_configure_stdio()`). `DIAG()` is `printf` to
**USB CDC** (`bsp/common/diag.h`). Driver code must not `printf` onto the
link. A build with `-DFWOG_DIAG=0` compiles diagnostics away and drops
pico_stdio_usb — the CDC (and 1200-baud BOOTSEL) go with it.

## What actually prints

`DIAG()` is the only text channel. Almost every app prints a boot line.
The table below is **useful data or an interactive console**, not “alive”
banners. Flash the **main** UF2 so the display image arrives with
metadata; do not UF2-copy a display application.

| App | CDC | Kind | What you see |
|---|---|---|---|
| **HostDeck** | display `093C:2055` | **data** | `MACRO page=… slot=… Label -> Chord …` on each tap. `python tools/hostdeck/hostdeck.py` types those chords on this PC. |
| **KitHome** | display | **data** (HostDeck tile) | Same `MACRO` lines. Helper accepts product `kithome` as well as `hostdeck`. MicScope/BandScope tiles are LCD-only on this CDC. |
| **InertialTrail** | display | **data** | Live CSV while a take runs: `# inertialtrail …` then `kind,ms,steps,…` / `step,…` / `mark,…` / `xy,…`. Not 1200 baud. |
| **InertialTrailRF** | display | **data** | CSV `# trailrf v008  kind,step,rssi0,rssi1,freq0_hz,freq1_hz` while walking. Main also writes `/trailrf/*.csv` on FatFs (status on main CDC). |
| **TalkClip** | display + main | markers, not PCM | Display: `# talkclip start/stop …`. Main: `[talkclip] start /talkclip/…` and close byte counts. PCM crosses the **link** to FatFs, not USB. Pull clips with DiskGlass, not a serial dump. |
| **OpticClick** | display | **data** (IR log) | `tx 0x…`, `rx slot … 0x…`, `# blast …`, `# capture armed`. NEC codes as hex. |
| **ISMburst** | main | **data** (decode) | `[ismburst] … hex=…` burst decode; store/replay lines. Display is LCD. |
| **FobReplay** | main | event log | Capture / replay / queue lines (`stored slot`, `replayed slot`). Not a CSV stream. |
| **PingHalo** | main | **data** (C6 mirror) | `[pinghalo] BN HELLO …`, `BN ADV rssi=… mac=…`, `BN BLE on/off`. BLE advertisements, via Bottlenose UART then this CDC. |
| **LanFerry** | main | **data** (C6 mirror) | `[lanferry] BN AP on ssid=… pass=…`, `BN STA clients=… files=… used=… last=…`, `BN PIPE pct=…`, `BN AP wipe`. The file drop itself is HTTP on the SoftAP, not CDC. |
| **BattleBridge** | main | **data** (C6 mirror) | `[battlebridge] BN GAME on ssid=FWOG-arena pass=…`, `BN GAME sta players=…`, `BN GAME off`. The arena itself is HTTP/WebSocket on the SoftAP, not CDC. |
| **BleDeck** | main | **data** (C6 mirror) | `[bledeck] BN HID …` (`conn` / `pair` / `enc` / `lock` / `forget` / `bat` / `disc`). |
| **bench_display** | display | **console** | Type `help`. Sensor/charger/RTC/mic/IR/LCD commands; `OK` / `ERR` replies. |
| **bench_main** | main | **console** | Type `help`. CC1101, FPGA, FatFs, header I/O. |
| **smoke_*** | each CPU | bring-up | Clock/link report; any USB char (or a button on display) escapes to BOOTSEL. |
| **bl_display** | display, after 10 s of main silence | **console** | `help` / `info` / `bootsel` / `run` / `ship` / `erase`. Default `fw console` target. |
| HeaderKit, TwinFox, TireEar, BandScope, VoltPet, ToneBox, MicScope, TtyGlass, ChirpMail, DiskGlass, lcd, template, ogevegas | matching CPU | debug | Boot `DIAG()` only (I2C count, radio ok, relic RSSI, …). Useful numbers live on the LCD. |

HostDeck’s helper **refuses** main (`093C:2054`). PingHalo / LanFerry /
BleDeck / BattleBridge useful lines are on **main**, because UART1 to Bottlenose is a
main-CPU peripheral.

## Related

- [HostDeck](hostdeck.md) — MACRO helper
- [AirMaraud](airmaraud.md) — C6 2.4 GHz lab (scan; TX armed/unproven)
- [retired.md](retired.md) — keep-test
