# BattleBridge — fork handoff

This is the standalone handoff from the multiplayer-game exploration fork.
The main development chat should be able to continue from this file without
the chat transcript.

## Product decision

BattleBridge is a four-player browser game inspired by SubSpace and Geometry
Wars:

- four phones/laptops join a WPA2 SoftAP hosted by Bottlenose;
- a compact arena keeps players in contact;
- each player races through a private sequence of randomly placed colored
  gates;
- shooting and destroying other players earns points;
- spawned enemies chase and shoot players;
- death leads to a random safe respawn with brief invulnerability;
- browsers render near 30 FPS while the C6 runs the authoritative game.

No PC is required. Open `http://192.168.4.1/` after joining `FWOG-arena`.

## Where processing belongs

```text
Browser x4                       Bottlenose ESP32-C6
---------                        -------------------
Canvas rendering    <--- state -- authoritative 30 Hz simulation
Interpolation                    player movement validation
Particles/audio     --- input --> projectiles and hit detection
Touch/key/gamepad                 gates, enemy AI, health, score
Cosmetic effects                 random safe respawns

                                      |
                             115200-baud UART1
                                      |
                         Main RP2040 + display RP2040
                         lobby/status/C6 flash only
```

The 115200-baud Bottlenose UART carries about 11 KB/s before framing and
logging overhead. It must not carry 30 complete game snapshots. Physics,
projectiles, collisions, gate crossings and match state therefore remain on
the C6. Each browser offloads the expensive presentation work.

Useful optional RP2040 work is deliberately low-rate:

- generate an enemy/gate director schedule from the shared match seed;
- send enemy strategy targets at 5–10 Hz while C6 still integrates physics;
- log match summaries to main FatFs;
- render a spectator/minimap on the OG LCD.

Those are future measurements, not v001 dependencies. Enemy steering is
cheap compared with Wi-Fi and WebSocket work, so moving it across UART may
cost more than it saves.

## LanFerry findings

BattleBridge reuses the proven topology but is its own app and C6 mode.

Relevant current LanFerry behavior:

- `FWOG-ferry` is a WPA2 SoftAP at `192.168.4.1`;
- `cfg.ap.max_connection = 4`;
- ESP-IDF `esp_http_server` already serves a phone-friendly page;
- the evolved ferry uses a static 124 KiB mailbox and an 8 KB live-pipe ring;
- its server reserves up to seven sockets (`CONFIG_LWIP_MAX_SOCKETS=16`) and
  a larger HTTP task stack;
- SoftAP must be 11b/g/n HT20, fail-closed `wifi_start`, pairwise CCMP;
  default C6 11ax beacons join but HTTP never comes up;
- UART pass must not ride on STAT (torn `pass=` flickers the QR);
- Bottlenose has no PSRAM assumption and 4 MB of flash.

BattleBridge does not allocate the ferry mailbox. Its fixed game state is
small: four players, eight enemies and 48 projectiles. The first binary
snapshot is 636 bytes. Four clients at 30 snapshots/s are roughly 76 KB/s
before WebSocket/TCP overhead, comfortably below SoftAP throughput and wholly
outside the OG UART.

The exact heap headroom is unverified until four real browsers connect. The
OG status screen reports free C6 heap so this can be measured rather than
guessed.

## v001 implementation in this fork

### Bottlenose

- `firmware/bottlenose/main/battlebridge_game.c/.h`
  - pure fixed-step game state;
  - compact 960×600 wraparound arena;
  - four players, 12-gate race, score/kills, enemy spawn/aim/fire;
  - bounded projectile/enemy pools;
  - random spawn attempts with distance from living players;
  - lobby, countdown, play and results phases;
  - optional bots filling empty seats with four skill levels;
  - 90 s match clock and sudden death;
  - 2v2 shared gates and friendly-fire off;
  - kill-streak pea upgrade; rematch from RESULTS;
  - laser wall bounce; ice skating by default, grip pickup for agility.
- `firmware/bottlenose/main/battlebridge.c/.h`
  - `FWOG-arena` WPA2 AP;
  - inline HTML/JS client served from C6 flash;
  - binary WebSocket input and 636-byte state snapshots;
  - keyboard/mouse and dual-touch (no on-screen stick HUD);
  - 33 ms game task (about 30 Hz);
  - `BN GAME START|STOP|STAT|GO|BOTS=0-3|TEAMS=0|1|WIPE` control protocol;
  - WPA2 pass in C6 NVS (`fwog`/`arena`); WIPE mints a new one;
  - cartoon cover at `/cover.jpg`;
  - Web Audio + `navigator.vibrate` in the inline client.
