# Combo images

Stand-alone apps stay. Named groups here are **landings**: one display
image with tiles, one main UF2 that embeds that display image, and
**KitHome-style mux** for exclusive hardware. Landing chrome is four
PinDesk-scale rows per page with `^`/`v` corners.

**Shipped:** [KitHome](kithome.md) **007** (eight live tiles),
[PinDesk](pindesk.md) **003**, [OrcaLobby](orcalobby.md) **004**,
[FunDesk](fundesk.md) **004**, [EmsDesk](emsdesk.md) **004**.

The mux pattern is already on the board. KitHome display sends
`KIT_MSG_SEL` (`apps/kithome/kit_proto.h`); main idles the CC1101 unless
the live tile is BandScope. That is the only supported way to put more
than one radio *job* in a binary. It is **not** a way to run TwinFox,
ChirpMail, and VoltPet radios at the same time.

**Landing chrome contract** (copy this, do not invent a sixth exit):

- **Gray tap** moves the landing cursor **up**; **red tap** moves it
  **down**. Green tap opens the tile.
- **Yellow, green, or gray hold ~0.7 s** leaves the tile and returns to
  the list. Tap still does the tile’s job.
- **Enter a tile (and go home) with a full 320×240 clear**, then paint.
  Header-only fill leaves KitHome list glyphs in the body.
- **Red hold 6 s** ships. Do not paint the tile (or the landing) while
  `fwog_power_poll` is armed, and **latch GOODBYE** — USB can keep SYS
  up after `BATFET_DIS`, and v002 drew BandScope/MicScope on top of the
  splash.

Catalog index: [README.md](README.md) (Proposed combo images).

## Flash budget (why most combos fit)

| Region | Size | Combo implication |
|---|---|---|
| Display flash | 16 MB | 128 KB bootloader + 4 KB metadata; apps link at `0x10021000`. Measured non-LVGL display `.bin` ~221–248 KB (KitHome 002 **232 352** bytes for four tiles). |
| Main firmware | first **8 MB** | Native mains ~362–553 KB including the embedded display. Bottlenose mains ~1.7–1.95 MB because they also embed `fwog_c6.bin` (**1 515 056** bytes). Last 8 MB is FatFs (`FWOG_FS_FLASH_OFFSET`) — **never** place app code there. |
| C6 / Bottlenose | ~4 MB | One ESP-IDF image. `SINGLE_APP_LARGE` is `0x177000` (1 536 000). Current `fwog_bottlenose.bin` **1 449 520** bytes — **5.6 %** free (BattleBridge cover art). Extra OG *tiles* do not add a second C6 binary. |
| LVGL (not used) | ~390 KB flash, ~143 KB RAM of 264 KB | Do not pull `lvgl_display` into a product landing. |

Binding limits are **RAM** (FFT, capture BSS, wasm3), **one user of a
peripheral at a time**, and C6 mode/partition headroom — not 16 MB.

`DESCRIPTION` strings in `apps/*/CMakeLists.txt` are UF2 catalog text,
not size. DiskGlass is the heavy native pair (~247 KB display / ~553 KB
main, wasm3 + ASK replay). KitHome main is **507 160** bytes, display
BSS ~27 KB of 264 KB.

## Shared-resource matrix

Legend: **own** = this app needs the resource while its tile is live.
**file** = names coexist on the one FatFs volume; writers take turns.
**no** = does not use it.

| App | CC1101 | FatFs (last 8 MB) | Sensors / audio / IR | LCD + 5 buttons | C6 / UART1 |
|---|---|---|---|---|---|
| KitHome (BandScope tile) | own (sweep, one radio) | file (`mscope.cal`; GlassBak dump) | PDM (MicScope) | landing mux | no |
| BandScope | own (RSSI sweep) | no | no | own | no |
| TwinFox | **both** ears, same park | no | no | own | no |
| ChirpMail | own, 433.92 2-FSK | file `/chirpmail/CANNED.TXT` | no | own | no |
| VoltPet | own, hop 315 / 433.92 / 868 ASK | file `voltpet.bin` | LIS3DH + mic snacks | own | no |
| ISMburst | own ASK/2-FSK RX/TX | file `/ismburst` | no | own | no |
| TireEar | own 315/433 ASK or 2-FSK | no | no | own | no |
| InertialTrailRF | **both** parked RSSI | file `/trailrf` | LIS3DH | own | no (Bottlenose off) |
| FobReplay | own ASK/OOK capture/TX | file `/fobreplay` | no | own | no |
| DiskGlass | own only while ASK-replay | file (library) | I2S; IR compose | own | no |
| TalkClip | no | file `/talkclip` | PDM VAD | own | no |
| MicScope / PitchFork | no | `mscope.cal` (MicScope) | PDM + FFT | own | no |
| ToneBox | no | no | I2S out | own | no |
| OpticClick | no | file slots | IR | own | no |
| InertialTrail | no | file `/trail` | LIS3DH | own | no |
| HostDeck / RigGlass | no | no | no | own | no |
| GlassBak | no | dump/restore volume | no | own | no |
| HeaderKit / QwiicBench | no | no | breakout I2C | own | no |
| TtyGlass | no | no | no | own | **UART1 RX** (not C6) |
| Retia | no | no | FPGA SPI (frozen bitstream) | own | header UART/SPI modes |
| PingHalo / BleDeck / LanFerry / BattleBridge / AirMaraud | no CC1101 | PingHalo labels | no | own | **C6 mode** on UART1 |
| MeshIsle / StickPeek | — | — | — | — | blocked silicon |

Hard exclusives (cannot be **live** together, even if both code paths
are in the UF2):

- **UART0** — inter-CPU link; never stdio.
- **CC1101 pair** — two radios total. TwinFox and InertialTrailRF each
  take **both**. Mux: idle unless that tile is live (`KIT_MSG_SEL`).
