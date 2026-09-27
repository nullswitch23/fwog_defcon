# fsbak — pull and restore GlassBak FatFs dumps

The OG is a USB device. GlassBak (or the KitHome GlassBak tile) walks
main FatFs and prints a hex archive on **main** CDC. This helper rebuilds
the files on the host, groups them by the app that owns each path, and
can **Push** them back so firmware opens the same `/OPTIC.BIN` (etc.).

Never open the port at **1200 baud** (BOOTSEL).

```
python tools/fsbak/fsbak.py --gui
python tools/fsbak/fsbak.py
python tools/fsbak/fsbak.py --out backups/today
python tools/fsbak/fsbak.py --push --out backups/today
python tools/fsbak/fsbak.py --push --app OpticClick
python tools/fsbak/fsbak.py --list
python tools/fsbak/fsbak.py --port COM12
```

Pull: press **Green** on the OG after the helper is listening.

**Worklist:** auto-convert TalkClip `/talkclip/*.RAW` to a playable WAV
on pull. Not done yet — dumps stay RAW until that lands.

Push: `PUT /path size` plus hex; `gb_fs_poll_restore()` writes those
paths (parents created). Original apps then see the data where they left
it.

Requires `pyserial` (and tkinter for `--gui`). Identifies main the same
way `fw.py` does (PID `093C:2054`, then `FWOG main `).
