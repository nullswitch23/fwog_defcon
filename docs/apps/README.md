# Apps

Catalog of firmware this repo builds for the FreeWili OG. Names here are
the product names App Explorer shows. Each app has its own page.

**Native** means the OG already has the silicon; **add-on** needs a module
on the Orca / breakout (Bottlenose ESP32-C6 is the Wi-Fi + BLE one).
This table is **shipped firmware** only (`apps/<name>/` with a `VERSION`).
Retired and blocked names live in [Retired / blocked](#retired--blocked)
below, not here.

`Ver` is the three-digit FwOGapp `VERSION`. `Last` is the day firmware or
the page last changed, not a promise that it was run on a board that day.
Rows are **A–Z by product name**.

| Name | Category | Ver | Last | One-liner | Fit | Page |
|---|---|---|---|---|---|---|
| **AirMaraud** | Bottlenose | 010 | 2026-09-21 | Bottlenose 2.4 GHz lab: scan APs, arm to TX one deauth/disassoc | add-on (Bottlenose) | [airmaraud.md](airmaraud.md) |
| **BandScope** | Sub-GHz | 004 | 2026-09-21 | Sub-GHz RSSI sweep; freeze+scroll peaks; owned marks; history strip | native | [bandscope.md](bandscope.md) |
| **BattleBridge** | Bottlenose | 001 | 2026-09-22 | Four-browser C6-hosted arena race/battle: gates, PvP, enemies | add-on (Bottlenose) | [battlebridge.md](battlebridge.md) |
| **BleDeck** | Bottlenose | 004 | 2026-09-21 | BLE HID remote: four pages, pairing lock, pack % | add-on (Bottlenose) | [bledeck.md](bledeck.md) |
| **ChirpMail** | Sub-GHz | 007 | 2026-09-27 | Two OGs; 24 canned + T9; ACK PIN; host helper; 433.92 | native | [chirpmail.md](chirpmail.md) |
| **DiskGlass** | Storage | 009 | 2026-09-26 | FatFs library: T9 IR compose, loop WASM, delete, IR/QR | native | [diskglass.md](diskglass.md) |
| **EmsDesk** | Lab | 004 | 2026-09-27 | Landing: BandScope TwinFox TireEar ISMburst FobReplay ChirpMail OpticClick; BandScope chrome on enter | native | [emsdesk.md](emsdesk.md) |
| **FobReplay** | Sub-GHz | 006 | 2026-09-26 | ASK/OOK capture, T9 8.3 names on /fobreplay, unused-code queue, PREDICT | native | [fobreplay.md](fobreplay.md) |
| **FunDesk** | Lab | 004 | 2026-09-27 | Landing: Trail TrailRF VoltPet PitchFork; VoltPet PULL 3 s; PDM stays up | native | [fundesk.md](fundesk.md) |
| **GlassBak** | Storage | 001 | 2026-09-25 | FatFs dump/restore on main CDC; GUI groups files by app | native | [glassbak.md](glassbak.md) |
| **HeaderKit** | Breakout | 004 | 2026-09-22 | Hi-Z-safe I2C scan plus quick physical side-header pinout | native | [headerkit.md](headerkit.md) |
| **HostDeck** | Host | 003 | 2026-09-26 | Five-button macros; T9 LCD labels; host helper types CDC chords (no TinyUSB HID) | native | [hostdeck.md](hostdeck.md) |
| **InertialTrail** | Sensors | 008 | 2026-09-26 | Peak-valley pedometer, origin-fixed map, T9 /trail title | native | [inertialtrail.md](inertialtrail.md) |
| **InertialTrailRF** | Sub-GHz | 009 | 2026-09-26 | Dual parked RSSI + steps; T9 /trailrf title | native | [inertialtrailrf.md](inertialtrailrf.md) |
| **ISMburst** | Sub-GHz | 007 | 2026-09-26 | T9 names on FAN/DOOR/SPARE, load BURST*.BIN, ASK+2-FSK decode, hunt, /ismburst | native | [ismburst.md](ismburst.md) |
| **KitHome** | Lab | 007 | 2026-09-27 | Landing: eight live tiles; PDM/I2S/radio mux; wasm3 DiskGlass | native | [kithome.md](kithome.md) |
| **LanFerry** | Bottlenose | 006 | 2026-09-22 | Bottlenose AP so two of your machines can swap files | add-on (Bottlenose) | [lanferry.md](lanferry.md) |
| **MicScope** | Audio | 004 | 2026-09-23 | PDM spectrogram: Gray-hold quiet cal, bars as dB SPL approx | native | [micscope.md](micscope.md) |
| **OpticClick** | IR | 002 | 2026-09-25 | Multi-PHY capture, 24 FatFs slots, T9, NEC library | native | [opticclick.md](opticclick.md) |
| **OrcaLobby** | Bottlenose | 004 | 2026-09-27 | Landing: LanFerry PingHalo BleDeck AirMaraud; PingHalo LIB_GET 3 s after tile enter | add-on (Bottlenose) | [orcalobby.md](orcalobby.md) |
| **PingHalo** | Bottlenose | 023 | 2026-09-27 | BLE hunt: freeze locks list; T9 LABEL persist; YEL deletes slot | add-on (Bottlenose) | [pinghalo.md](pinghalo.md) |
| **PinDesk** | Lab | 003 | 2026-09-27 | Landing: HeaderKit, QwiicBench, TtyGlass, Retia (full tiles) | native | [pindesk.md](pindesk.md) |
| **PitchFork** | Audio | 003 | 2026-09-26 | Tuner: Gray note, hold AUTO, Red ship only; floor 100 | native | [pitchfork.md](pitchfork.md) |
| **QwiicBench** | Breakout | 002 | 2026-09-26 | I2C bench; T9 nick unknowns (RAM); Hi-Z other buses | native | [qwiicbench.md](qwiicbench.md) |
| **Retia (Bus Pirate)** | Breakout | 005 | 2026-09-27 | USB CDC bus terminal: HiZ, GPIO, I2C, UART and SPI on the 3.3 V breakout | native | [retia.md](retia.md) |
| **RigGlass** | Host | 002 | 2026-09-26 | Host CPU/RAM/net/GPU/temp now+2m+10m over main CDC | native | [rigglass.md](rigglass.md) |
| **TalkClip** | Audio | 006 | 2026-09-26 | VAD notes; Blue-hold hang; Gray-hold T9 names last CLIP; /talkclip | native | [talkclip.md](talkclip.md) |
| **TireEar** | Sub-GHz | 004 | 2026-09-27 | TPMS 315/433 ASK or 2-FSK; OEM profiles; 45 s scan and review | native | [tireear.md](tireear.md) |
| **ToneBox** | Audio | 004 | 2026-09-27 | Tone museum + BLUEBOX pulse sequences on I2S; C5/DTMF/coin; header relay H | native | [tonebox.md](tonebox.md) |
| **TtyGlass** | Breakout | 004 | 2026-09-26 | Header UART RX allowlist (UART1/GP26/GP27/CTS); freeze; T9 find | native | [ttyglass.md](ttyglass.md) |
| **TwinFox** | Sub-GHz | 005 | 2026-09-26 | Dual CC1101 hunt; me/u RSSI; 1 kHz digits; 315/433/915 | native | [twinfox.md](twinfox.md) |
| **VoltPet** | Sensors | 005 | 2026-09-26 | Eight foes, dash-in fights, random spark until you let go; T9 name | native | [voltpet.md](voltpet.md) |

### Retired / blocked

Not shipped. Design notes stay so the names are not reopened as “missing
from the A–Z list.” **IsmDesk** was the working name for **EmsDesk**; do
not track it as a second combo.

| Name | Fit | Reason | Page |
|---|---|---|---|
| **WifiHear** | retired | too similar to other apps or not native (AirMaraud already scans) | [wifihear.md](wifihear.md) |
| **BeamTag / BeamScope** | retired | too similar to other apps or not native (OpticClick) | — |
| **SondeChase** | retired | too similar to other apps or not native | — |
| **DitDash / TiltMaze** | retired | too similar to other apps or not native (VoltPet) | — |
| **ChipSip** | retired | too similar to other apps or not native (QwiicBench / Retia) | — |
| **UART GPS / CAN dongle** | retired | too similar to other apps or not native (adapter, not firmware-only) | — |
| **MeshIsle** | blocked | CC1101 is not LoRa; Bottlenose is not a Meshtastic PHY | [meshisle.md](meshisle.md) |
| **StickPeek** | blocked | both RP2040s are USB devices; no host port | [stickpeek.md](stickpeek.md) |
| **KissIsle** | retired | KISS TNC / Reticulum not on the current pass | [kissisle.md](kissisle.md) |

`ToneBox` plays named tones and historical coin pulse trains into the
speaker. Using those against a telephone network is a crime. v004's
BLUEBOX page can play tuts sequences and a header relay `H`; it still
has no POTS interface. Flipper apps are not binary-compatible here —
see [flipper.md](flipper.md).

## How these were built

This BSP (`wiliOGbsp`) is a clean C re-implementation of the original
FreeWili 1 firmware library (`rmpLib`), not a fork. The board is two
RP2040s: a display CPU (LCD, five buttons, LEDs, IR, mic, speaker,
sensors) and a main CPU (two CC1101s, FPGA, breakout). Firmware follows
a contract: `VERSION` / `DESCRIPTION`, 6 s red power-off on the display,
1200-baud BOOTSEL, USB identity, a `fwog_uf2_info_t` record in every UF2,
and a watchdog kick on every main app. The rules live in
[AGENTS.md](../../AGENTS.md), the [root README](../../README.md), and the
slate addendum [bsp-addendum.md](../bsp-addendum.md).

Each product is one folder (`apps/<name>/` with `display/` and `main/`).
CMake declares `fwog_display_app` / `fwog_main_app`. The main UF2 embeds
the display image and is what you flash. Never UF2-flash a display
application except `bl_display` (the once-per-board serial bootloader).

LCD chrome is iterated on the host emulator (`fw emu`,
[`tools/ogemu`](../../tools/ogemu/README.md)). Radios, FatFs, and
Bottlenose are not in that emulator. Pure logic — parsers, game rules,
CRC, FatFs geometry — is host-tested under `fw test` with no SDK.

Coding agents (Cursor, on the bench PC) implemented and iterated most of
the product apps against that BSP, those host tests, and ogemu scripts,
then flashed the main UF2 and checked the board: LCD and WS2812 over a
Wyze bench camera, USB CDC consoles for the rest. The BSP invariants
were paid for on hardware before that workflow existed; the apps ride on
them. That is not a claim that the BSP itself was “written by AI.”

Bottlenose (ESP32-C6 on the Orca header) is one ESP-IDF image with
modes: LanFerry SoftAP, BattleBridge arena, PingHalo BLE scan, BleDeck
HID, AirMaraud 2.4 GHz lab. Yellow-hold on a Bottlenose-capable OG app
ROM-flashes `fwog_c6.bin` (~1.5 MB, embedded in that main UF2) into the
C6. Combining Bottlenose *OG lobbies* is an RP2040 landing problem, not
a second C6 binary.

## Combo images

Stand-alone apps stay. Named groups are landings in one UF2: one display
image with tiles, one main that muxes exclusive hardware. Landing chrome
is PinDesk-scale (four rows, scale-2 names, blurbs). More than four tiles
paginates; `^` / `v` in the right corners when there is a previous or next
page. Full members / conflicts / flash numbers:
[combo-images.md](combo-images.md).

| Combo | Verdict | Why (resource) |
|---|---|---|
| **KitHome** | **shipped 007** | Eight live tiles, four per page. PDM MicScope/TalkClip/ToneBox; I2S DiskGlass/ToneBox; radio0 BandScope or DiskGlass replay |
| **EmsDesk** | **shipped 004** | Seven ISM/IR tiles, four per page. One radio live; SEL 0x5F. (Working name was IsmDesk.) |
| **SoundDesk** | **drop** | MicScope already on KitHome; PitchFork is the same PDM/FFT; DiskGlass already plays clips |
| **OrcaLobby** | **shipped 004** | LanFerry PingHalo BleDeck AirMaraud, no BattleBridge. One C6 mode; BLUE hold ROM-flashes C6; PingHalo 3 s LIB_GET |
| **FileDesk** | **drop** | DiskGlass browses the volume; KitHome already ships GlassBak |
| **PinDesk** | **shipped 003** | HeaderKit, QwiicBench, TtyGlass, Retia — full tiles, sequential I2C then UART1 then Retia CDC |
| **FunDesk** | **shipped 004** | Trail / TrailRF / VoltPet / PitchFork. One radio: TrailRF or VoltPet; PitchFork AUTO is BLUE hold; VoltPet 3 s PULL |
| **HuntDesk** | **drop** | InertialTrailRF already is the walk; PingHalo needs UART1/C6 |
| **WalkThree** | **drop** | Trail + RSSI already lives in InertialTrailRF |

### KitHome — shipped 007

**Members:** HostDeck, MicScope, BandScope, GlassBak, DiskGlass, TalkClip,
ToneBox, RigGlass (all live). Firmware in `apps/kithome/`. Bench-signed
**007**.

**Why:** this-PC macros, acoustic survey, sub-GHz listen, FatFs backup,
files, clips, tone museum, host telemetry.

**Resource conflicts:** PDM exclusive MicScope vs TalkClip vs ToneBox;
I2S DiskGlass or ToneBox; radio0 BandScope or DiskGlass REPLAY only
(`KIT_MSG_SEL`). YEL/GRN/GRY hold home; colliding member holds moved to
**BLUE**. RigGlass CDC on main USB. ToneBox **H** on UART1.

### EmsDesk — shipped 004

**Members:** BandScope, TwinFox, TireEar, ISMburst, FobReplay, ChirpMail,
OpticClick. Firmware in `apps/emsdesk/`. Two pages. One radio live
(OpticClick idles both). See [emsdesk.md](emsdesk.md). Landing SEL is
**0x5F** (0x70 is DiskGlass). YEL/GRN/GRY hold home; member holds moved
to **BLUE**.

### OrcaLobby — shipped 004

**Members:** LanFerry, PingHalo, BleDeck, AirMaraud. No BattleBridge.
Firmware in `apps/orcalobby/`. UART1 is the C6 link. **BLUE hold**
ROM-flashes `fwog_c6.bin` (yellow hold is home). See [orcalobby.md](orcalobby.md).

### FunDesk — shipped 004

**Members:** InertialTrail, InertialTrailRF, VoltPet, PitchFork. Firmware
in `apps/fundesk/`. See [fundesk.md](fundesk.md). TrailRF or VoltPet owns
the CC1101s, never both. PitchFork AUTO is **BLUE hold** (gray hold is home).

### PinDesk — shipped 003

**Members:** HeaderKit, QwiicBench, TtyGlass, Retia. Firmware in
`apps/pindesk/`. See [pindesk.md](pindesk.md).

**Resource conflicts:** one I2C master at a time; TtyGlass UART1 is
Bottlenose’s UART, so not on an Orca landing. Retia CDC is on **main USB**
while that tile is open.

Flash budget is not the reason to hesitate. Each RP2040 has 16 MB
(`PICO_FLASH_SIZE_BYTES`). Display: 128 KB bootloader + 4 KB metadata,
app at `0x10021000`. Main: firmware in the first 8 MB; the last 8 MB is
FatFs (`FWOG_FS_FLASH_OFFSET`) — do not plan app code there. Measured
non-LVGL display `.bin` files in `build/apps/` are ~221–248 KB. Main
native `.bin` files are ~362–553 KB including the embedded display;
Bottlenose mains are ~1.7–1.95 MB because they also embed `fwog_c6.bin`.
What binds is **RAM**, **one user of a peripheral at a time**, and C6
app-partition headroom — not 16 MB.

**Hard no’s** (detail in [combo-images.md](combo-images.md)): TwinFox +
ChirpMail + VoltPet live together; any combo that runs two CC1101
personalities at once; Orca + TtyGlass (UART1); two C6 binaries; app
code in FatFs; a second FPGA bitstream; UART0 stdio; LVGL product
landing; MeshIsle/StickPeek tiles.

### Show-floor packs (3–5 flashes)

1. **Booth — KitHome 007** (eight live tiles; two pages).
2. **RF village — EmsDesk 004**.
3. **Bottlenose — OrcaLobby 004** (or BattleBridge dedicated). Not TtyGlass.
4. **Sound / toy — FunDesk 004** (or PitchFork / ToneBox stand-alone).
5. **Breakout — PinDesk 003**.

## Revving an app

Operator-visible firmware in `apps/<name>/` is a new `VERSION`. Bump the
three-digit field in that folder’s `CMakeLists.txt` (both `_display` and
`_main` when both exist) and `fwog_splash_bind` if the display half uses
it. Then:

1. Update `docs/apps/<name>.md` (Status line) and this catalog’s `Ver` /
   `Last` row.
2. `fw build <name>_main && fw flash <name>_main`. Never UF2-flash a
   display application. If Bottlenose firmware changed, the main UF2
   embeds `firmware/bottlenose/fwog_c6.bin`; Yellow-hold flash the Orca
   from that image.
3. **One git commit per `VERSION`.** Do not combine two version numbers
   in one commit. Conventional Commits, app in the scope, version in the
   subject: `feat(airmaraud): 009 list beacons again, YEL FRZ while ON`.
   `fw new-app` is **001**. Catalog or procedure edits with no VERSION
   bump are `docs:`.

USB serial how-to (both CDCs, 1200-baud BOOTSEL, which apps dump data):
[console.md](console.md). Display UI can be exercised on the PC (no board)
with [`tools/ogemu`](../../tools/ogemu/README.md): `fw test` builds and
smokes the harness; `fw emu --gui` opens the panel. Radios and the main
CPU are not in the emulator. A 2026-09-12 headless pass (MicScope wired,
world-bus limits, three refusals) is [ogemu-beta.md](ogemu-beta.md).

**Product chrome (from 2026-09-26).** Every display app that calls
`fwog_splash_boot()` shows the NES-box splash with the DEF CON
smiley-and-crossbones seal bottom-right and a **DEF CON** sign-off under
the version. Every app on `FWOG_POWER_DEFAULT()` plays
`bsp/display_cpu/lcd/art/bad_feeling_8k.wav` on the 6 s red power-off path
(`fwog_splash_ship()`), overlay **GOODBYE** / **sign off**. Re-flash the
**main** UF2 to pick this up; it lives in `fwog_display_bsp`, not in
each app folder.

## Why these and not others

The OG is two RP2040s: display (LCD, five buttons, 7 LEDs, IR, PDM mic, I2S
speaker, LIS3DH, RTC) and main (two CC1101s, iCE40 with a **frozen**
bitstream, breakout SPI/UART/I2C/GPIO). Stock, there is no BLE, Wi-Fi, GPS,
magnetometer, gyro, or USB host port.

A **Bottlenose** Orca (ESP32-C6 on the 20-pin header) adds 2.4 GHz Wi-Fi
and Bluetooth. That is what moves PingHalo from "needs a module" to "the
module is on the desk," and it is why LanFerry is a Wi-Fi file-drop
instead of a fantasy. It does not add USB host, NFC, cellular, a
magstripe head, or a Semtech LoRa radio, so StickPeek and
[MeshIsle](meshisle.md) stay blocked and the silicon-failed rows in
[retired.md](retired.md) do not come back.

That is a strong sub-GHz / IR / analog / breakout kit, a decent Wi-Fi/BLE
kit once Bottlenose is talking, and a weak phone-clone kit.

## Scoped — authorization, then silicon

These were held until authorization, silicon, and a remainder worth a page
were written down. **Silicon that can do a thing is not a reason to ship
it as a toy.** Authorized professional pentest use is the authorization
bar the catalog asks about; it does not unblock a PHY this board does not
have. Full notes: [retired.md](retired.md).

| Idea | Silicon on OG + Bottlenose | Verdict |
|---|---|---|
| RF denial (jammer-class TX) | CC1101 can transmit in ISM, narrowband ~10 dBm | **in-scope** for authorized RF assessments; not a consumer gadget |
| HID injector | TinyUSB can enumerate as a keyboard to *this* PC | **in-scope** toward an engagement target; [HostDeck](hostdeck.md) is the operator-at-the-keyboard remainder |
| Rolljam / rolling-code RF | Two CC1101s make the topology available | **in-scope** for authorized vehicle/access RF tests; FobReplay today is unused-code replay only |
| Credit-card reader | No magstripe, no EMV slot, no 13.56 MHz NFC | **blocked** — wrong silicon; authorization does not grow a reader |
| Cellular IMSI catcher | Wrong radio, not a cellular baseband | **blocked** — wrong silicon; authorization does not grow a BTS |
| 2.4 GHz deauth (AirMaraud) | C6 `esp_wifi_80211_tx` documents beacon/probe/action/data only; IDF `ESP_OK` is not a sniffer | **firmware** in [airmaraud.md](airmaraud.md): scan is real; TX is armed one-shot and **unproven on this C6**. A separate scan-only app (WifiHear) is retired |
