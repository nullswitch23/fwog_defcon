# BSP addendum — what the product slate taught the OG

Read this **after** [AGENTS.md](../AGENTS.md). That file is still the
contract: power-off, watchdog, UART0, 200 MHz `clk_peri`, never grow the
bootloader, never UF2-flash a display application, frozen FPGA bitstream.
This page is what we learned **building the 2026 product slate** on top of
that contract. It is not a second contract and it does not relax any of
the seven FwOGapp rules.

Catalog: [apps/README.md](apps/README.md). Combos:
[apps/combo-images.md](apps/combo-images.md). Public remote cut:
[README.md](../README.md) (Public tree).

Mux landings are shipped and bench-signed (2026-09-27): **KitHome 007**,
**EmsDesk 004**, **OrcaLobby 004**, **FunDesk 004**, **PinDesk 003**.
**IsmDesk** was the working name for EmsDesk — do not track it. TireEar
004 and ToneBox 004 are on that RF/sound kit. Remaining village item:
**fsbak RAW → WAV** (operator is testing). Public remote still needs the
secrets / history pass below.

## Patterns that belong in every new app

**T9 is the text path.** Gray hold opens it (PingHalo, VoltPet, ChirpMail
slots, TalkClip names, …). BandScope is the documented exception that
kept its own chrome. Do not invent a sixth letter-entry model.

**Arrow pad** for menus that are not T9: green select, yellow left, blue
right, gray up, red down — same as AGENTS.md’s FreeWili 2 porting note.
Red tap backs out of a screen; 6 s red is still ship. Distinguish tap vs
hold the way TwinFox and VoltPet do (ship swallows `pressed` on arm).

**FatFs is main’s last 8 MB.** Files coexist (`voltpet.bin`,
`/chirpmail/CANNED.TXT`, `/talkclip`, `/ismburst`, `/fobreplay`,
`mscope.cal`). Writers take turns. Display never mounts that volume.
GlassBak dumps it on **main** CDC. DiskGlass browses it. Do not put app
code in that half of flash.

**Host helpers talk CDC at 115200, never 1200 baud.** 1200 is BOOTSEL.
ChirpMail, HostDeck, RigGlass, GlassBak/fsbak identify by PID then
`FWOG main ` / `FWOG display ` product strings. Windows hides
`product` on raw pyserial; go through `fw._cpu_ports()`.
`fw._pick_cpu_port()` returns a **device string**, not a `CpuPort` —
resolve it before `.product`.

**ogemu is LCD chrome, not radios.** `fw emu` + jsonl scripts catch
text and buttons. Relic/RF stubs exist for VoltPet-shaped frames. They
are not evidence the CC1101, FatFs, or C6 work. Flash `*_main` and look
at the board (or the bench cam) for that.

**One radio personality live.** Two CC1101s. TwinFox and InertialTrailRF
each take **both**. KitHome’s `KIT_MSG_SEL` idles the radio unless the
BandScope tile is up. Do not run TwinFox + ChirpMail + VoltPet receive
at once. A combo landing is a mux, not a zoo.

**Bottlenose is one `fwog_c6.bin`.** Extra OG tiles do not add a second
C6 binary. UART1 is the C6 link **or** TtyGlass, not both. One SoftAP
or one BLE role. BattleBridge’s JPEG is why the C6 app partition has
~6 % free — keep the arena its own UF2.

**Ship saves, it does not wait.** `fwog_power_poll()` `armed` is the
window to PUSH FatFs (VoltPet). RAM dies on ship. A gray hold or USB
wakes hardware with the CPUs reset.

**Do not touch UART0 from splash until `fwog_link_uart_init()`.** The
PL011 is still in RESET and `uart_is_readable` stalls the bus (ChirpMail
004 hung on the box art).

**Link settle (ChirpMail 005 / PingHalo 023).** Splash with the link
**down**. After `fwog_link_uart_init()`, run the UI loop (paint +
`fwog_power_poll`) and **do not** send PULL / LIB_GET / SCAN for **3 s**.
`s_get_ms` starting at 0 is a trap: `now - 0 >= 2000` fires on the first
iteration after a 3 s splash. A PULL while main is still in its
post-`display_update_run` sleep fills both FIFOs, `uart_write_blocking`
hangs on CTS, main’s 8.3 s watchdog fires, `GUI_NRESET` restarts the
display, splash again. Same class as uart-before-splash.

**Landing chrome contract.** KitHome (and any later combo) leaves a tile
on yellow/green/gray hold, not on red. Red 6 s ships. Do not paint while
`p.armed`, and latch GOODBYE so USB-powered SYS cannot redraw the last
tile on the splash.

**VERSION is one git commit.** Both CPU halves, splash bind, catalog
row, `docs/apps/<name>.md`. Do not land two version numbers together.

## Invariants the slate re-paid

These were already in AGENTS.md. Apps still broke them, so they are
restated as *product* failure modes:

