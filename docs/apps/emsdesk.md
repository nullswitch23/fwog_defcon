# EmsDesk

**Status:** firmware in `apps/emsdesk/` (VERSION **004**). Last: 2026-09-27.

**004:** BandScope tile now calls `colors_init()` on enter (chrome was
drawing RGB 0 on black — looked like the tile never loaded).

Seven tiles, **two pages** (PinDesk scale, `^`/`v` corners). One CC1101
job live. Gray tap up, red tap down, green opens. Yellow / green / gray
hold ~0.7 s home. Member holds that used those buttons are **BLUE**.
Red 6 s ships.

Flash **`emsdesk_main`**. Do not UF2-flash the display half. Landing SEL
is **0x5F** (0x70 is DiskGlass `DG_MSG_CMD`).

| SEL | Tile | Notes |
|---|---|---|
| 1 | BandScope | CS0 sweep. Own-mark is BLUE hold. |
| 2 | TwinFox | Both radios. Beacon TX is BLUE hold. |
| 3 | TireEar | TPMS capture. OEM is BLUE hold. |
| 4 | ISMburst | ASK/2-FSK. Shares 20 KB capture with FobReplay (not both hot). |
| 5 | FobReplay | Capture / queue / PREDICT. Same shared arena. |
| 6 | ChirpMail | 433.92 mailbox. T9 done is BLUE hold. |
| 7 | OpticClick | IR / FatFs. Radios idle. |

Home idles both radios.
