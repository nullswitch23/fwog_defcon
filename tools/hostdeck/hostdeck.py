#!/usr/bin/env python3
"""HostDeck helper — type this OG's MACRO chords into *this* Windows session.

HostDeck v001 prints chords on the display CPU's USB CDC. TinyUSB HID is
deliberately not enumerated (replacing the Pico CDC descriptors would
trade away 1200-baud BOOTSEL, the display CPU's only remote recovery).
This helper is the authorized remainder: you install it on the PC the
cable is in, it finds that display CDC the same way fw.py does, and it
SendInput's the chord into the focused window.

    python tools/hostdeck/hostdeck.py

Never open the port at 1200 baud. That is BOOTSEL, not a console.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import shlex
import subprocess
import sys
import time

_HERE = pathlib.Path(__file__).resolve().parent
_TOOLS = _HERE.parent
for _p in (_TOOLS, _HERE):
    if str(_p) not in sys.path:
        sys.path.insert(0, str(_p))

import fw  # noqa: E402

# pico_stdio_usb ignores the UART baud; pyserial still wants a number.
# 1200 is the SDK's "reboot into BOOTSEL" sentinel — never use it here.
CDC_BAUD = 115200
BOOTSEL_BAUD = 1200

HOSTDECK_TOKENS = ("hostdeck", "kithome")

# Virtual-key codes (Winuser.h). Letters and digits are their ASCII values.
VK_BACK = 0x08
VK_TAB = 0x09
VK_RETURN = 0x0D
VK_SHIFT = 0x10
VK_CONTROL = 0x11
VK_MENU = 0x12
VK_ESCAPE = 0x1B
VK_SPACE = 0x20
VK_DELETE = 0x2E
VK_LWIN = 0x5B
VK_RWIN = 0x5C
VK_F1 = 0x70
VK_VOLUME_MUTE = 0xAD
VK_VOLUME_DOWN = 0xAE
VK_VOLUME_UP = 0xAF
VK_MEDIA_NEXT_TRACK = 0xB0
VK_MEDIA_PREV_TRACK = 0xB1
VK_MEDIA_STOP = 0xB2
VK_MEDIA_PLAY_PAUSE = 0xB3

MODIFIERS = {
    "ctrl": VK_CONTROL,
    "control": VK_CONTROL,
    "alt": VK_MENU,
    "shift": VK_SHIFT,
    "win": VK_LWIN,
    "windows": VK_LWIN,
    "gui": VK_LWIN,
    "meta": VK_LWIN,
}

# Names HostDeck already prints, plus a small consumer set the LCD may grow.
NAMED_KEYS = {
    "return": VK_RETURN,
    "enter": VK_RETURN,
    "tab": VK_TAB,
    "escape": VK_ESCAPE,
    "esc": VK_ESCAPE,
    "space": VK_SPACE,
    "backspace": VK_BACK,
    "delete": VK_DELETE,
    "del": VK_DELETE,
    "play": VK_MEDIA_PLAY_PAUSE,
    "pause": VK_MEDIA_PLAY_PAUSE,
    "play/pause": VK_MEDIA_PLAY_PAUSE,
    "playpause": VK_MEDIA_PLAY_PAUSE,
    "mute": VK_VOLUME_MUTE,
    "volup": VK_VOLUME_UP,
    "vol+": VK_VOLUME_UP,
    "vol_up": VK_VOLUME_UP,
    "volume up": VK_VOLUME_UP,
    "volumeup": VK_VOLUME_UP,
    "voldown": VK_VOLUME_DOWN,
    "vol-": VK_VOLUME_DOWN,
    "vol_down": VK_VOLUME_DOWN,
    "volume down": VK_VOLUME_DOWN,
    "volumedown": VK_VOLUME_DOWN,
    "next": VK_MEDIA_NEXT_TRACK,
    "prev": VK_MEDIA_PREV_TRACK,
    "previous": VK_MEDIA_PREV_TRACK,
    "stop": VK_MEDIA_STOP,
}

# firmware v001:
#   MACRO page=0 slot=2 Copy -> Ctrl+C (HID not enumerated in v001)
# optional later token:
#   HOSTDECK chord=Ctrl+C page=0 slot=2 label=Copy
_MACRO_RE = re.compile(
    r"^MACRO\s+page=(?P<page>\d+)\s+slot=(?P<slot>\d+)\s+"
    r"(?P<label>.+?)\s+->\s+(?P<chord>.+?)\s*$",
    re.IGNORECASE,
)
_HOSTDECK_CHORD_RE = re.compile(
    r"^HOSTDECK\b(?:\s+chord=(?P<chord>.+?))?(?:\s+page=(?P<page>\d+))?"
    r"(?:\s+slot=(?P<slot>\d+))?(?:\s+label=(?P<label>\S+))?\s*$",
    re.IGNORECASE,
)
_HOSTDECK_PAYLOAD_RE = re.compile(
    r"^HOSTDECK\b\s+payload=(?P<payload>\d+)(?:\s+label=(?P<label>\S+))?\s*$",
    re.IGNORECASE,
)
_HOSTDECK_EXEC_RE = re.compile(
    r"^HOSTDECK\b\s+exec=(?P<chord>.+?)\s*$",
    re.IGNORECASE,
)
_TRAILING_PAREN = re.compile(r"\s+\([^()]*\)\s*$")

PAYLOAD_MAP_FILENAMES = ("hostdeck_payloads.json",)


class HostDeckError(RuntimeError):
    """Could not identify this machine's HostDeck display CDC."""