| If you skip this | What you see |
|---|---|
| `board_watchdog_kick()` every main loop | Board resets ~8.3 s; display held in reset; no USB |
| `fwog_power_poll()` every display loop | Red 6 s does not ship |
| `fwog_configure_stdio()` (via `fwog_*_app`) | UART0 stdio corrupts the link |
| Flash `*_main` only | Display UF2 at `0x10021000` with stale metadata; dark panel, no CDC |
| UART0 MMIO before `fwog_link_uart_init()` | Display hangs on splash; never paints the app |
| PULL/LIB_GET in the first 3 s after uart_init (`s_get_ms == 0`) | Main watchdog; display splash loop |
| Two RPI-RP2 volumes | Drive letters swap; wrong CPU image can fight the PDM pin |
| Hardcoded PIO/baud | Silent 3 Mbaud / 24 MHz LCD if `clk_peri` is not 200 MHz |

## DEF CON village — hanging work

Show-floor packs are shipped: KitHome (booth), EmsDesk (RF), OrcaLobby
(Bottlenose), FunDesk (sound/toy), PinDesk (breakout). Stand-alones stay.
Unbuilt Flipper-shaped names are **retired** (too similar to other apps
or not native) — see [apps/README.md](apps/README.md) Retired / blocked.
**IsmDesk** is EmsDesk; do not track it.

| Item | Status |
|---|---|
| **fsbak RAW → WAV** | **Open** — TalkClip `/talkclip/*.RAW` dumps should become playable WAV on pull. Operator testing. |
| **MeshIsle, StickPeek, KissIsle** | Blocked or retired (silicon / policy). Not in the shipped catalog table. |

Assessment-class remainders (OccupyISM, HID-toward-a-target, authorized
fob-jam) stay off the consumer catalog. Silicon can do them; the village
table should not.

Do not invent concurrent-radio mega-apps, a second FPGA bitstream, LVGL
as product chrome, or UART0 stdio to “help debugging.”

## Publish hold (Opus 5.5, 2026-09-26)

No API keys or private keys in the public paths. Still **do not push
this clone as-is**.

Already cleaned in-tree: bench LAN IP out of `AGENTS.md` / `fw.py` help;
USB serial fixtures `S1`/`S2` in `tools/tests/test_fw.py`; personal
SDK path in `ws2812_driver.h`; PingHalo first-name credit; HostDeck
example payloads; missing README PNG.

The **private henry clone tracks** the Cyberpunk ship-clip source
(`i-got-a-real-bad-feeling-about-this-cyberpunk-2077.mp3`) and
`DEF-CON-logo.png`. Converted splash PCM is
`bsp/display_cpu/lcd/art/bad_feeling_8k.wav`. **Public git omits** the
MP3; product UF2s still carry the DEF CON seal and the ship WAV.

Still required for a public remote:

1. **Fresh-history export** of the allowlisted tree. Existing git
   authors include personal emails; `origin` and `henry` both carry
   that history.
2. **Omit** `tools/wyze_cam/`, `.cursor/`, `AGENTS.local.md`, `T-Echo/`,
   root Cyberpunk mp3, `*.exe`. Those may still be **tracked** on henry —
   gitignore does not untrack them.
