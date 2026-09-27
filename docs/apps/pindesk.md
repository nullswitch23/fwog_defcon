# PinDesk

**Status:** firmware in `apps/pindesk/` (VERSION **003**). Last: 2026-09-27.

Breakout landing: **HeaderKit**, **QwiicBench**, **TtyGlass**, **Retia**.
Four scale-2 rows (PinDesk geometry). Gray tap up, red tap down, green
opens. Yellow / green / gray hold ~0.7 s returns home. Tile enter and home
full-clear the panel. Red 6 s ships.

Flash **`pindesk_main`**. Do not UF2-flash the display half. Retia’s CDC
terminal is on **main USB** while that tile is open.

| Tile | What you get |
|---|---|
| **HeaderKit** | Hi-Z I2C scan + pin-map pages (same chrome as stand-alone) |
| **QwiicBench** | Scan / plot; BLUE tap nicks unknowns (GRAY hold is home) |
| **TtyGlass** | Header UART RX; BLUE hold cycles RX pad (YELLOW hold is home) |
| **Retia** | LCD mode/pinout from main; type `mode uart` etc. on main CDC |

Main muxes one I2C master, then UART1/PIO RX, then the Retia breakout
terminal. Not on an Orca landing (UART1 is Bottlenose).