class ChordError(ValueError):
    """A MACRO line named a chord this helper does not know."""


class PayloadError(ValueError):
    """A payload descriptor is missing or malformed."""


def _norm(s):
    return " ".join((s or "").strip().lower().split())


def lookup_key(token):
    """A single non-modifier key to a VK, or None."""
    n = _norm(token)
    if n.startswith("consumer "):
        n = n[len("consumer "):]
    if n in NAMED_KEYS:
        return NAMED_KEYS[n]
    m = re.fullmatch(r"f(\d{1,2})", n)
    if m:
        fn = int(m.group(1))
        if 1 <= fn <= 24:
            return VK_F1 + (fn - 1)
    if len(n) == 1:
        ch = n.upper()
        if "A" <= ch <= "Z" or "0" <= ch <= "9":
            return ord(ch)
    return None


def parse_chord(chord):
    """Return (modifier_vks, key_vk) for a HostDeck chord string."""
    raw = (chord or "").strip()
    if not raw:
        raise ChordError("empty chord")
    # A consumer name may contain spaces but not '+'. Modifier chords use '+'.
    if "+" in raw:
        parts = [p.strip() for p in raw.split("+") if p.strip()]
        mods, keys = [], []
        for part in parts:
            n = _norm(part)
            if n in MODIFIERS:
                vk = MODIFIERS[n]
                if vk not in mods:
                    mods.append(vk)
            else:
                keys.append(part)
        if len(keys) != 1:
            raise ChordError(f"cannot parse chord {chord!r}")
        vk = lookup_key(keys[0])
        if vk is None:
            raise ChordError(f"unknown key {keys[0]!r} in chord {chord!r}")
        return tuple(mods), vk
    vk = lookup_key(raw)
    if vk is None:
        raise ChordError(f"unknown chord {chord!r}")
    return (), vk


def _clean_line(line):
    text = (line or "").strip().lstrip("\x00")
    if text.startswith("\r"):
        text = text.lstrip("\r")
    return text


def parse_macro_line(line):
    """Parse one DIAG line into a dict, or None if it is not a macro."""
    text = _clean_line(line)
    m = _HOSTDECK_EXEC_RE.match(text)
    if m:
        chord = _TRAILING_PAREN.sub("", m.group("chord")).strip()
        return {"kind": "HOSTDECK_EXEC", "chord": chord}
    m = _HOSTDECK_PAYLOAD_RE.match(text)
    if m:
        return {
            "kind": "HOSTDECK_PAYLOAD",
            "payload_index": int(m.group("payload")),
            "label": m.group("label"),
        }
    m = _HOSTDECK_CHORD_RE.match(text)
    if m and (m.group("chord") or "").strip():
        chord = _TRAILING_PAREN.sub("", m.group("chord")).strip()
        return {
            "kind": "HOSTDECK",
            "page": int(m.group("page")) if m.group("page") else None,
            "slot": int(m.group("slot")) if m.group("slot") else None,
            "label": m.group("label"),
            "chord": chord,
        }
    m = _MACRO_RE.match(text)
    if not m:
        return None
    chord = _TRAILING_PAREN.sub("", m.group("chord")).strip()
    return {
        "kind": "MACRO",
        "page": int(m.group("page")),
        "slot": int(m.group("slot")),
        "label": m.group("label").strip(),
        "chord": chord,
    }


