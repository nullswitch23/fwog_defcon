# HostDeck helper

Windows helper that types HostDeck button chords into **this** PC.

TinyUSB HID is not required. HostDeck v001 already prints `MACRO` lines on
the display CPU's USB CDC. Replacing Pico's CDC descriptors to add HID
would trade away **1200-baud BOOTSEL**, which is the display CPU's only
remote recovery. This helper keeps that path intact.

It is not an injector. It only opens a CDC port this machine identified as
HostDeck's **display** CPU (PID `093C:2055`, product `FWOG display hostdeck …`
or `FWOG display kithome …`).
Main (`093C:2054`) is never a candidate.

## Run

```
python -m pip install pyserial
python tools/hostdeck/hostdeck.py
```

Leave the console running. Focus the window that should receive keys, then
press the OG's colour buttons.

| Flag | What it does |
|---|---|
| `--dry-run` | Parse and print chords/payloads, do not SendInput or run commands |
| `--list` | Show FreeWili CDC ports and which one is HostDeck |
| `--port COM12` | Use that port after the same HostDeck / not-main checks |
| `--payloads FILE` | JSON **array** of payload descriptors for `HOSTDECK payload=<n>` lines |

`Ctrl-C` quits. The console title shows the connected product string and the
last macro.

## How it finds the port

Same order as `tools/fw.py`:

1. USB PID **093C:2055** (display). **093C:2054** is main and is ignored.
2. Else the product-string prefix `FWOG display ` (older images still on
   `2E8A:000A`).
3. Then require `hostdeck` or `kithome` in the product string, so
   `bench_display` / `lcd_display` never type keys. KitHome's HostDeck
   tile prints the same `MACRO` lines.
   `lcd_display` never type keystrokes.

Windows hides the USB product string from pyserial; the helper reuses
`fw._cpu_ports()` / `fw._pick_cpu_port()`, which fill it from SetupAPI.

Two `RPI-RP2` volumes at once cannot be told apart. The helper refuses,
same as `fw flash`.

## Never 1200 baud

Opening a running RP2040 CDC at **1200 baud** reboots that CPU into BOOTSEL.
The helper opens at 115200 (USB CDC ignores the UART baud) and **refuses**
1200 if asked.

## Chord map

Firmware v001 prints:

```
MACRO page=0 slot=2 Copy -> Ctrl+C (HID not enumerated in v001)
```

| Page | Button | Label | Chord typed |
|---|---|---|---|
| 1 | Green | Play | Play/Pause (VK_MEDIA_PLAY_PAUSE) |
| 1 | Yellow tap | Mute | Mute |
| 1 | Blue tap | Copy | Ctrl+C |
| 1 | Gray | Paste | Ctrl+V |
| 1 | Red tap | Enter | Return |
| 2 | Green | Cut | Ctrl+X |
| 2 | Yellow tap | Undo | Ctrl+Z |
| 2 | Blue tap | Save | Ctrl+S |
| 2 | Gray | Tab | Tab |
| 2 | Red tap | Esc | Escape |

Yellow hold 700 ms latches page 1; blue hold 700 ms latches page 2; red hold
6 s still ships the board off.

Modifiers `Ctrl` / `Alt` / `Shift` / `Win` compose with a key. Consumer
names `Play`, `Mute`, `VolUp`, `VolDown` are accepted with or without a
`Consumer ` prefix. A later `HOSTDECK chord=Ctrl+C` line is also parsed;
firmware v001 does not emit it.

## Standalone payloads (no firmware change)

Without reflashing, you can override what a button does by placing a payload
map next to the helper or in your home directory. The helper searches, in
order:

1. `tools/hostdeck/hostdeck_payloads.json`
2. `./hostdeck_payloads.json` (current working directory)
3. `~/.hostdeck/payloads.json`

The file is a JSON **object** mapping a button key to a payload descriptor.
Keys may be `page:<n>:<slot>`, `<page>:<slot>`, or a button label (e.g.
`Copy`).

```json
{
  "0:2": {"type": "macro", "chord": "Ctrl+C"},
  "0:1": {"type": "string", "text": "hello world"},
  "0:3": {"type": "file", "path": "C:/scripts/secret.txt", "send_newline": false},
  "Paste": {"type": "command", "cmd": "notepad"}
}
```

When a `MACRO` line from the firmware matches a key, the helper runs the
payload instead of typing the firmware's chord.

### Payload types

| `type` | Fields | Action |
|---|---|---|
| `macro` | `chord` | Same SendInput chord path as `MACRO` |
| `string` | `text` | Type the text character by character (Unicode via SendInput) |
| `file` | `path`, optional `send_newline` | Read the file as UTF-8 and type it; append `\n` when `send_newline` is true |
| `command` | `cmd` (string or argv list) | `subprocess.Popen` with no shell, non-blocking |

`--dry-run` prints what would run for every payload type without typing,
reading files, or spawning processes.

## Protocol lines (firmware may emit later)

| Line | Meaning |
|---|---|
| `MACRO page=<p> slot=<s> <label> -> <chord>` | v001 default; overridden by payload map when matched |
| `HOSTDECK chord=<chord> page=<p> slot=<s> label=<label>` | Explicit chord; also overridden by payload map |
| `HOSTDECK exec=<chord>` | Execute `<chord>` immediately (no page/slot lookup) |
| `HOSTDECK payload=<n> label=<label>` | Run entry `<n>` from the `--payloads` JSON array |

Example indexed list (`payloads.json` passed to `--payloads`):

```json
[
  {"type": "string", "text": "git status"},
  {"type": "macro", "chord": "Return"},
  {"type": "command", "cmd": ["cmd", "/c", "start", "https://example.com"]}
]
```

Then `HOSTDECK payload=0` types `git status`, `HOSTDECK payload=1` presses
Enter, and so on. An optional `label=` is logged only.

## Example

Copy the example and point the helper at it:

    cp tools/hostdeck/hostdeck_payloads.example.json payloads.json
    python tools/hostdeck/hostdeck.py --payloads payloads.json

Index 0 is what the gray button (page 1, slot 3) fires by default on firmware v002.
