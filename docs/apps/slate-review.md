# Slate review — 2026-09-12

A read of the current app catalog against [Flipper Lab](https://lab.flipper.net/apps),
the official Flipper Zero app store. The question is: **which ideas on that
store are a good fit for the silicon this board actually has**, under the
keep-test in [retired.md](retired.md). Authorized professional pentest use
passes Law; a missing PHY still fails Silicon. It is not a plan to clone a
Flipper. [flipper.md](flipper.md) already explains why a FAP cannot load
here and what is worth lifting (data, not Furi).

Method: the store front-end is a JavaScript app, so the scan went through its
catalog API instead. That is 442 published applications across eleven
categories — Games 121, GPIO 119, Tools 94, NFC 30, Media 21, USB 13, Sub-GHz
13, RFID 11, Infrared 8, Bluetooth 7, iButton 5. The shape of that
distribution is itself the finding: the Flipper store is mostly **games and
I2C/UART sensor breakouts**, not radio tools. Both of those are areas where
the OG is stronger than a Flipper, and both are thin on our slate.

This scan did not implement new Flipper ports. The OG slate has moved since
the first draft: [KitHome](kithome.md) 002 is firmware (HostDeck +
MicScope + BandScope + GlassBak), and retired.md now treats authorized
pentest as passing (1). Unscoped use against strangers still stays out.
Further combo landings are recs only — [combo-images.md](combo-images.md).

Fit labels used below:

| Fit | Means |
|---|---|
| analog already here | The job is done by a shipping OG app |
| **port** | Silicon we have; worth a page |
| assessment-class | In-scope for a written engagement; not a consumer gadget |
| skip | Possible, not worth a page |
| skip on silicon / needs new hardware / blocked | Missing PHY or adapter |
| retire-class | Still a bad idea on this board even with a letter (secrets on readable flash, mechanical lock bypass) |

## 1. What the slate already covers well

Seventeen documented ideas plus **[KitHome](kithome.md)** (the landing that
loads HostDeck, MicScope, BandScope, and GlassBak in one UF2). Mapped to the job a
user is trying to do rather than to the chip they use:

| Job | App | State |
|---|---|---|
| See what is transmitting near me | [BandScope](bandscope.md) | 002, sweep + peak-hold + top-5 hunt |
| Record and replay a remote I own | [FobReplay](fobreplay.md) | 003, SINGLE and QUEUE (no jam-leg yet) |
| Understand an unknown OOK burst | [ISMburst](ismburst.md) | 003, on-device pulse analysis + owned-gadget replay |
| Hear my car's tyre sensors | [TireEar](tireear.md) | 003, ASK/FSK burst count; OEM PSI is per-vendor, not a generic frame |
| Find a transmitter by walking toward it | [TwinFox](twinfox.md) | 002, dual-radio RSSI + opt-in beacon |
| Survey a building's RF while I walk it | [InertialTrailRF](inertialtrailrf.md) | 002, parked RSSI against a step clock |
| Control a TV I own | [OpticClick](opticclick.md) | 001, NEC capture and replay |
| Poke at a breakout board | [HeaderKit](headerkit.md) | 002, I2C scan |
| Read a serial console without a laptop | [TtyGlass](ttyglass.md) | 002, RX-only UART1 terminal |
| Look at a sound | [MicScope](micscope.md) | 002, spectrogram and VU |
| Keep a spoken note | [TalkClip](talkclip.md) | 003, VAD + CDC dump |
| Count my steps | [InertialTrail](inertialtrail.md) | 004, pedometer, clock, drift demo |
| Hear a historical tone | [ToneBox](tonebox.md) | 002, DTMF, ACTS, MF/SF museum |
| Drive this PC from five buttons | [HostDeck](hostdeck.md) | 001 + host helper (also talks to KitHome) |
| Four tools, one UF2 | [KitHome](kithome.md) | 002, landing (GlassBak added) |
| Move a file between two of my machines | [LanFerry](lanferry.md) | 006, Bottlenose AP |
| Find a BLE tracker | [PingHalo](pinghalo.md) | 003, Bottlenose passive scan |
| Have something to look after | [VoltPet](voltpet.md) | 002, FatFs save across ship mode |

Authorized assessment tools that **silicon can run but this tree has not
built yet** are in section 3 (OccupyISM, HidStick, FobJam, AirMaraud). They
are not consumer catalog items.

Read across that list and the coverage is genuinely good in three places. The
**sub-GHz receive** story is more complete than most Flipper apps manage,
because BandScope, ISMburst, TireEar, TwinFox and InertialTrailRF each take a
different question to the same radio rather than being five skins on one
capture loop. The **breakout and instrument** story is started and coherent.
And the **product discipline** is the real asset: [retired.md](retired.md)
means the catalog can say yes to an authorized assessment tool and no to
a missing PHY without re-arguing it, which is exactly what the Flipper
store cannot do.

Three places are thin. There is **no game** other than VoltPet, in a store
where games are the largest category. There is **no I2C sensor app**, in a
store where sensor breakouts are the second largest category. And there is
**no file manager**, which three separate pages are quietly waiting on.

## 2. Flipper Lab scan

Named apps from the store, with a verdict. "Analog already here" means the
job is done; "port" means the idea transfers to silicon we have; "assessment-
class" means a written engagement, not a toy; "skip" means legal-and-possible
but not worth a page.

### Sub-GHz (13 apps)

| Flipper app | Fit | Note |
|---|---|---|
| **ProtoView** | analog already here | ISMburst does on-device pulse analysis; ProtoView is the same idea with a nicer waveform view |
| **TPMS Bridge** | analog already here | TireEar. Ownership is required; there is still no single TPMS RF frame to copy |
| **Genie Door Recorder** | assessment-class | FobReplay covers unused-code replay today. A per-brand rolling-code recorder is in-scope for authorized access-control RF tests; see retired.md |
| **Flipper Share** | **port** | Text between two boards over CC1101 packet mode. See ChirpMail below |
| **RS41 Tracker** | **port** | Vaisala radiosonde at 400–406 MHz, inside the CC1101's 387–464 band. See SondeChase below |
| **POCSAG Pager** | assessment-class | Decode of an in-scope pager network is a radio assessment. Unscoped intercept of strangers' pages stays out; that is why ISMburst's page still excludes POCSAG as a default |
| **Sub-GHz Scheduler** | skip unless engagement needs it | Unattended timed retransmission. FobReplay today is a human pressing yellow; a scheduler is a different assessment tool, not a (1) failure |
| **FM TX** | skip on silicon | Audio transmit on an ISM packet radio is out-of-spec emission. Authorized RF denial is the CC1101 in packet/ASK, not a pretend FM broadcast |

### Infrared (8 apps)

| Flipper app | Fit | Note |
|---|---|---|
| **IR Scope** | **port** | Raw edge-timing analyzer. This is the measurement OpticClick's page says was never made. See BeamScope |
| **Intervalometer**, **Pause Timer**, **Zeitraffer** | port after raw IR | Camera shutter codes are mostly not 32-bit NEC, so they need raw TX first |
| **Midea AC Remote**, **AC Fujitsu General Remote**, **IR Builder** | port after raw IR | AC frames are long and non-NEC; same prerequisite |
| **Flipper Flame RNG** | skip | A dice roller filed under Infrared |

IR is the clearest single blocker in the tree: everything above waits on one
thing, which is a raw timing engine next to the existing NEC decoder.

### GPIO (119 apps — the biggest opportunity)

| Flipper app | Fit | Note |
|---|---|---|
| **i2c Tools**, **GPIO Explorer**, **401/DigiLab** | analog already here | HeaderKit, with SPI/UART/GPIO pages still to come |
| **UART Terminal** | analog already here | TtyGlass |
| **BME680**, **Lightmeter**, **[MH-Z19] CO2 sensor**, **UV Meter**, **Temp sensors reader**, **CO2 Logger** | **port** | All the same shape: one I2C driver plus a graph. See QwiicBench |
| **24cxxprog**, **AVR Flasher** | **port** | Breakout SPI/I2C memory reader. See ChipSip |
| **SD SPI** | needs new hardware | A microSD adapter, already scoped in [../hardware/microsd.md](../hardware/microsd.md). Cheap, and it is what gives TalkClip more than eight minutes |
| **[NMEA] GPS**, **u-blox GPS** | needs new hardware | A UART module. This is what would turn InertialTrailRF from a step survey into a real one |
| **Flashlight**, **LED Blinker** | analog already here | Seven WS2812 on the display CPU |
| **ServoTester**, **Signal Generator**, **7-Segment Output** | port (HeaderKit pages) | Small, and they belong on HeaderKit rather than as apps |
| **[J305] Geiger Counter**, **RadSens**, **PMSx003 Airmon**, **MQ-3 Alcometer**, **LD2410 Human Detector**, **RCWL0516 Radar Scan** | needs new hardware | Once the module exists these are pulse counting or a UART parse, so they fall out of QwiicBench |
| **[NRF24] Channel Scan**, **W5500 Ethernet**, **FM Radio**, **[YRM100] UHF RFID**, **UHF Expansion** | needs new hardware | The UHF RFID one needs a backscatter reader front end, not just 915 MHz tuning |
| **[ESP32] WiFi Marauder**, **Marauder GUI**, **FlipWiFi** | blocked on this C6; scan remainder | Deauth needs ESP8266-class `wifi_send_pkt_freedom`; C6 `esp_wifi_80211_tx` does not list deauth. See [AirMaraud](airmaraud.md). WifiHear (scan-only) is retired |
| **[GPIO] Wiegand Reader**, **[GPIO] Reader** | assessment-class | Pulse-counting a client's access-control reader on the breakout is ordinary physical pentest. Unscoped badge harvesting stays out |
| **[GPIO] Sentry Safe** | retire-class | Mechanical safe bypass. Not a radio this board has |
| **Flipagotchi**, **Matagotchi** | analog already here | VoltPet |
| **GAME BOY Cartridge MALVEKE**, **Pokemon Trade Tool**, **Xbox POST Code Reader** | needs new hardware | Charming, and each needs its own adapter |

### Games (121 apps)

| Flipper app | Fit | Note |
|---|---|---|
| **Air Labyrinth**, **Gurpil** | **port, and better here** | Tilt games. Flipper needs an add-on accelerometer; the LIS3DH is soldered to our display CPU. See TiltMaze |
| **Laser Tag** | **port, and better here** | Two boards duelling over IR. We have both a blaster and a receiver, plus LEDs and a speaker. See BeamTag |
| **Morse Trainer** | **port** | See DitDash |
| **Arkanoid**, **Snake 2.0**, **2048**, **Sokoban**, **Tic Tac Toe**, **T-Rex runner**, **Flappy Bird**, **Pong**, **Game of Life**, **Mandelbrot Set** | port | Straightforward on 320×240 colour. Pick two or three, not twenty |
| **DOOM**, **Wolfenduino** | skip | Possible on an RP2040 and a large time sink for a demo nobody keeps |
| **Dice D&D**, **Lifecounter**, **Slot Machine**, **Fortune Spinner**, **Umpire Indicator** | skip | Novelty utilities |

### Media (21 apps)

| Flipper app | Fit | Note |
|---|---|---|
| **Tone Generator**, **Metronome**, **BPM Tapper** | analog already here | ToneBox's engine covers all three |
| **Music Player** (RTTTL), **Ocarina**, **FlipPiano**, **Handpan Chords**, **Guitar Chords** | port | Fine at 8 kHz; a ringtone player is the one flipper.md already anticipated |
| **WAV Player**, **MP3 Player** | port with a caveat | 8 kHz mono, and there is no file browser yet to pick a file with |
| **Text to SAM** | port | Speech synthesis lands close to this speaker's native rate. Charming, low priority |
| **Flizzer Tracker**, **DVD Screensaver** | skip | |

Nothing in this category exploits the microphone, because a Flipper does not
have one. That is the gap worth taking.

### Tools (94 apps)

| Flipper app | Fit | Note |
|---|---|---|
| **DTMF Dolphin** | analog already here | ToneBox took its data tables and left the seize sequencer |
| **Flipp Pomodoro**, **Pomodoro Timer**, **Count Down Timer** | analog already here | WiLiDoro exists in the published catalog |
| **Authenticator** | port with a caveat | The MCP7940 makes TOTP possible. No secure element makes it a lab-account tool. See TimeKey |
| **QRCode** | port | Trivial on this panel, and the published Orca Field Notes app already renders QR |
| **HEX Editor**, **Hex Viewer**, **FlipNote**, **FlipLibrary** | **port** | All of these presuppose a file browser we do not have. See DiskGlass |
| **Morse Flipper**, **San Morse** | **port** | See DitDash |
| **Quac!** | port | One button fires one saved signal. A good future mode for FobReplay and OpticClick rather than its own app |
| **Barcode App**, **Flipper Wedge** | skip | A backlit colour LCD is a poor barcode; the wedge half is HostDeck's helper |
| **CAN Tools** | needs new hardware | Flagged separately below — this one is strategically interesting |
| **DCF77 Transmitter**, **DCF77 Clock Spoof** | blocked | 77.5 kHz is far below the CC1101's floor, so it needs a coil on a GPIO. Authorization does not grow that PHY |
| **ZeroFIDO**, **FlipBIP Crypto Wallet**, **Password Manager**, **FlipPass**, **FlipCrypt** | retire-class | Real credentials and real money on a part whose flash reads out with picotool. A pentest letter does not make that a safe vault |
| **Lishi**, **Combo Cracker** | retire-class | Mechanical lock bypass. Not this board's job |
| **Calculator**, **Counter**, **Knit Counter**, **Multi Tally**, **Multi Converter**, **Moon Phases**, **Caesar Cipher**, **ROT13**, **Enigma**, **Brainfuck**, **fmatrix**, **Theme Manager** | skip | Micro-utilities. Cheap to write, and none of them is why someone bought this board |

### USB (13 apps)

Every one of these is **blocked by policy, not by silicon**, and it is worth
being precise about why. Both RP2040s are USB devices and TinyUSB is already
in the tree, so a composite CDC+HID descriptor is ordinary firmware. But it
would mean replacing `stdio_usb_descriptors.c`, which is the descriptor set
1200-baud BOOTSEL rides on — and that is the display CPU's only remote
recovery, because it has no BOOTSEL button. HostDeck's page makes this call
already and it is the right one.

| Flipper app | Fit | Note |
|---|---|---|
| **USB Remote**, **Better Mouse**, **Mouse Jiggler**, **Click Recorder**, **Air Mouse**, **BMI Air Mouse** | blocked by policy | HostDeck's host helper is the answer. An accel-driven pointer page in that helper would be *Air Mouse* without a HID descriptor |
| **PC Monitor USB**, **PC monitor** | **port** | Host pushes telemetry, board displays it. The helper and the CDC link both already exist. See RigGlass |
| **Mass Storage**, **Portal Of Flipper** | blocked | USB host. [StickPeek](stickpeek.md) scoped this already |
| **CCID** | blocked | Smartcard reader role |
| **BarCode ScannerE** | skip | |

### Bluetooth (7 apps)

| Flipper app | Fit | Note |
|---|---|---|
| **FindMy Flipper** | needs Bottlenose, and leave it | Broadcasting a findable tag is buildable on the C6 and is also how a tracker gets made. PingHalo says presence-only on purpose |
| **BT Trigger**, **Anki Remote**, **VGM Game Remote**, **Android KB Bridge** | needs Bottlenose | Five buttons as a BLE HID remote. See BleDeck |
| **Claude Buddy**, **FlipGemini** | needs Bottlenose | Works with a key and a network. Novelty |

### NFC, RFID, iButton (46 apps)

Blocked, all of them, and this is the largest single block of the store we
simply cannot serve. **Metroflip**, **PicoPass**, **Seader**, **NFC Maker**,
**NFC URL**, **Sonicare Head ID**, **EM4100 Key generator**, **T5577 Raw
Writer**, **H10301 Writer**, **FDX-B Maker**, **iButton Converter** — none of
these has a PHY on this board. There is no 13.56 MHz front end, no 125 kHz
analog front end, and no 1-Wire interface. **MFKey**, **RFID Fuzzer**,
**Mifare Fuzzer** and **iButton Fuzzer** remain a credential-cloning
problem even with the silicon; if an NFC Orca ever appears, an NDEF
lister for tags in scope for the engagement is the remainder
[retired.md](retired.md) already names, and a payment-card reader is
still the wrong product.

## 3. Good fit, not built yet

**2026-09-27:** DiskGlass, PitchFork, QwiicBench, RigGlass, and ChirpMail
in this table **shipped**. BeamScope, BeamTag, ChipSip, SondeChase,
TiltMaze, DitDash, and TimeKey are **retired** (too similar to other
apps or not native) — they are not in the shipped catalog. Historical
notes below.

Twelve ideas that use silicon we have. Names are pithy placeholders in the
[README.md](README.md) style; rename before anything ships.

| Placeholder | One-liner | Fit | Flipper inspiration |
|---|---|---|---|
| **DiskGlass** | Browse and pull main's 8 MB FatFs from the front panel | native | HEX Editor, FlipLibrary |
| **BeamScope** | Raw IR edge-timing analyzer; names the PHY it heard | native | IR Scope |
| **BeamTag** | Two boards duel over IR; LED bar is health, speaker is hits | native | Laser Tag |
| **PitchFork** | Chromatic tuner on the PDM mic; cents needle, LED flat/sharp | native | *(none — Flipper has no mic)* |
| **QwiicBench** | I2C sensor bench on the breakout: read, name, graph, log | native | BME680, Lightmeter, MH-Z19 |
| **ChipSip** | 24Cxx / 25-series memory reader-writer on breakout SPI and I2C | native | 24cxxprog, AVR Flasher |
| **RigGlass** | Host pushes CPU/RAM/net over CDC; LCD and LED bar are the gauges | native | PC Monitor |
| **ChirpMail** | Short text between two OGs over CC1101 packet mode | native | Flipper Share |
| **SondeChase** | Vaisala RS41 radiosonde receiver at 400–406 MHz, with bearing | native | RS41 Tracker |
| **TiltMaze** | Accelerometer arcade: marble maze, tilt-brick, tilt theremin | native | Air Labyrinth, Gurpil |
| **DitDash** | Morse trainer: speaker sidetone, LED keyer, mic decodes you back | native | Morse Flipper, San Morse |
| **TimeKey** | TOTP codes off the MCP7940 RTC | native, with a caveat | Authenticator |

Details worth writing down now, before anyone starts:

**DiskGlass** is infrastructure disguised as an app. TalkClip, InertialTrail
and InertialTrailRF each say, on their own page, that the file is a later
slice. Main owns the only volume; display owns the screen. One browser plus a
`list`/`get` protocol on main's CDC finishes all three and makes VoltPet's
save inspectable. It is also the prerequisite for a WAV player, a hex viewer,
and a `.sub` importer, so it buys more than it costs.

**BeamScope** is the smaller half of the IR problem and probably belongs as
OpticClick v002 rather than a new folder. The BSP engine is 32-bit NEC at
38 kHz and OpticClick's page is candid that RX on this port "was never a
product claim." A raw edge capture on `pio1` that reports timings and a
best-guess PHY name — NEC, RC5, SIRC, Kaseikyo — turns that from an
unverified claim into a measurement, and it unlocks every AC remote and
intervalometer port above. Do not let it grow into a claim that we decode all
four protocols.

**BeamTag** is the demo. Two boards, IR blaster on GPIO 9 against the
receiver on GPIO 16, WS2812 as a health bar, I2S for hit sounds. It is the
most immediately showable thing on this list and it exercises the IR receive
path hard, which is the point. Build BeamScope first or you will be debugging
two unknowns at once.

**PitchFork** is the strongest strategic idea here, because it is the one a
Flipper structurally cannot do: there is no microphone on that device. We
have an MP34DT06J at 8 kHz and MicScope has already built the spectrogram.
A tuner is that FFT with a note name, a cents needle, and the LED bar as a
flat/sharp meter. 8 kHz is plenty — a guitar's top string is 330 Hz and its
useful harmonics sit well under 4 kHz.

**QwiicBench** has the best ratio of product to code on the list. A hundred
and nineteen Flipper GPIO apps are, structurally, one I2C driver and a graph.
HeaderKit already scans the bus and finds the address; this is the half that
reads it. A small table of two or three parts on the desk — a BME280, a
BH1750 — and a plot is a complete app, and each new part after that is a
dozen lines. It is also the natural home for the Geiger and particulate
sensors once a module shows up.

**RigGlass** costs almost nothing because `tools/hostdeck/` already owns an
identified CDC link to the authorized PC. Teach the helper to push
utilisation and temperatures on a timer, and the panel becomes a desk gauge.
No descriptor change, no new silicon, and it makes the helper look less like
a single-purpose script.

**ChirpMail** (v001 in `apps/chirpmail/`) is the app that justifies owning two boards.
cleanest packet-mode exercise for the CC1101 driver, which everything else
uses in async-serial OOK mode. Keep it inside ISM at the existing ~10 dBm,
which is TwinFox's rule. Text entry should use the arrow-pad on-screen
keyboard AGENTS.md already recommends for ported FreeWili 2 apps.

**SondeChase** is the most ambitious and the most rewarding. Radiosondes are
launched twice daily worldwide, the frames are public, chasing them is a real
hobby, and 400–406 MHz sits inside the CC1101's 387–464 MHz band. It is also
the first app with a genuine reason for two radios that is not a fox-hunt:
radio 0 decodes the frame while radio 1 does TwinFox-style RSSI bearing. Be
honest about the lift — GFSK at 4800 baud plus Reed–Solomon plus a GPS frame
parse on main is the heaviest item on this page.

**TiltMaze** and **DitDash** are the cheap, high-charm entries, and both beat
the Flipper version on hardware rather than on effort. Tilt games need an
add-on there and are soldered down here; a Morse trainer wants a speaker, a
key, an LED and — for decoding your own sending back — a microphone, and we
have all four.

**TimeKey** needs its caveat on the page or it should not ship. The RP2040 has
no secure element and main's flash reads out with picotool, so a stolen board
is a stolen seed. That is fine for a lab account and not fine for anything
that matters. Write the sentence, or leave the idea off the slate.

## 4. Needs Bottlenose, versus blocked

**Needs Bottlenose** (ESP32-C6 on the 20-pin header). Note that all of these
sit behind the same unfinished work: the wiliOG C6 image in
`firmware/bottlenose/` has to be flashed and answering `BN HELLO` before
LanFerry or PingHalo do anything, so a third Bottlenose app is cheaper than
the first two were.

| Idea | Flipper analog | Note |
|---|---|---|
| **BleDeck** — five buttons as a BLE HID remote to a phone or PC | BT Trigger, Anki Remote, VGM Game Remote | Needs BLE HID on the C6. Complements HostDeck: one is wired to this PC, one is wireless to anything |
| Wi-Fi and BLE column for InertialTrailRF | — | That page already says this needs its own page and its own legal argument. It is right |
| LLM chat on the panel | Claude Buddy, FlipGemini | Works with a network and a key. Novelty, low priority |
| FindMy broadcast | FindMy Flipper | Buildable, and PingHalo is presence-only on purpose. Leave it |

**Needs new hardware, but cheaply** — worth distinguishing from blocked,
because these are adapters rather than Orcas:

- **microSD on breakout SPI.** Already scoped in
  [../hardware/microsd.md](../hardware/microsd.md). Uncompressed speech fills
  main's 8 MB volume in about eight minutes, so this is TalkClip's ceiling.
  Do not hang the card's chip-select on header pin 1; that pin is the FPGA's.
- **UART GPS.** A module and an NMEA parser. This is the difference between
  InertialTrailRF being a step survey and being a real one, and TtyGlass
  already reads UART1.
- **CAN transceiver.** Flagged on its own because it is strategically odd
  that it is missing: the USB VID `093C` belongs to Intrepid Control Systems,
  a vehicle-network instrumentation company. A breakout CAN sniffer on the
  OG is the most on-brand add-on this board could possibly have, and Flipper
  ships **CAN Tools** as a third-party app. It is not firmware-only — it
  needs a transceiver and a controller — but it is the one gap on this list
  where the product story is already written.

**Blocked on stock hardware:**

| Blocked | Why |
|---|---|
| USB host — Mass Storage, Portal Of Flipper | Both RP2040s are devices. [StickPeek](stickpeek.md) has the full analysis, including why PIO-USB on the header fights the level shifters |
| USB HID — Mouse Jiggler, Better Mouse, USB Remote, CCID, Air Mouse | Silicon can, policy will not: a composite descriptor replaces the set 1200-baud BOOTSEL rides on |
| NFC, 125 kHz RFID, iButton — 46 apps | No 13.56 MHz front end, no 125 kHz AFE, no 1-Wire |
| DCF77 — DCF77 Transmitter | 77.5 kHz is three orders of magnitude below the CC1101's floor |
| UHF RFID — YRM100, UHF Expansion | Needs a backscatter reader front end, not 915 MHz tuning |
| nRF24, W5500 Ethernet, FM Radio, Geiger, PMSx003 | Modules |

The iCE40 bitstream is frozen (AGENTS.md rule 4) and `io_buffer` is not going
to grow a PHY for any of the above. Nothing on this page proposes touching
it.

## 5. Do not build (wrong silicon, or not this board's job)

The keep-test is in [retired.md](retired.md): authorized professional
pentest use, then PHY, then remainder. RF denial, authorized HID, and
rolling-code RF occupation are **not** on this skip list. These still are:

| Flipper app | Why not |
|---|---|
| **FM TX** | Wideband audio transmit on an ISM packet radio. Out-of-spec emission. Authorized RF denial is packet/ASK occupancy, not a pretend FM broadcast |
| **POCSAG Pager** | Third-party message content. ISMburst's page already excludes POCSAG, and InertialTrailRF's rule — energy and frequency are the survey, payloads are not — is the same rule |
| **Lishi**, **Combo Cracker**, **[GPIO] Sentry Safe** | Lock and safe bypass. Not a radio this board has, and not an RF assessment |
| **MFKey**, **RFID Fuzzer**, **Mifare Fuzzer**, **iButton Fuzzer**, **T5577 Raw Writer** | No 13.56 / 125 kHz / 1-Wire PHY. An NFC Orca would still not be a payment-card reader |
| **ZeroFIDO**, **FlipBIP Crypto Wallet**, **Password Manager** | Real credentials and real money on a part with no secure element and readable flash |

Marauder-class Wi-Fi tests, a Sub-GHz scheduler, and a per-brand rolling-code
recorder are assessment tools when (1) is a yes; they are not automatic
skips. FobReplay QUEUE is the firmware that exists today, not the only
remainder worth writing.

One pattern is worth naming. Flipper's store is permissive because it is a
store: it hosts the app and lets the user decide. This catalog is a product
slate, so an idea here carries the project's name. "The chip can do it" is
still not a reason to ship a toy. It is a reason to consider the pentest
tool when the engagement is real.

## 6. Gaps in apps that already exist

Observations from reading the current tree, not part of the Flipper scan.

**ISMburst's Prologue and Nexus decoders will produce false positives.** The
page describes Prologue as "36-bit PPM, type nibble 0x9/0x5" and Nexus as
"36-bit PPM, const 0xF." Those are weak discriminants on their own — plenty
of unrelated 36-bit PPM traffic will satisfy a single nibble, and rtl_433
itself gates these families on more than that. Either add the family's
checksum as a condition for claiming the name, or demote an unchecked match
to "36-bit PPM, Prologue-shaped" in the UI. A confident wrong temperature is
worse than an honest hex dump, and the rest of that page is careful about
exactly this distinction.

**HostDeck's helper is Windows-only.** `tools/hostdeck/sendinput_win.py` is
the typing backend, so a macOS or Linux operator gets the LCD labels and no
keystrokes. The page is right that HID is off the table, which makes the
helper the whole product, so its portability is the app's portability. This
is also the file RigGlass would extend, so it is worth deciding the shape
before adding a second feature to it.

**The catalog has no local-URL path.** `apps/apps_example.json` advertises
three apps — `ogvegas`, `wilidoro`, `orca-catalog` — of which only `ogvegas`
has a folder in this tree, and none of the seventeen documented slate apps
appear in it at all. Every `uf2.url` points at
`https://docs.freewili.com/og-apps/uf2/...`, and `build_catalog.py` hard-fails
a `baseUrl` that is not `https://`, so there is no way to point App Explorer
at a local build tree. `fwOGAppExplorer/publish/README.md` already suggests
`--base-url https://localhost:8443/og-apps/` as the workaround, which means
the need is known; accepting a `file://` or plain-`http://localhost` origin
behind an explicit flag would make the whole flash path testable offline.
Separately, the example catalog is stale enough to mislead — it should either
be regenerated from this tree or labelled as a format sample.

**`tools/ogemu` exists and changes the economics of this entire page.** The
display-CPU UI emulator is further along than "starting": v001 already runs a
real `apps/<name>/display/main.c` against a RAM ST7789, a real 6×8 font,
injected buttons on the same bits as `fwog_buttons_poll()`, virtual time, the
real 6 s ship machine, and a PNG plus text-log dump, with a JSONL script
format and a CTest smoke. No Pico SDK, no board.

That matters for section 3 because most of those twelve ideas are a UI plus
one driver, and the UI half is now writable and reviewable without hardware.
**DiskGlass**, **RigGlass**, **TiltMaze**, **DitDash**, **TimeKey** and the
panel half of **QwiicBench** and **PitchFork** are all mostly emulator work.
Read against ogemu's own stub table, the honest split is:

| Idea | How much is emulator-reachable |
|---|---|
| DiskGlass | Browser chrome and navigation, yes; FatFs and the link are v002 |
| RigGlass | Nearly all of it — the data arrives as text, which the script can inject |
| TiltMaze, DitDash | Layout and game logic, yes; LIS3DH and I2S need stubs |
| TimeKey | All of the UI; the RTC read needs a stub |
| PitchFork, QwiicBench | Panel only. The FFT is host-testable in `tests/` instead |
| BeamScope, BeamTag, ChirpMail, SondeChase | Panel only. These are radio and IR apps, so the interesting half is on hardware either way |

Two gaps worth noting. Only `hostdeck` and `template` are wired in
`tools/ogemu/CMakeLists.txt`, so adding the current slate's display halves is
a one-line-per-app job that has not been done — `voltpet` is even sketched as
a comment. And WS2812 is a colour buffer whose `process()` is a no-op, which
means the LED bar cannot be asserted on; several apps use it as their primary
indicator, so a "what colour is LED n" query would be worth the small effort.
The v002 list in its README already has the right priorities, and the
dual-CPU link loopback is the one that unblocks the most of section 3.

**OpticClick is NEC-only and its RX is unproven**, by its own admission. Both
BeamScope and BeamTag depend on that path, so whichever is built first is
also the verification. Sequence them deliberately.

**TwinFox does not reuse BandScope's sweep engine.** InertialTrailRF's page
explicitly says main should reuse it; TwinFox v001 is a single parked
frequency on both radios. Either it should share the engine or its page
should say why a fox-hunt wants a fixed dwell — which is a defensible answer,
just not a written one.

**`docs/apps/README.md` does not distinguish slate from shipped BSP apps.**
The tree contains `bench`, `ogvegas`, `lcd`, `bl`, `smoke`, `template`,
`lvgl` and `cpuprobe` with no page, which is reasonable for demos and
infrastructure, but a reader arriving at that table cannot tell what is
actually on a board. A one-line note would fix it.

## 7. Recommended next three

If the slate continues, this order:

1. **DiskGlass.** Three existing pages promise a file and none delivers one.
   It is the lowest-risk item on the list, it converts TalkClip,
   InertialTrail and InertialTrailRF from demos into tools, and its browser
   chrome can be built in `ogemu` before anything touches a board. Nothing
   else on this page unblocks as much.

2. **BeamScope, then BeamTag.** The IR receive path is the one BSP driver the
   docs admit is unverified, and it gates six Flipper ports. BeamScope is the
   measurement and is small; BeamTag is the payoff and is the best
   demonstration this board has. Doing the scope first means the game debugs
   one unknown instead of two.

3. **PitchFork.** The microphone is the thing a Flipper does not have, and
   this is the app that makes that obvious to someone holding both devices.
   MicScope already did the hard part. A tuner with a cents needle and a
   flat/sharp LED bar is a finished product in a short sitting, and it makes
   the mic look like a feature rather than a sensor.

Honourable mention: **QwiicBench** is arguably the highest value per line of
code of anything here, given that it addresses the second largest category in
the Flipper store with one driver and a graph. It is fourth only because it
needs a sensor on the desk and the three above need nothing that is not
already soldered to the board.

Cheaper than any of them, and not an app: **wire the existing slate's display
halves into `tools/ogemu/CMakeLists.txt`.** That is one line each, it makes
every app on the slate reviewable from a PNG, and it is the difference
between the emulator being a HostDeck harness and being the way UI work gets
done here.