def _payload_map_paths():
    seen = set()
    for name in PAYLOAD_MAP_FILENAMES:
        for base in (_HERE, pathlib.Path.cwd()):
            path = (base / name).resolve()
            if path not in seen:
                seen.add(path)
                yield path
    home_path = (pathlib.Path.home() / ".hostdeck" / "payloads.json").resolve()
    if home_path not in seen:
        yield home_path


def load_payload_map(path=None):
    """Load a page/slot or label -> payload descriptor map."""
    if path is not None:
        paths = [pathlib.Path(path)]
    else:
        paths = list(_payload_map_paths())
    for candidate in paths:
        if not candidate.is_file():
            continue
        with candidate.open(encoding="utf-8") as fh:
            data = json.load(fh)
        if not isinstance(data, dict):
            raise PayloadError(f"{candidate} must be a JSON object")
        print(f"payload map: {candidate}", flush=True)
        return data
    return {}


def load_payload_list(path):
    """Load an indexed payload list from --payloads."""
    candidate = pathlib.Path(path)
    if not candidate.is_file():
        raise PayloadError(f"payload list not found: {candidate}")
    with candidate.open(encoding="utf-8") as fh:
        data = json.load(fh)
    if not isinstance(data, list):
        raise PayloadError(f"{candidate} must be a JSON array")
    print(f"payload list: {candidate} ({len(data)} entries)", flush=True)
    return data


def lookup_payload_map(payload_map, page, slot, label):
    """Return a payload descriptor keyed by page/slot or label, or None."""
    if not payload_map:
        return None
    if page is not None and slot is not None:
        for key in (f"page:{page}:{slot}", f"{page}:{slot}"):
            if key in payload_map:
                return payload_map[key]
    if label:
        if label in payload_map:
            return payload_map[label]
        nlabel = _norm(label)
        for key, payload in payload_map.items():
            if _norm(key) == nlabel:
                return payload
    return None


def _command_argv(cmd):
    if isinstance(cmd, list):
        argv = [str(part) for part in cmd]
    elif isinstance(cmd, str):
        argv = shlex.split(cmd, posix=False)
    else:
        raise PayloadError("command payload requires cmd as a string or list")
    if not argv:
        raise PayloadError("command payload has an empty cmd")
    return argv


def _read_file_payload(path, send_newline):
    candidate = pathlib.Path(path)
    if not candidate.is_file():
        raise PayloadError(f"file payload path not found: {candidate}")
    text = candidate.read_text(encoding="utf-8")
    if send_newline and (not text or not text.endswith("\n")):
        text += "\n"
    return text


def describe_payload(payload):
    """One-line summary for logging and dry-run."""
    ptype = (payload or {}).get("type")
    if ptype == "macro":
        return f"macro -> {payload.get('chord', '?')}"
    if ptype == "string":
        text = payload.get("text", "")
        preview = text if len(text) <= 40 else text[:37] + "..."
        return f'string -> {preview!r}'
    if ptype == "file":
        path = payload.get("path", "?")
        extra = " +newline" if payload.get("send_newline") else ""
        return f"file -> {path}{extra}"
    if ptype == "command":
        cmd = payload.get("cmd", "?")
        if isinstance(cmd, list):
            cmd = " ".join(str(part) for part in cmd)
        return f"command -> {cmd}"
    return f"unknown payload type {ptype!r}"