- `firmware/bottlenose/main/main.c`
  - starts BattleBridge as a mode;
  - stops ferry/BLE/HID/WIFIPROOF before game start;
  - reports low-rate status to the OG.
- `CONFIG_HTTPD_WS_SUPPORT=y` in `sdkconfig.defaults`.

### OG app

`apps/battlebridge/` is independent of LanFerry:

- `battlebridge_main` updates the display, controls Bottlenose and embeds the
  merged C6 image;
- `battlebridge_display` shows SSID, password, player count, phase, C6 heap
  and C6-flash instructions;
- Green toggles the arena AP, Gray tap forces a start or rematch, Gray hold
  cycles bot skill (or mints a new WPA2 pass while the QR is showing),
  Yellow enters the BOOT/RESET/Green C6 writer, Blue tap shows a Wi-Fi
  join QR, Blue hold toggles 2v2, and Red hold still powers off. The LCD
  stays on the lobby during a match.
- game-state traffic never traverses either RP2040.

The WPA2 password is eight characters from the LanFerry alphabet (no `0`,
`1`, `I`, `l`, `O`). It is stored in C6 NVS (`fwog`/`arena`) and reused
across AP stop/start and C6 reboot. Gray hold on the QR view rotates it.
The main RP2040 accepts it from a complete newline-terminated `BN GAME on`
or `BN GAME wipe pass=` line, validates it into a temporary status record,
then sends one CRC-framed update to the display. Later `BN GAME sta`
lines update player/phase/heap without rewriting the password. The LCD
therefore never builds a QR from a partially rewritten UART password. Its
payload is:

```text
WIFI:T:WPA;S:FWOG-arena;P:<eight text characters>;;
```

Current source integration places BattleBridge in the Bottlenose tree for
build convenience. Compatibility or coexistence with other Bottlenose apps is
not a BattleBridge product constraint; it may become a dedicated C6 image.

### Host test

`tests/test_battlebridge_game.c` checks:

- four slots and refusal of a fifth;
- all-ready countdown into play;
- gate crossing increments progress/score;
- firing allocates a projectile;
- dead players respawn at a new safe position with invulnerability.

## Wire protocol

Browser input is eight bytes at 30 Hz:

```text
type=1, sequence, move_x, move_y, aim_x, aim_y, fire, ready
```

Movement and aim are signed bytes. The C6 accepts intent, never client
positions or damage claims.

Snapshots are fixed 636-byte little-endian binary frames:

- 16-byte header: phase, slot, tick, arena, active counts and gate goal;
- four 19-byte player records;
- eight 8-byte enemy records;
- 48 ten-byte projectile records.

Fixed arrays avoid allocation in the 30 Hz loop. A later version can send
only active entities if measurements show a need.

## Match semantics still open to tuning

v001 ends when a player reaches 12 gates. Ranking should be:

1. gates completed;
2. combat score;
3. kills.

Current scoring is intentionally easy to inspect:

- gate: +25;
- hit on another player: +5;
- player kill: +100;
- enemy kill: +25;
- death: −15.

The main chat should tune these only after a four-device match. Combat must
matter without making the race objective irrelevant.

## Performance acceptance

“30 FPS” means browser rendering, not LCD/UART frames. A release candidate
needs a four-device soak with:

- browser render p50/p95 frame time;
- input-to-snapshot RTT;
- C6 game-step p50/p95 duration;
- free/minimum heap;
- WebSocket send failures/backlog;
- reconnect and random-respawn behavior;
- sustained fight with all 48 projectile slots and maximum enemies.

Degrade in this order if needed:

1. browser particles and cosmetic effects;
2. snapshot rate from 30 to 20 Hz while keeping 30 FPS interpolation;
3. enemy count;
4. projectile cap.

Do not move authoritative snapshots through the OG UART as a “performance
fix.”

## Build and bench sequence

1. Run host tests for `battlebridge_game`.
2. Build Bottlenose with ESP-IDF 5.5.1.
3. Merge `fwog_c6.bin` exactly as `firmware/bottlenose/README.md` describes.
4. Build and flash `battlebridge_main` only.
5. Yellow / hold BOOT / tap RESET / release BOOT / Green to write the C6;
   release BOOT and reset it afterward.
6. Green starts `FWOG-arena`; join and open `http://192.168.4.1/`.
7. Connect four browsers and record the performance acceptance metrics.

## Known v001 risks

- The C6 build and four-browser run have to prove WebSocket task stack and
  socket limits; they are not proven by host simulation.
- The AP has exactly four station slots, leaving no spectator station.
- Browsers may open extra HTTP sockets briefly; assets are one inline page to
  minimize that.
- Physics currently uses a compact deterministic-ish integer model, but Wi-Fi
  scheduling means browsers must interpolate.
- Touch controls need real-phone tuning.

