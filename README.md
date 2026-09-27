# fwog_defcon

Village firmware cut of the FreeWili OG dual-RP2040 BSP. UF2s for
[FreeWili OG App Explorer](https://github.com/freewili/fwOGAppExplorer)
are GitHub Release assets (`defcon-2026`), not files in this tree.
Upstream BSP: [freewili/wiliOGbsp](https://github.com/freewili/wiliOGbsp).

## Introduction

At DEF CON 33, I bought a FreeWili embedded device as an alternative to a
Flipper Zero, thinking that I'd be going all sorts of cool things with it.
A year later, it mostly sat on a shelf, and at DC34, the makers of the
FreeWili (Intrepid Control Systems) made a new version that largely ignored
the original — but were willing enough to release some updated tools for
firmware flashing and a board support package (BSP) intended to help humans
and AI agents alike to build "apps" for the pretty nifty device.

Having followed the latest AI craze for some time, I decided to adopt a full
AI dev workflow with human product owners to build a slate of hacker focused
apps that run on the FreeWili "OG" — that is what I have in this repo. It
includes the built UF2, helper python apps, descriptions of the tools, and
additional readmes and addendums to the BSP that should give an AI agent a
small step up — I say small because my total resource investment in this was
less than $40, maybe 30 hours of my own time, and a couple of weeks working
a few hours in the evening. What I ended up with is a physical test bench
that can see the OG screen to verify landing screens, a separate locally run
uncensored model to guide/force the questionable pentest bits, and a better
understanding of how a human can improve an AI workflow (and might even be
necessary), the OG hardware itself, and some practices to do more of this AI
workflow in my professional and personal life. And all the source is there as
well as links to the libraries and resources I borrowed from, so go to town.
Note that all this is provided as-is with rights to the original developers
and that it is intended for "research" and "academic use" as the end user
deems appropriate.

First I want to document my human thoughts on what I learned categorized by
workflow and the OG hardware.

### Hardware

1. Communicating across the UART is a PITA. We computer users/homelabbers
   just assume that data can flow easily between embedded systems but there
   is a lot of constraints that must be dealt with like not overloading the
   bandwidth of the channel, not capturing or sending at an acceptable rate,
   garbled or misdirected signals, and overall inability to leverage the OG
   processing when you have an orca like bottlenose (Wifi + BLE) attached.
2. Speaking of the bottlenose, the boot and reset buttons are a PITA
   especially if you are developing. Because I wanted to be able to load
   bottlenose firmware from the OG UF2, it had to be sent over UART, and this
   was a fairly inconvenient button press sequence. I tried to AI build a 3D
   printed case with better buttons, but that was not achievable by the
   agents — I will probably do a photogrammetry scan and then edit it in
   tinkercad so the bottlenose is protected when I bring it to DC35.
3. The USB-C is largely a serial comm. Ok for one way and flashing but it'd
   be awesome if it could also double as a usb mass storage interface. This
   is actually possible via the UART but requires some hardware and shorting
   a few pins; the plan is documented and might be something I do. However,
   the OG has a fatfs storage of around 8 MBs which I used to store clips,
   persistent settings, etc. and then made a separate app and desktop helper
   app that dumps the data.
4. I think I understand this correctly, but the FPGA is largely used to
   adjust the GPIO header and is not really good for any quick processing
   "chip" — I was half thinking I could make a hardware decoder/encoder but
   after looking at it briefly, it just isn't meant for that type of work.
5. I'd have to go back and check, but even though the C1101 radios support
   433 Mhz near the Meshtastic/Reticulum bands, there is some extra magic
   that makes them non-starters.
6. I have some real concerns about the 6 buttons with how much I push them,
   but with 6 I can do almost any interface imaginable. I think the only
   think I might want is a D-pad on the back which would then leave the front
   facing buttons for specific actions instead of navigation. I liked
   thinking of some throwback technology like T-9 keyboards, so as a product
   owner, it was fun to think about how to achieve functionality with the
   limited hardware.
7. I wish there was NFC, something that could emulate MIFARE/HID cards. I
   have an m5 atom s3 with RFID2 which can read some mifare, but ultimately I
   want to pen test things and that means doing emulation...

### Workflow

1. Which leads me to my eventual setup and why I think I need to run an
   uncensored setup somewhere. Grok and cursor tend to have very few
   guardrails but it does stop when asking it to do something illegal. I had
   bought a panther lake 32 GB unified memory laptop with an arc B390
   thinking I'd poke around some inference, and I ended up needing to set up
   llama.cpp with an abliterated qwen3.8 model to take care of the illegal
   bits. What's neat is that I had the agents design its own interface so my
   laptop could take a look at the desktop local repos, task an agent worker,
   and basically orchestrate what was going on — all at 2-5 tokens/sec. I
   think my final setup would be a main cursor grok 4.6 orchestrator,
   composer 2.5 or cheap sub agents, and then an uncensored "consultant" that
   would monitor what items were blocked by policy or guardrails and then it
   would find a way around that block or end up doing itself (at very slow
   rate).
2. The whole intent was to create these iteratively, so instead of just fire
   and forget, there was a lot of taking a look at the flashed "chrome" and
   then giving adjustments. Sometimes, I felt like a meat puppet. Sometimes,
   I felt that there was no way the agents would figure it out without me.
   What I do know is that agents tend to solve with more — more code, more
   workaround, more documentation, more everything. In my work setups, I find
   that the agents tend to find the problem faster than me, but then fail to
   see a simple, elegant solution right in front of it. There was a lot of
   back and forth, and I read the thoughts to understand what was going on
   and to help figure out my next orders; sometimes a solution broke things
   and the human was necessary to provide that feedback.
3. Cursor is pretty slick and easily spins up subagents — my Claude at work
   is similar. I think it defaults and gravitates to "software engineer"
   speak, which I told it to take more tokens to explain, but I started using
   terms like blocked, chrome, smoke test, etc. after a few hours of
   interaction. I write software at work but am not a dev myself (more like
   prototyping tools). Hermes on the laptop is neat because you don't need to
   steer, it just adapts to the next prompt you give. Cursor does take a
   steer, but I found myself just queuing next tasks in a logical manner
   while having it keep track of all the outstanding tasks. When I tried to
   use different chats to parallel path work, it was a little ugly with
   commits. Thus, I ended up specifying that the main chat should task
   subagents and only sequence retests (due to the flash to hardware
   bottleneck) when the main chat and I were ready.
4. Many tools and skills and markdowns were made to help the agents not start
   from zero — while this worked well for my multiple sessions spread across
   weeks, I wonder how well a new human/AI team might benefit. Regardless,
   all this could be recreated with decent prompting and a couple hundred
   million tokens later (lolz — luckily, Cursor does accounting a little
   differently than ChatGPT and Claude but this is still a lot I think).

So is it AI slop? Sure. But it is also human slop. And more importantly,
where before there was no functionality beyond that the original FreeWili
team provided, there is a whole product slate of things that can be used now.
And where I was largely ignorant about the OG hardware/software before, I at
least know what is possible and approaches to design apps. If I was doing
this full time, I probably could have made something in 3-9 months. The whole
product slate? Probably not. And if I wanted to make something completely
different, I'm probably an hour or two of background research away before I
can almost one-shot a functional firmware build. Plus, all this automation
gives me more time to chat on Discord and read Phrack, so winning all
around? Until next time! (and if the FreeWili team wanted to send a FW2 my
way, I would not reject it!)

---

Board-support monorepo for the **FreeWili 1 OG**, which carries two RP2040s: a
display CPU (ST7789 LCD, five color buttons, 7 WS2812 LEDs, IR, PDM mic, I2S
speaker, I2C sensors) and a main CPU (two CC1101 sub-GHz radios, an iCE40
FPGA, user breakout I/O). One CMake configure builds both.

Agents and contributors: read [AGENTS.md](./AGENTS.md) first.

**FreeWili 1 OG** is the new name for the FreeWili 1 firmware. It supports
easy bootloading, open-source hardware, and generating a single UF2 file that
flashes both CPUs.

## Contents

1. [Introduction](#introduction)
2. [Loading what you build — the FreeWili OG App Explorer](#loading-what-you-build--the-freewili-og-app-explorer)
3. [Quick start](#quick-start)
4. [What's in `apps/`](#whats-in-apps)
5. [Product catalog](#product-catalog)
6. [LVGL](#lvgl)
7. [Public tree](#public-tree)
8. [Status](#status)
9. [License](#license)

Orientation for agents and the FwOGapp contract: [AGENTS.md](./AGENTS.md).
Product-slate addendum: [docs/bsp-addendum.md](docs/bsp-addendum.md).
Pin map: [docs/hardware/pinmap.md](docs/hardware/pinmap.md). Host emulator:
[tools/ogemu/README.md](tools/ogemu/README.md). Combo landings:
[docs/apps/combo-images.md](docs/apps/combo-images.md).

## Loading what you build — the FreeWili OG App Explorer

**[FreeWili OG App Explorer](https://github.com/freewili/fwOGAppExplorer)**
loads the apps this BSP builds. Point it at a board and it flashes an
FwOGapp — the single UF2 that carries both CPUs' firmware — without you
having to know which CPU is which or how to reach BOOTSEL.

That works because the two ends agree by construction. Every app built here
carries a `fwog_uf2_info_t` record naming its CPU, app name, version and
description, and a USB identity the host can match; the App Explorer reads
exactly those. An FwOGapp also powers the device on and off consistently and
supports automatic bootloading. This BSP is what builds an app the right way,
so the App Explorer can load it — the rules are enumerated as "The FwOGapp
contract" in [AGENTS.md](./AGENTS.md).

## Quick start

Prerequisites: Pico SDK 2.3.0 and the arm-none-eabi toolchain under
`~/.pico-sdk`, plus Python 3. Host tests additionally want MSYS2 MinGW GCC on
Windows.

```bash
fw bootloader              # once per board: serial bootloader -> display CPU
fw build template_main     # build a main-CPU app (carries the display image)
fw flash template_main     # BOOTSEL that CPU, then it copies the .uf2
fw console                 # attach to the display bootloader's USB console
fw test                    # host unit tests, no hardware
```

`fw bootloader` is the once-per-board step. After it, display firmware is
embedded in main's UF2 and arrives over the inter-CPU link automatically —
one file flashes both CPUs.

Two rules worth knowing before you plug anything in:

- Put only **one** CPU in BOOTSEL at a time. Both present the same USB serial,
  so with two mounted neither the tools nor you can tell which is which.
- Never `fw flash` a display *application*. It will not boot and it takes the
  display CPU off USB — the one CPU with no BOOTSEL button. Display apps ride
  along inside the main CPU's UF2. See [AGENTS.md](./AGENTS.md) for the full
  reason and the recovery path.

## What's in `apps/`

**One folder per app, with the CPU halves inside it.** A display app and its
main companion are one deliverable — the main UF2 carries the display image
inside it — so they live together and share a `CMakeLists.txt`:

```
apps/kithome/
    CMakeLists.txt      declares both kithome_display and kithome_main
    display/main.c
    main/main.c
```

Targets keep their `_display`/`_main` suffix; only the folder drops it.
Flash **`*_main.uf2`** from GitHub Release **`defcon-2026`**. Do not UF2-flash
a display application except `bl_display`.

OG Vegas, WiLiDoro, and Orca Field Notes are **not** in this cut — they stay
in [freewili/wiliOGbsp](https://github.com/freewili/wiliOGbsp). `lcd` and
`lvgl` are in the source tree for bring-up / opt-in UI; they have **no**
Release UF2 here.

### Product slate (Release `*_main.uf2` + App Explorer)

A–Z. Each row is one GitHub Release asset. Pages: [docs/apps/README.md](docs/apps/README.md).

| UF2 | What it is |
| --- | --- |
| `airmaraud_main.uf2` | Bottlenose 2.4 GHz lab: scan APs, arm one deauth/disassoc. [airmaraud.md](docs/apps/airmaraud.md) |
| `bandscope_main.uf2` | Sub-GHz RSSI sweep; freeze+scroll peaks. [bandscope.md](docs/apps/bandscope.md) |
| `battlebridge_main.uf2` | C6-hosted arena: gates, PvP, enemies. [battlebridge.md](docs/apps/battlebridge.md) |
| `bledeck_main.uf2` | BLE HID remote; four pages, pairing lock. [bledeck.md](docs/apps/bledeck.md) |
| `chirpmail_main.uf2` | Two OGs; canned + T9 text at 433.92. [chirpmail.md](docs/apps/chirpmail.md) |
| `diskglass_main.uf2` | FatFs library: T9/IR, WASM, delete, IR/QR. [diskglass.md](docs/apps/diskglass.md) |
| `emsdesk_main.uf2` | Mux landing: BandScope TwinFox TireEar ISMburst FobReplay ChirpMail OpticClick. [emsdesk.md](docs/apps/emsdesk.md) |
| `fobreplay_main.uf2` | ASK/OOK capture, named slots, unused-code queue. [fobreplay.md](docs/apps/fobreplay.md) |
| `fundesk_main.uf2` | Mux landing: Trail TrailRF VoltPet PitchFork. [fundesk.md](docs/apps/fundesk.md) |
| `glassbak_main.uf2` | FatFs dump/restore on main CDC. [glassbak.md](docs/apps/glassbak.md) |
| `headerkit_main.uf2` | Hi-Z-safe I2C scan + side-header pinout. [headerkit.md](docs/apps/headerkit.md) |
| `hostdeck_main.uf2` | Five-button macros; host helper types chords. [hostdeck.md](docs/apps/hostdeck.md) |
| `inertialtrail_main.uf2` | Peak-valley pedometer, origin-fixed map. [inertialtrail.md](docs/apps/inertialtrail.md) |
| `inertialtrailrf_main.uf2` | Dual parked RSSI + steps. [inertialtrailrf.md](docs/apps/inertialtrailrf.md) |
| `ismburst_main.uf2` | ASK+2-FSK decode, named FAN/DOOR/SPARE, hunt. [ismburst.md](docs/apps/ismburst.md) |
| `kithome_main.uf2` | Mux landing: eight live tiles (booth default). [kithome.md](docs/apps/kithome.md) |
| `lanferry_main.uf2` | Bottlenose AP file drop between two PCs. [lanferry.md](docs/apps/lanferry.md) |
| `micscope_main.uf2` | PDM spectrogram / dB SPL bars. [micscope.md](docs/apps/micscope.md) |
| `opticclick_main.uf2` | Multi-PHY IR capture, FatFs slots, NEC library. [opticclick.md](docs/apps/opticclick.md) |
| `orcalobby_main.uf2` | Mux landing: LanFerry PingHalo BleDeck AirMaraud. [orcalobby.md](docs/apps/orcalobby.md) |
| `pinghalo_main.uf2` | BLE hunt; freeze list; T9 labels. [pinghalo.md](docs/apps/pinghalo.md) |
| `pindesk_main.uf2` | Mux landing: HeaderKit QwiicBench TtyGlass Retia. [pindesk.md](docs/apps/pindesk.md) |
| `pitchfork_main.uf2` | Chromatic tuner on the PDM mic. [pitchfork.md](docs/apps/pitchfork.md) |
| `qwiicbench_main.uf2` | I2C bench; T9 nick unknowns. [qwiicbench.md](docs/apps/qwiicbench.md) |
| `retia_main.uf2` | Bus Pirate-style CDC: HiZ GPIO I2C UART SPI. [retia.md](docs/apps/retia.md) |
| `rigglass_main.uf2` | Host CPU/RAM/net/GPU/temp over main CDC. [rigglass.md](docs/apps/rigglass.md) |
| `talkclip_main.uf2` | PDM VAD clips on `/talkclip`. [talkclip.md](docs/apps/talkclip.md) |
| `tireear_main.uf2` | TPMS-shaped 315/433 ASK or 2-FSK. [tireear.md](docs/apps/tireear.md) |
| `tonebox_main.uf2` | Tone museum + BLUEBOX sequences on I2S. [tonebox.md](docs/apps/tonebox.md) |
| `ttyglass_main.uf2` | Header UART RX allowlist; freeze; T9 find. [ttyglass.md](docs/apps/ttyglass.md) |
| `twinfox_main.uf2` | Dual CC1101 hunt; me/u RSSI. [twinfox.md](docs/apps/twinfox.md) |
| `voltpet_main.uf2` | Eight foes, dash-in fights, T9 name. [voltpet.md](docs/apps/voltpet.md) |

### Also in Release `defcon-2026` (bring-up, not App Explorer catalog)

| UF2 | What it is |
| --- | --- |
| `bl_display.uf2` | Display serial bootloader. Once per board. |
| `bench_main.uf2` | Driver console (`tools/bench.py`). |
| `cpuprobe.uf2` | “Which CPU is this?” |
| `smoke_main.uf2` | Clocks, USB, inter-CPU link. |
| `template_main.uf2` | Skeleton `fw new-app` copies. |

## Product catalog

Operator-facing firmware is the table above. App Explorer JSON is
[`catalog/apps.json`](catalog/apps.json) (same 32 mains; `uf2.url` points at
Release `defcon-2026`). Mux landings: **KitHome 007**, **EmsDesk 004**,
**OrcaLobby 004**, **FunDesk 004**, **PinDesk 003**.

Versions, retired/blocked names, combo verdicts, and how to bump
`VERSION`: [docs/apps/README.md](docs/apps/README.md).

### LVGL

The BSP ships an **LVGL 9 port** — the ST7789 as an LVGL display, and the five
buttons as an LVGL keypad — in `bsp/display_cpu/lvgl/`.

**LVGL itself is not vendored here.** Your project supplies it and the BSP
builds `fwog_display_lvgl` against it, the same arrangement it has with the
Pico SDK. To try the example without wiring that up yourself, let this repo
fetch LVGL for you:

```bash
cmake --preset target -DFWOG_LVGL_FETCH=ON
cmake --build build --target lvgl_main
fw flash lvgl_main
```

Budget for it: roughly **390 KB of flash and 143 KB of RAM** of the RP2040's
264 KB, against ~32 KB for a bare display app. That is why it is opt-in.

## Public tree

What a public remote should carry so others can build the product slate,
docs, and host tools. What stays off that remote.

| Publish | Path | Why |
|---|---|---|
| yes | `bsp/` | Dual-RP2040 BSP; pin maps; FatFs; radios; bootloader |
| yes | `apps/` | Product firmware + `template` / `bl` / `lcd` / `smoke` / `bench` / `cpuprobe` |
| yes | `catalog/` | App Explorer JSON |
| yes | `docs/apps/` | Catalog, screenshots, [combo-images.md](docs/apps/combo-images.md) |
| yes | `docs/hardware/pinmap.md` | Machine-checked pin map |
| yes | `freewili1-docs/` | Legacy OG hardware notes (no longer in upstream wiliOGbsp) |
| yes | `docs/images/` | Optional product photos (`BottleNose_FACE.webp`; no `freewili-og.png` yet) |
| yes | `firmware/bottlenose/` | Shared C6 image embedded in Bottlenose mains |
| yes | `tests/`, `tools/ogemu/`, `tools/fw.py`, host helpers (`chirpmail`, `fsbak`, `hostdeck`, `rigglass`, …) | No-board tests and CDC helpers |
| yes | `AGENTS.md`, `LICENSE`, `NOTICE`, `THIRD-PARTY-NOTICES.md`, CMake presets | Contract, license, build |
| no | `AGENTS.local.md` | Maintainer hardware notes (already gitignored) |
| no | `.cursor/`, `tools/wyze_cam/` | Bench camera, LAN IPs, bridge secrets |
| no | `T-Echo/`, `esp8266_deauther/`, `onewili/`, `fwOGAppExplorer/` | Other trees / downloads |
| no | root Cyberpunk MP3 (`i-got-a-real-bad-feeling-about-this-cyberpunk-2077.mp3`) | Ship-clip **source**; henry tracks it; public omits |
| no | `docs/superpowers/`, extra `docs/hardware/` PDFs | Internal history / third-party Lattice note |
| no | `build*/`, `*.uf2`, `*.elf`, `*.exe` | Build outputs |

`192.168.4.1` SoftAP URLs and generated ferry passwords in Bottlenose apps
are product behavior, not bench secrets. USB VID/PID `093C:2054`/`2055` are
the published identity.

## Status

The foundation, the display serial bootloader and its update path are
complete, and most peripheral drivers are implemented and have been exercised
on hardware.

| Area                                                  | State                                                               |
| ----------------------------------------------------- | ------------------------------------------------------------------- |
| Clocks, link, diagnostics, bootloader, display update | Working, verified on hardware                                       |
| LCD (ST7789), buttons, WS2812 LEDs, ship mode         | Working, verified on hardware                                       |
| CC1101 radios ×2, IR TX/RX, PDM mic, I2S speaker      | Working, verified on hardware                                       |
| LIS3DH accelerometer, MCP7940 RTC, PCAL6416 expander  | Working, verified on hardware                                       |
| iCE40 FPGA loader                                     | Bitstream loads and `CDONE` asserts; no gateware function exercised |
| LVGL 9 port (display + keypad)                        | Builds and links against LVGL v9.2.2; **not yet run on a board**     |
| Breakout I/O direction control                        | Code complete and host-tested, but **never run on a board**          |

Known gaps: crash-safety under power loss mid-update, ship-mode current draw
(nothing on the board can measure it), the FPGA's gateware behaviour, the
breakout I/O direction sequencer, and the LVGL port.

## License

**Dual licensed** — see [LICENSE](./LICENSE) and [NOTICE](./NOTICE).

| What you're building                      | License                                                                                    |
| ----------------------------------------- | ------------------------------------------------------------------------------------------ |
| Firmware targeting **FreeWili hardware**  | FreeWili Hardware License — MIT text plus a field-of-use condition. Free, no registration. |
| Firmware targeting **any other hardware** | Commercial license required; contact [Intrepid Control Systems](https://intrepidcs.com/).  |

The free option is **not** the MIT License and **not** open source: it is MIT's
text with a hardware field-of-use condition added. Please don't describe it as
either.

Bundled third-party components (FatFs, the wilibsp-derived CIC filter, the Pico
SDK board header, and the LVGL-derived `lv_conf.h`) keep their own more
permissive licenses and are **not** subject to the hardware condition — see
[THIRD-PARTY-NOTICES.md](./THIRD-PARTY-NOTICES.md). LVGL itself is not
distributed here.
