# OrcaLobby

**Status:** firmware in `apps/orcalobby/` (VERSION **004**). Last: 2026-09-27.

**004** waits 3 s after PingHalo tile enter before `LIB_GET` (same CTS /
watchdog splash loop as stand-alone PingHalo 023).

Bottlenose landing: **LanFerry**, **PingHalo**, **BleDeck**, **AirMaraud**.
No BattleBridge. One C6 mode at a time. UART1 is the C6 link (not TtyGlass).

Gray tap up, red tap down, green opens. Yellow / green / gray hold ~0.7 s
returns home. **BLUE hold** ROM-flashes `fwog_c6.bin` (yellow hold is home).
Tile enter and home full-clear the panel. Red 6 s ships.

Flash **`orcalobby_main`**. Do not UF2-flash the display half.

| Tile | What you get |
|---|---|
| **LanFerry** | SoftAP file ferry. BLUE tap QR. GRAY tap wipe (was gray hold). |
| **PingHalo** | BLE hunt / labels. RED hold T9. BLUE hold library while scanning. |
| **BleDeck** | BLE HID remote. |
| **AirMaraud** | 2.4 GHz lab scan. RED hold sweep. |

Home idles the C6 (AP / scan / HID / WIFIPROOF stopped).
