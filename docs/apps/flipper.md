# Flipper Zero apps on FreeWili OG

Flipper FAPs do **not** run here. What ports is the *idea* (and sometimes a
protocol table), rewritten against this BSP.

Source used: [123fzero/flipper-zero-awesome](https://github.com/123fzero/flipper-zero-awesome)
(the Flipper Zero awesome-list consulted for this slate) and
`freewili1-docs/` (speaker is 8 kHz 16-bit PCM; radios are the two CC1101s;
Orcas add Wi-Fi/BLE/camera). Hardware facts also cite the upstream BSP
[freewili/wiliOGbsp](https://github.com/freewili/wiliOGbsp). Village chrome
cites **DEF CON** (smiley-and-crossbones seal on the boot splash).

## Why a FAP will not load

| Flipper | FreeWili OG |
|---|---|
| STM32WB55, Furi OS, `.fap` plugin | Two RP2040s, no Furi, UF2 per CPU |
| 128×64 mono + d-pad | 320×240 colour + five colour buttons |
| One CC1101 | Two CC1101s on **main** |
| NFC + 125 kHz RFID + iButton | None of those |
| IR TX/RX | IR TX/RX on **display** (RX unproven in this port) |
| Speaker (higher rate) | I2S 8 kHz; Nyquist 4 kHz |
| USB HID / BadUSB in stock FW | CDC today; [HostDeck](hostdeck.md) is the operator-at-the-keyboard remainder; authorized HID is [retired.md](retired.md) |
| Wi-Fi via ESP32 **devboard** | [Bottlenose](../../freewili1-docs/docs/extending-with-orcas/bottlenose-wifi-orca/bottlenose-wifi-orca.md) ESP32-C6 Orca — same class of add-on |

The one project that *does* run Flipper apps on other silicon
([Sor3nt/Flipper-Zero-ESP32-Port](https://github.com/Sor3nt/Flipper-Zero-ESP32-Port))
reimplements Furi + a HAL, then **recompiles** sources. It does not execute
stock ARM `.fap` files. Doing that here would mean porting Furi onto two
RP2040s and lying about NFC. Rewrite the feature instead.

## What to steal from a Flipper app

1. **Protocol tables** — Sub-GHz decoder lists (weather, TPMS, POCSAG), IR
   libraries, CC1101 register presets. Those are data.
2. **File formats** — `.sub` RAW timings are the same shape FobReplay already
   stores as edge durations. A `.sub` importer is plausible later.
3. **Nothing of Furi** — GUI, records, `furi_hal_subghz_*`, scene manager.

## Catalog overlap

| Flipper (awesome list) | OG |
|---|---|
| Sub-GHz Remote / RAW / ProtoView / Spectrum Analyzer | [FobReplay](fobreplay.md), [BandScope](bandscope.md) |
| Weather Station, TPMS Reader, POCSAG | [ISMburst](ismburst.md), [TireEar](tireear.md) — ISMburst is on-device OOK analysis; TireEar is TPMS-shaped listen |
| Infrared (TV-B-Gone, etc.) | [OpticClick](opticclick.md) |
| GPIO / UART / I2C | [HeaderKit](headerkit.md), [TtyGlass](ttyglass.md) |
| DTMF Dolphin | [ToneBox](tonebox.md) — dialer sequencer + coin cadence museum from Dolphin's **data tables**; no KP/ST seize, no Furi audio |
| Music Player / WAV / Tuning Fork | ToneBox / future RTTTL; WAV is 8 kHz on this board (`freewili1-docs`) |
| BadUSB / HID Autofire | [HostDeck](hostdeck.md) for the operator at this PC; authorized HID toward an engagement target is in-scope |
| NFC / RFID / iButton / MagSpoof | Blocked without new silicon |
| Wi-Fi Marauder, Evil Portal | Bottlenose can carry authorized 2.4 GHz assessment once the UART protocol exists; [LanFerry](lanferry.md) / [PingHalo](pinghalo.md) are the current firmware. Unscoped deauth/portals stay out |
| Games, clock, TOTP | Native UI work; TOTP is [VoltPet](voltpet.md)-adjacent, not Flipper source |
| ESP32 camera suite | WILEye Orca in `freewili1-docs`, not this BSP yet |

## Practical rule

If the Flipper app is **a codec plus a tiny UI**, lift the codec. If it is
**Furi glue around a radio we do not have**, skip it. If it is **an
assessment technique this silicon can run**, apply the three-question
test in [retired.md](retired.md): authorized professional pentest use
passes (1); missing PHY still fails (2). Unscoped use against strangers
stays out. ToneBox is the example for *data*: Dolphin's frequency/pulse
**tables** ported; scenes, HAL, and KP→digits→ST automation against a
public telephone network are not.

## How a source port actually works

There is no FAP loader and no Furi HAL on either RP2040. A Flipper app
that is worth bringing over is rewritten, not relinked:

1. Read the **data** (tone tables, IR timings, Sub-GHz decoder lists,
   `.sub` layouts). Those are usually a few structs.
2. Throw away `application.fam`, scenes, `furi_hal_*`, and the GUI.
3. Drive this BSP: `fwog_display_app` / `fwog_main_app`, five colour
   buttons, ST7789, I2S at 8 kHz or the CC1101s on **main**.
4. Keep the product rule from [retired.md](retired.md). A table of
   historical frequencies is an exhibit. An automated seize against a
   public telephone network is not. A portal or occupy-TX against an
   in-scope network, with written authorization, is an assessment tool,
   not an automatic skip.

ToneBox 002 did exactly that with
[litui/dtmf_dolphin](https://github.com/litui/dtmf_dolphin)
`dtmf_dolphin_data.c`. The audio engine stayed `i2s_audio_*`.