def execute_payload(payload, *, dry_run, send_chord, send_text, run_command):
    """Run one payload descriptor."""
    if not isinstance(payload, dict):
        raise PayloadError("payload must be a JSON object")
    ptype = payload.get("type")
    if ptype == "macro":
        chord = payload.get("chord")
        if not chord:
            raise PayloadError("macro payload requires chord")
        desc = describe_payload(payload)
        if dry_run:
            print(f"dry-run: {desc}", flush=True)
            return desc
        mods, key = parse_chord(chord)
        send_chord(mods, key)
        print(f"sent: {desc}", flush=True)
        return desc
    if ptype == "string":
        text = payload.get("text", "")
        desc = describe_payload(payload)
        if dry_run:
            print(f"dry-run: {desc}", flush=True)
            return desc
        send_text(text)
        print(f"sent: {desc}", flush=True)
        return desc
    if ptype == "file":
        path = payload.get("path")
        if not path:
            raise PayloadError("file payload requires path")
        text = _read_file_payload(path, bool(payload.get("send_newline")))
        desc = describe_payload(payload)
        if dry_run:
            print(f"dry-run: {desc}", flush=True)
            return desc
        send_text(text)
        print(f"sent: {desc}", flush=True)
        return desc
    if ptype == "command":
        argv = _command_argv(payload.get("cmd"))
        desc = describe_payload(payload)
        if dry_run:
            print(f"dry-run: {desc}", flush=True)
            return desc
        run_command(argv)
        print(f"sent: {desc}", flush=True)
        return desc
    raise PayloadError(f"unknown payload type {ptype!r}")


def is_hostdeck_product(product):
    """True when the USB product string names HostDeck or KitHome.

    fwog_usb_product() yields 'FWOG display hostdeck 001' or
    'FWOG display kithome 001'. Matching a token (not the full string)
    keeps a VERSION bump from breaking us. KitHome's HostDeck tile prints
    the same MACRO lines. The trailing-space prefix 'FWOG display ' is
    enforced by fw._pick_cpu_port already; this is the extra 'is it THIS
    app' check so we never type keystrokes from bench_display or lcd_display.
    """
    p = (product or "").lower()
    return any(t in p for t in HOSTDECK_TOKENS)


def _fmt_port(p):
    pid = f"{p.pid:04X}" if p.pid is not None else "?"
    vid = f"{p.vid:04X}" if p.vid is not None else "?"
    return f"{p.device} ({vid}:{pid} {p.product or '?'})"


def pick_hostdeck_display(ports):
    """The HostDeck display CpuPort.

    Identification is fw.py's: PID 093C:2055 first, then the
    'FWOG display ' product prefix. Main (2054) is never a candidate.
    After that we still require 'hostdeck' or 'kithome' in the product
    string — a display CPU running another app is not this PC's macro pad.
    """
    device = fw._pick_cpu_port(ports, "display")
    port = next(p for p in ports if p.device == device)
    # PID wins when it is present: a main CPU (2054) whose product string
    # was renamed to look like the display must still not receive keystrokes.
    if port.pid == fw.CPU_PID["main"]:
        raise HostDeckError(
            f"{_fmt_port(port)} has the MAIN PID 2054. HostDeck buttons "
            "live on the display CPU (PID 2055). Refusing."
        )
    if not is_hostdeck_product(port.product):
        raise HostDeckError(
            f"{_fmt_port(port)} is the display CPU, but it is not HostDeck "
            "or KitHome (USB product must contain 'hostdeck' or 'kithome'). "
            "Refusing to send keys."
        )
    return port


def resolve_hostdeck_port(ports, port_arg=None):
    """Pick the display CDC, or honour --port after the same checks."""
    if port_arg:
        matches = [p for p in ports
                   if (p.device or "").upper() == port_arg.upper()]
        if not matches:
            raise HostDeckError(
                f"port {port_arg} is not in the serial listing; "
                "pass a COM name this machine actually has"
            )
        p = matches[0]
        if p.vid == fw.ICS_USB_VID and p.pid == fw.CPU_PID["main"]:
            raise HostDeckError(
                f"{_fmt_port(p)} is the MAIN CPU (PID 2054). HostDeck "
                "buttons live on the display CPU (PID 2055). Refusing."
            )
        if (p.product or "").lower().startswith(fw.CPU_PRODUCT_PREFIX["main"].lower()):
            raise HostDeckError(
                f"{_fmt_port(p)} reports a main-CPU product string. "
                "HostDeck is the display CDC. Refusing."
            )
        if not is_hostdeck_product(p.product):
            raise HostDeckError(
                f"{_fmt_port(p)} is not HostDeck or KitHome (product must "
                "contain 'hostdeck' or 'kithome'). Refusing to send keys."
            )
        return p
    return pick_hostdeck_display(ports)


