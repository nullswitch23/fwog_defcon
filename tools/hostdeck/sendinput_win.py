"""Win32 SendInput keyboard/media-key injector for HostDeck.

Only types into *this* Windows session. There is no remote target and no
HID device on the wire — the helper is the keyboard, sitting next to fw.py.
"""
from __future__ import annotations

import ctypes
from ctypes import wintypes

INPUT_KEYBOARD = 1
KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_EXTENDEDKEY = 0x0001
KEYEVENTF_UNICODE = 0x0004

# Media / Win keys need the extended flag or some apps ignore them.
_EXTENDED = {
    0x5B,  # VK_LWIN
    0x5C,  # VK_RWIN
    0xAD,  # VK_VOLUME_MUTE
    0xAE,  # VK_VOLUME_DOWN
    0xAF,  # VK_VOLUME_UP
    0xB0,  # VK_MEDIA_NEXT_TRACK
    0xB1,  # VK_MEDIA_PREV_TRACK
    0xB2,  # VK_MEDIA_STOP
    0xB3,  # VK_MEDIA_PLAY_PAUSE
}


class _MOUSEINPUT(ctypes.Structure):
    _fields_ = (
        ("dx", wintypes.LONG),
        ("dy", wintypes.LONG),
        ("mouseData", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ctypes.c_size_t),
    )


class _KEYBDINPUT(ctypes.Structure):
    _fields_ = (
        ("wVk", wintypes.WORD),
        ("wScan", wintypes.WORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ctypes.c_size_t),
    )


class _HARDWAREINPUT(ctypes.Structure):
    _fields_ = (
        ("uMsg", wintypes.DWORD),
        ("wParamL", wintypes.WORD),
        ("wParamH", wintypes.WORD),
    )


class _INPUTUNION(ctypes.Union):
    _fields_ = (("mi", _MOUSEINPUT), ("ki", _KEYBDINPUT), ("hi", _HARDWAREINPUT))


class _INPUT(ctypes.Structure):
    _fields_ = (("type", wintypes.DWORD), ("union", _INPUTUNION))


_SendInput = ctypes.windll.user32.SendInput
_SendInput.argtypes = (wintypes.UINT, ctypes.POINTER(_INPUT), ctypes.c_int)
_SendInput.restype = wintypes.UINT


def _event(vk, up):
    flags = KEYEVENTF_KEYUP if up else 0
    if vk in _EXTENDED:
        flags |= KEYEVENTF_EXTENDEDKEY
    inp = _INPUT()
    inp.type = INPUT_KEYBOARD
    inp.union.ki = _KEYBDINPUT(vk, 0, flags, 0, 0)
    return inp


def send_vk_chord(modifiers, key):
    """Press modifiers, tap `key`, release modifiers. Foreground window only."""
    if not key:
        raise ValueError("no virtual-key to send")
    events = [_event(vk, False) for vk in modifiers]
    events.append(_event(key, False))
    events.append(_event(key, True))
    events.extend(_event(vk, True) for vk in reversed(modifiers))
    arr = (_INPUT * len(events))(*events)
    sent = _SendInput(len(events), arr, ctypes.sizeof(_INPUT))
    if sent != len(events):
        raise OSError(f"SendInput sent {sent}/{len(events)} events")


def _unicode_event(codepoint, up):
    flags = KEYEVENTF_KEYUP if up else 0
    flags |= KEYEVENTF_UNICODE
    inp = _INPUT()
    inp.type = INPUT_KEYBOARD
    inp.union.ki = _KEYBDINPUT(0, codepoint, flags, 0, 0)
    return inp


def send_unicode_text(text):
    """Type `text` character by character via SendInput unicode events."""
    if text is None:
        return
    events = []
    for ch in text:
        cp = ord(ch)
        events.append(_unicode_event(cp, False))
        events.append(_unicode_event(cp, True))
    if not events:
        return
    arr = (_INPUT * len(events))(*events)
    sent = _SendInput(len(events), arr, ctypes.sizeof(_INPUT))
    if sent != len(events):
        raise OSError(f"SendInput sent {sent}/{len(events)} unicode events")