3. **OG Vegas / WiLiDoro / Orca Field Notes** stay in
   [freewili/wiliOGbsp](https://github.com/freewili/wiliOGbsp); this DEF CON
   cut does not republish them.
4. `fw test` on a public checkout: skip `tools/tests/test_wyze_cam.py`
   if `wyze_cam` is not published.
5. `freewili1-docs/` **is** shipped on the DEF CON cut (it is no longer
   in the upstream public repo), so [flipper.md](apps/flipper.md) links
   into it are acceptable there.

## Sources (draft for public remote — review)

Not a substitute for [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).
That file is the license map for **vendored** code. This list is every
upstream that *contributed* to the product, including prerequisites and
art, so a public cut can cite them.

### Hardware and product identity

- **Intrepid Control Systems / FreeWili** — OG hardware, USB VID/PID
  `093C:2054`/`2055`, iCE40 gateware `fpga_default_v5.bin`, catalog
  identity. Dual license in `LICENSE` / `NOTICE`.
- **Upstream BSP** — [freewili/wiliOGbsp](https://github.com/freewili/wiliOGbsp).
  This DEF CON cut is a village snapshot plus product apps; OG Vegas,
  WiLiDoro, and Orca Field Notes remain there.
- **DEF CON** — conference and the smiley-and-crossbones mark on the boot
  splash (trademark of DEF CON Communications, Inc.).
- **FreeWili 1 `rmpLib` (C++)** — design reference. This BSP is a clean
  C re-implementation, not a fork. No rmpLib sources are shipped.
- **FreeWili 2 `wilibsp`** — CIC decimator and DC blocker (`bsp/common/dsp/`).
- **FreeWili OG App Explorer** — companion host app, separate repo
  (`github.com/freewili/fwOGAppExplorer`); not vendored here.

### Silicon and SDKs (build-time, not vendored)

- **Raspberry Pi Pico SDK 2.3.0** (BSD-3-Clause) — RP2040 HAL, PIO,
  clocks, `pico_stdio_usb`, 1200-baud BOOTSEL.
- **TinyUSB** (MIT, via Pico SDK) — USB device / CDC.
- **Espressif ESP-IDF** (≥ 5.3, Apache-2.0) — Bottlenose ESP32-C6
  (`firmware/bottlenose/`). **esptool.py** (GPL-2.0+) for merge/flash.
- Datasheets used by drivers: TI **CC1101** (SWRS061), ST **LIS3DH**,
  **ST7789**, Maxim **MAX98357A**, TI **BQ25896**, Microchip **MCP7940**,
  NXP **PCAL6416**, Lattice **iCE40**. Implementation is project code.

### Vendored / adapted libraries (see THIRD-PARTY-NOTICES.md)

- **FatFs R0.15** (ChaN, BSD-1-Clause) — `bsp/main_cpu/fs/ff/`.
- **wasm3 v0.5.0** (MIT) — `apps/diskglass/third_party/wasm3/`.
- **LVGL 9.2.2** (MIT) — not vendored; port + `lv_conf.h` only.
- **Project Nayuki qrcodegen** (MIT) — `apps/lanferry/qrcodegen.{c,h}`.
  Listed in THIRD-PARTY-NOTICES.md.
- **ELECHOUSE CC1101 Arduino** — algorithm reference for the CC1101
  port; not copied into the tree (`cc1101.h` provenance).
- **OpenPDMFilter** — evaluated and **declined**; no code in-tree.

### Host tools (prerequisites)

- Python 3, CMake, Ninja, arm-none-eabi GCC, **pyserial**, **Pillow**
  (splash asset gen), tkinter (ogemu / ChirpMail / fsbak GUIs).
- **freewili-finder** — USB identification semantics (external tool).

### Art and audio (clearance before public)

- **DEF CON** smiley-and-crossbones mark — splash corner
  (`DEF-CON-logo.png` → `art/defcon_seal.png`). Trademark of DEF CON.
  Baked into product UF2s on this cut. Fallback `skully_seal.png` is
  in-repo generated.
- **Cyberpunk 2077** line used as 6 s ship clip — source MP3 on henry
  only; PCM `art/bad_feeling_8k.wav` is in the BSP and in the UF2s.
  CD Projekt / rights holders. Public git omits the MP3.
- NES-box **layout** splash — homage to NES box framing as popularized by
  **James Rolfe / Angry Video Game Nerd**; illustration original; no
  Nintendo marks.
- **ogvegas** `VivaLosVegas16khz.wav` — not in this cut (upstream
  [wiliOGbsp](https://github.com/freewili/wiliOGbsp)).
- HeaderKit photos credit **FreeWili_WebDocs**.

### Protocol / data (not Flipper firmware)

- **NEC IR**; CCITT No.5 / Bell MF / ACTS / DTMF as ToneBox museum data.
- **rdoetjes/tuts** — C5 named pairs and dial-file format (`apps/tonebox/`).
- **litui/dtmf_dolphin** — ACTS / coin pulse tables (data).
- **Dangerous Prototypes Bus Pirate** — Retia is inspired by the CLI,
  no Bus Pirate firmware vendored.
- **Microsoft UF2** plus this tree’s `fwog_uf2_info_t` record.
- Flipper comparison — [123fzero/flipper-zero-awesome](https://github.com/123fzero/flipper-zero-awesome)
  was the list consulted; FAPs do not run here (`docs/apps/flipper.md`).

### Private henry only (not the public remote)

- `tools/wyze_cam/`, docker-wyze-bridge / go2rtc, `.cursor/`,
  `AGENTS.local.md`, `T-Echo/` and other download trees, root Cyberpunk
  MP3, `tools/fsbak-out/`, git history with personal emails.

## How this cut was built (since 2026-08-28)

Cursor was used for the product-slate work from **28 Aug 2026**. Git on
this tree from that date: **40 commits**, about **138 815 lines added** and
**16 573 deleted**. That total includes third-party and generated bytes
(wasm3, catalog JSON, ogvegas assets, PNGs) — it is not “138k lines of
model-authored C.” FatFs, wasm3, Pico SDK, ESP-IDF, the FPGA bitstream,
and much of `bsp/` are third-party or predate that window.

**Cursor usage** (operator report): **889.6 million tokens**, predominantly
**Grok 4.6 High**. That figure is from Cursor’s usage UI, not reconstructible
from git. Almost all `apps/` product firmware, mux landings, ogemu scripts,
and catalog rows after 28 Aug 2026 were produced in those agent sessions.