def check_bootsel_volumes(find_volume=None):
    """Refuse when two RPI-RP2 volumes are mounted (the CPUs are twins)."""
    find_volume = find_volume or fw.find_rpi_rp2
    return find_volume()


def open_cdc(device, baud=CDC_BAUD):
    """Open the display CDC. Baud is ignored by USB; 1200 is still fatal."""
    if baud == BOOTSEL_BAUD:
        raise HostDeckError(
            "refusing to open at 1200 baud: that reboots the RP2040 into "
            "BOOTSEL, which is recovery, not the HostDeck console"
        )
    try:
        import serial
    except ImportError as e:
        raise HostDeckError(
            "pyserial is required:  python -m pip install pyserial"
        ) from e
    ser = serial.Serial()
    ser.port = device
    ser.baudrate = baud
    ser.timeout = 0.2
    ser.dsrdtr = False
    ser.open()
    # pico_stdio_usb stays mute until DTR is asserted (same as tools/bench.py).
    ser.dtr = True
    time.sleep(0.2)
    ser.reset_input_buffer()
    return ser


def default_send_chord(modifiers, key):
    if sys.platform != "win32":
        raise HostDeckError(
            "SendInput is Windows-only; use --dry-run to parse without typing"
        )
    from sendinput_win import send_vk_chord
    send_vk_chord(modifiers, key)


def default_send_text(text):
    if sys.platform != "win32":
        raise HostDeckError(
            "SendInput is Windows-only; use --dry-run to parse without typing"
        )
    from sendinput_win import send_unicode_text
    send_unicode_text(text)


def default_run_command(argv):
    subprocess.Popen(argv, shell=False)


def _set_title(text):
    if sys.platform != "win32":
        return
    try:
        ctypes_mod = __import__("ctypes")
        ctypes_mod.windll.kernel32.SetConsoleTitleW(text)
    except (AttributeError, OSError):
        pass


def _describe_event(ev, payload=None):
    page = ev.get("page")
    slot = ev.get("slot")
    label = ev.get("label") or "?"
    chord = ev.get("chord")
    loc = ""
    if page is not None and slot is not None:
        loc = f"page={page} slot={slot} "
    if ev.get("kind") == "HOSTDECK_PAYLOAD":
        idx = ev.get("payload_index")
        suffix = f" payload={idx}"
        if label and label != "?":
            suffix += f" label={label}"
        return f"{suffix.strip()} -> {describe_payload(payload) if payload else f'payload[{idx}]'}"
    if payload is not None:
        return f"{loc}{label} -> {describe_payload(payload)}"
    return f"{loc}{label} -> {chord}"


def _resolve_event_payload(ev, payload_map, payload_list):
    kind = ev.get("kind")
    if kind == "HOSTDECK_PAYLOAD":
        idx = ev.get("payload_index")
        if payload_list is None:
            raise PayloadError("HOSTDECK payload= line received but no --payloads list loaded")
        if idx < 0 or idx >= len(payload_list):
            raise PayloadError(f"payload index {idx} out of range (0..{len(payload_list) - 1})")
        return payload_list[idx]
    if kind in ("MACRO", "HOSTDECK"):
        return lookup_payload_map(payload_map, ev.get("page"), ev.get("slot"), ev.get("label"))
    return None


def _execute_chord_event(ev, *, dry_run, send_chord):
    last = _describe_event(ev)
    try:
        mods, key = parse_chord(ev["chord"])
    except ChordError as e:
        print(f"skip: {last}  ({e})", flush=True)
        return None
    if dry_run:
        print(f"dry-run: {last}", flush=True)
        return last
    send_chord(mods, key)
    print(f"sent: {last}", flush=True)
    return last


