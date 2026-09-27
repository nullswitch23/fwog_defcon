# FunDesk

**Status:** firmware in `apps/fundesk/` (VERSION **004**). Last: 2026-09-27.

**004:** VoltPet waits 3 s after tile enter before `PULL` (boot-relative hatch
was firing on a warm landing). PitchFork keeps PDM claimed — `pdm_mic_stop`
then re-init hung gray-hold home.

**Members:** InertialTrail, InertialTrailRF, VoltPet, PitchFork. PinDesk-scale
four-row landing. Gray tap up, red tap down, green opens. Yellow / green /
gray hold ~0.7 s home. Red 6 s ships.

Flash **`fundesk_main`** when you are ready. Do not UF2-flash the display half.

| Tile | What you get |
|---|---|
| **Trail** | Pedometer / origin map; `/trail` CSV. **BLUE hold T9** (gray hold is home). Radios idle. |
| **TrailRF** | Dual parked RSSI + steps; `/trailrf`. Owns both CC1101s. **BLUE hold T9**. |
| **VoltPet** | Relic pet; `voltpet.bin`; radio0 listen, radio1 idle. **BLUE hold name**. |
| **PitchFork** | PDM tuner. **BLUE hold AUTO**; gray tap still cycles note. Mic stopped on leave. |

Never TrailRF and VoltPet at once. Home idles both radios.