- **FPGA bitstream** — frozen one image. Retia uses it; no second
  bitstream in a combo.
- **LCD, five buttons, WS2812** — one chrome (the landing).
- **PDM / I2S / IR / LIS3DH** — one consumer while the tile is live.
- **UART1** — Bottlenose **or** TtyGlass, not both.
- **C6 modes** — one SoftAP, one BLE role; HID + scan already refused.
  Ferry AP and arena AP do not run together.
- **FatFs** — files coexist; open/write is one-at-a-time.

PingHalo, RigGlass, and TalkClip are **not** CC1101 users.

## Combos that fit (flash + explicit mux)

| Combo | Members | Flash | Mux | Verdict |
|---|---|---|---|---|
| **KitHome** | HostDeck MicScope BandScope GlassBak DiskGlass TalkClip ToneBox RigGlass | eight live, four/page | PDM exclusive; I2S DiskGlass/ToneBox; radio0 BandScope or DiskGlass replay; wasm3 | **Shipped 007** |
| **EmsDesk** | BandScope TwinFox TireEar ISMburst FobReplay ChirpMail OpticClick | 004 | One radio; shared 20 KB capture; SEL 0x5F | **Shipped 004** |
| **OrcaLobby** | LanFerry, PingHalo, BleDeck, AirMaraud | 004 + C6 embed | One C6 mode; BLUE hold ROM flash; PingHalo 3 s LIB_GET; no TtyGlass; no BattleBridge | **Shipped 004** |
| **PinDesk** | HeaderKit, QwiicBench, TtyGlass, Retia | full tiles 003 | Sequential I2C. UART1 = TtyGlass ⇒ not Orca. Retia CDC on main USB | **Shipped 003** |
| **FunDesk** | Trail, TrailRF, VoltPet, PitchFork | 004 | One accel/radio; PDM claimed at boot; TrailRF xor VoltPet on CC1101; VoltPet 3 s PULL | **Shipped 004** |
| **WalkThree** | InertialTrail, InertialTrailRF, TwinFox | — | Trail + RSSI already lives in InertialTrailRF | **drop** |

A later **OpticClick** tile on KitHome does not fight the radio (IR is
display-CPU). **ISMburst** on KitHome would, and stays on **EmsDesk**.

## Attractive combos that fail

| Idea | Why it fails |
|---|---|
| **Radio zoo live:** TwinFox + ChirpMail + VoltPet (plus ISMburst / TireEar / FobReplay / KitHome sweep) all receiving | Dual-radio exclusive. Two CC1101s cannot be three PHYs. **Do not invent a mega-app that runs them simultaneously.** Mux is one live tile, not concurrent radios. |
| **EmsDesk without mux** | Seven jobs in one UF2 is fine; seven receivers at once is not. |
| **SoundDesk:** MicScope + PitchFork + TalkClip + ToneBox + DiskGlass | Flash fits (DiskGlass still ≪ 8 MB). Fails as a *combo worth building*: MicScope is already on KitHome; PitchFork is the same PDM/FFT; DiskGlass 009 already plays `/talkclip`. Three PDM clients + I2S fight. |
| **FileDesk:** TalkClip + DiskGlass + GlassBak + ISMburst + InertialTrailRF | ISMburst and TrailRF both want CC1101s; DiskGlass ASK-replay wants radio0. GlassBak already ships on KitHome; DiskGlass already lists the files. FatFs folders coexist without a fifth landing. |
| **HuntDesk:** InertialTrail + TrailRF + TwinFox + PingHalo | Flash would fit (PingHalo ~1.95 MB with C6). **Fails as a simultaneous hunt:** TrailRF and TwinFox both take both CC1101s; PingHalo needs UART1 as the C6 link; trail apps leave Bottlenose off. |
| **OrcaLobby five-way including BattleBridge + TtyGlass** | UART1 exclusive. BattleBridge JPEG is why the C6 factory partition has ~86 KB left — keep the arena its own UF2. Extra C6 assets fail the partition, not extra OG tiles. |
| **Two `fwog_c6.bin` in one main UF2** | One C6 flash image. |
| **LVGL landing + product tiles** | Display RAM, not flash. |
| **App code in last 8 MB** | That half is FatFs. |
| **Second FPGA bitstream** | Frozen (`AGENTS.md` contract 4). |
| **stdio on UART0** | Corrupts the inter-CPU link. |
| **MeshIsle / StickPeek tiles** | Blocked silicon. |

## Recommended DEF CON / show-floor packs

Three to five **flashes** per table, not a mega-binary. Dedicated UF2s
win when the demo must **own** the radio or the C6 for the whole talk.

| Pack | What is on the board | Why |
|---|---|---|
| **1. Booth default** | **KitHome 007** (eight live tiles, two pages) | Shipped. Orthogonal jobs. `KIT_MSG_SEL` is the mux example. |
| **2. RF village** | **EmsDesk 004** | One ISM kit, one radio live. |
| **3. Bottlenose table** | **OrcaLobby 004** or **BattleBridge** dedicated | Shared `fwog_c6.bin`, one mode. No TtyGlass. |
| **4. Sound / toy** | **FunDesk 004**, or PitchFork / ToneBox stand-alone | Not SoundDesk. KitHome already has MicScope. |
| **5. Breakout** | **PinDesk 003** | HeaderKit / QwiicBench / TtyGlass / Retia. Not on the Bottlenose table. |

Mux landings are **bench-signed**: KitHome 007, EmsDesk 004, OrcaLobby 004,
FunDesk 004, PinDesk 003. Public-tree secrets review is still required
before a public remote; it is not a hold on those UF2s.

**IsmDesk** was the working name for EmsDesk. Do not track it as a second
combo.