def run_loop(ser, *, dry_run, product, payload_map, payload_list,
             send_chord, send_text, run_command):
    last = "(none yet)"
    _set_title(f"HostDeck — {product} — last: {last}")
    buf = ""
    while True:
        data = ser.read(4096)
        if not data:
            continue
        buf += data.decode("utf-8", "replace")
        while "\n" in buf:
            line, _, buf = buf.partition("\n")
            ev = parse_macro_line(line.replace("\r", ""))
            if ev is None:
                continue
            try:
                payload = _resolve_event_payload(ev, payload_map, payload_list)
            except PayloadError as e:
                last = _describe_event(ev)
                print(f"skip: {last}  ({e})", flush=True)
                _set_title(f"HostDeck — {product} — last: {last}")
                continue
            if payload is not None:
                last = _describe_event(ev, payload)
                _set_title(f"HostDeck — {product} — last: {last}")
                try:
                    execute_payload(
                        payload,
                        dry_run=dry_run,
                        send_chord=send_chord,
                        send_text=send_text,
                        run_command=run_command,
                    )
                except (PayloadError, ChordError, HostDeckError, OSError) as e:
                    print(f"skip: {last}  ({e})", flush=True)
                continue
            if ev.get("kind") in ("HOSTDECK_EXEC", "HOSTDECK", "MACRO") and ev.get("chord"):
                last = _execute_chord_event(ev, dry_run=dry_run, send_chord=send_chord)
                if last is not None:
                    _set_title(f"HostDeck — {product} — last: {last}")
                continue
            last = _describe_event(ev)
            print(f"skip: {last}  (no chord or payload)", flush=True)
            _set_title(f"HostDeck — {product} — last: {last}")


def main(argv=None):
    p = argparse.ArgumentParser(
        prog="hostdeck",
        description="Type HostDeck MACRO chords into this Windows session. "
                    "Not an injector: only this PC's display CDC, never 1200 baud.",
    )
    p.add_argument("--port", help="COM port of the HostDeck display CDC "
                                  "(still refused if it is main or not HostDeck)")
    p.add_argument("--dry-run", action="store_true",
                   help="parse and print chords without SendInput")
    p.add_argument("--list", action="store_true",
                   help="print identified FreeWili CDC ports and exit")
    p.add_argument("--payloads",
                   help="JSON array of payload descriptors for HOSTDECK payload=<n>")
    args = p.parse_args(argv)

    try:
        check_bootsel_volumes()
    except fw.MultipleRp2VolumesError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    try:
        ports = fw._cpu_ports()
    except ImportError:
        print("hostdeck needs pyserial:  python -m pip install pyserial",
              file=sys.stderr)
        return 1
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    if args.list:
        print("FreeWili CDC ports (PID 2055 = display, 2054 = main):")
        any_fw = False
        for port in ports:
            if port.vid not in fw.FWOG_USB_VIDS:
                continue
            any_fw = True
            mark = ""
            if is_hostdeck_product(port.product):
                mark = "  <- HostDeck / KitHome display"
            elif port.pid == fw.CPU_PID["main"]:
                mark = "  (main — ignored)"
            elif port.pid == fw.CPU_PID["display"]:
                mark = "  (display, not HostDeck)"
            print(f"  {_fmt_port(port)}{mark}")
        if not any_fw:
            print("  (none)")
        return 0

    try:
        chosen = resolve_hostdeck_port(ports, args.port)
    except (HostDeckError, fw.CpuPortError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    try:
        payload_map = load_payload_map()
        payload_list = load_payload_list(args.payloads) if args.payloads else None
    except PayloadError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    product = chosen.product or "FWOG display hostdeck"
    print("HostDeck helper — this PC only (not an injector, not TinyUSB HID)")
    print(f"connected: {_fmt_port(chosen)}")
    print(f"open {chosen.device} at {CDC_BAUD} (USB CDC; never {BOOTSEL_BAUD})")
    print("focus the window that should receive keys. Ctrl-C to quit.")
    if args.dry_run:
        print("dry-run: payloads and chords will be printed, not executed.")

    try:
        ser = open_cdc(chosen.device, CDC_BAUD)
    except HostDeckError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"error: opening {chosen.device}: {e}", file=sys.stderr)
        return 1

    try:
        run_loop(
            ser,
            dry_run=args.dry_run,
            product=product,
            payload_map=payload_map,
            payload_list=payload_list,
            send_chord=default_send_chord,
            send_text=default_send_text,
            run_command=default_run_command,
        )
    except KeyboardInterrupt:
        print("\nbye", flush=True)
        return 0
    finally:
        try:
            ser.close()
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
