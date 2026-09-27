# KitHome recommendations — a trustworthy field-kit landing

A pentest field kit is only useful if you trust what its screen says while an
engagement is live, and if the owned-hardware tools on it are complete enough
to use without improvising on site. This page reviews the OG app slate against
those two bars and recommends a landing firmware (**KitHome**) plus the extra
legitimate features its apps need.

Rules of engagement here follow [retired.md](retired.md): **authorized
professional pentest** with written scope and spectrum permission passes
Law; a missing PHY still fails Silicon. This review argues two extra
bars for the landing firmware: *is the instrument trustworthy* (no
strobe you cannot trust) and *is the owned-hardware task completable*.
Unauthorized use against strangers stays out. KitHome **001** is already
firmware (`apps/kithome/`); this page is the recs that landed with it.
Named landings after KitHome are in [combo-images.md](combo-images.md)
(**EmsDesk** is the RF mux; IsmDesk was that working name).

## 0. Out of this recs pass

This review did **not** implement RF denial, a HID stack, a rolling-code
jam-leg, a cipher predictor, a payment reader, or an IMSI catcher.
[retired.md](retired.md) now treats authorized professional pentest as
passing Law; missing PHY still fails Silicon. Card readers and IMSI
catchers stay blocked on this board. Cipher-breaking next-code helpers
remain a separate crypto question. KitHome's first cut is HostDeck +
MicScope + BandScope, not those tools.

HostDeck's CDC helper (`tools/hostdeck/`) is the operator-at-the-keyboard
remainder. FobReplay QUEUE is unused-code replay. BandScope/TwinFox listen.
Those remain the firmware that exists today.

## 1. Per-app flicker / dirty-flag review

The panel has no read-back and no hardware reset; every draw is write-only over
SPI, and `st7789_fill_rect`/`st7789_clear` are asynchronous DMA wipes. Flicker
is what you see when code re-issues a full-area fill (or re-blits identical
glyphs) on a loop that runs every ~2 ms. Under RoE this matters directly: a
strobing spectrum or a flashing RSSI line makes you distrust a reading you are
about to act on. The fix pattern this repo has already blessed is a per-region
dirty flag, per-bin patching for graphs, and — new in the tree — a
`lcd_text_draw_padded_changed()` helper that skips the blit when a live line is
byte-identical to what is already on screen.

**Gold standard — leave alone.**

- **[BandScope](bandscope.md)** — the reference. Chrome, info, spectrum and
  hits each have their own dirty flag; the spectrum is patched bin-by-bin
  (`paint_bin`), clearing only a 3 px column when a bin actually moves, and the
  peak bin is tracked so it repaints exactly on transition. No full-area wipe
  in steady state.
- **[FobReplay](fobreplay.md)** and **[ISMburst](ismburst.md)** — mature
  multi-region discipline: a one-time `s_painted` clear, then `chrome` /
  `status` / `rssi` / `decode` regions each gated, plus a 200 ms throttle on
  the RSSI line so a status-per-200-ms link cannot strobe the whole UI.

**MicScope — already fixed in the tree.** The version I reviewed no longer
wipes the spectrogram every frame. It patches bins against `s_drawn_bar` (only
changed 9 px columns are cleared and redrawn, the same idea as BandScope), does
its one full-area fill exactly once behind `s_bars_inited`, and routes the
VU/PDM text through `lcd_text_draw_padded_changed`. No further action; verified
under ogemu (the overlay log shows the `rms/peak` line blitting only on 0 →
2400 → 9000, not per tick).

**HostDeck — full `st7789_clear()` on page change.** This is *gated* (it only
runs when `s_chrome_dirty` is set, i.e. on a page switch), so it is not a
per-frame strobe — but it flashes the whole panel on every page toggle. The two
pages share an identical layout and differ only in labels, so the clear is
avoidable: patch the five labels with `lcd_text_draw_padded` and drop the
`st7789_clear`. This matters more once HostDeck is a KitHome view, because tile
navigation would otherwise flash the screen on every selection move.

**Chrome-gated-only family — live lines re-blit every ~2 ms.** These gate the
header behind `s_chrome_dirty` but then redraw their live data lines *and their
static hint lines* unconditionally every loop iteration, which is exactly the
"rewrite identical glyphs every 2 ms" case the new helper documents:

- **TwinFox** and **TireEar** — fixed during this review (see below).
- **HeaderKit**, **TtyGlass**, **[InertialTrailRF](inertialtrailrf.md)**,
  **[PingHalo](pinghalo.md)**, **TalkClip**, **LanFerry**,
  **InertialTrail** — the same mechanical treatment landed in the
  follow-up flicker pass (VERSION bumps in those apps' CMakeLists).
  Left on this page as the original rec, not as remaining work.

### Code touched during this review

Display-only, dirty-flag-only, no new radio modes, verified under ogemu:

- **`apps/twinfox/display/main.c`** — the frequency/beacon line and the
  `RSSI0/RSSI1` line now go through `lcd_text_draw_padded_changed` (two slot
  buffers); the two static hint lines moved into the chrome block. Smoke shows
  the RSSI line drawing once at baseline and once when `tf_status` arrives,
  instead of every tick.
- **`apps/tireear/display/main.c`** — same fix for the mode line, the
  `RSSI/last/ms` line, and the `bursts` line (three slots); the static hint
  moved into the chrome block. Smoke confirms one draw per change.

MicScope needed no edit from me (already fixed in the tree).

## 2. The KitHome trio: HostDeck + MicScope + BandScope

I looked for a stronger trio and did not find one. Keep the parent's three.
They are the strongest *native* set (no Bottlenose) that can share **one main
UF2**, and the reason is main-CPU contention, not taste:

- **Coverage is orthogonal.** HostDeck = this-PC macros, MicScope = acoustic
  survey, BandScope = sub-GHz listen. Three different sensing/output surfaces a
  field kit opens first, with no overlap. A radio-only trio (BandScope +
  ISMburst + FobReplay) would cover one domain three times.
- **Only one of the three drives the main CPU.** This is the decisive point for
  "share one main UF2." HostDeck's macros print on the display CDC (no radio);
  MicScope's PDM mic and FFT are entirely on the display CPU (main idle). Only
  BandScope needs the main CPU — a CC1101 sweep feeding `bs_status` and antenna
  switching. So the combined **KitHome-main** UF2 is just BandScope's sweep
  engine plus the mandatory `board_watchdog_kick()`; the other two impose
  **zero** main-side conflict. A radio-heavy trio would force two CC1101s and
  three different main-side protocols to be multiplexed in one binary — heavier
  and more fragile for no coverage gain.
- **The contract is already satisfied.** All three display halves declare
  `FWOG_POWER_DEFAULT()` and poll `fwog_power_poll()`; the KitHome-main declares
  `FWOG_WATCHDOG_DEFAULT()` and kicks. Bring-up order (display powers off, main
  kicks) is met by construction.

Mechanically, **KitHome is one display app** — a landing menu that dispatches
to three in-process views — carried by one KitHome-main UF2. It is not three
separately flashed apps. ISMburst and OpticClick are the natural page-4/page-5
additions *after* the landing exists (owned-gadget store, owned-TV IR), but
they stay off the first cut: ISMburst wants a capture/replay main mode that
conflicts with BandScope's continuous sweep, and OpticClick is display-CPU IR
that adds a fourth surface before the first three are proven on a board.

## 3. Extra legitimate features these three need

All implementable on OG silicon **without Bottlenose**, all receive/own-device,
no TX modes added. Each is warranted because it closes a gap that otherwise
forces improvisation mid-engagement.

**HostDeck — this-PC macros (finish the honest remainder).**
- The host helper already ships (`python tools/hostdeck/hostdeck.py`). It
  now accepts both `FWOG display hostdeck …` and `FWOG display kithome …`
  so KitHome's MACRO lines type into this PC. TinyUSB HID toward an
  in-scope host is a separate pentest idea (retired.md), not this helper.
- Editable, labelled slots and more than two pages, configured over CDC rather
  than recompiled — so an operator can set up engagement-specific chords
  (screenshot, timestamp-note, open-capture-tool) without a rebuild.
- Per-slot LED confirmation on fire (already partial) so you can see a macro
  landed without looking at the host.

**MicScope — acoustic site survey.**
- **Peak/max-hold overlay** on the spectrogram (mirror BandScope's hold), so a
  transient — a lock click, an HVAC cycle, a beep — leaves a visible mark
  during a walkthrough instead of vanishing between frames.
- **Baseline + delta**: capture a quiet-floor reference on a keypress and show
  the live spectrum as a delta, which is how you actually find the noisy corner
  of a room.
- **Band cursor**: arrow-select a bar to read its exact Hz and level, so a
  finding is a number you can write down, not "the third bar looked tall."
- **Survey log to CDC** (CSV: `t, dom_hz, rms`) for a walkthrough record —
  MicScope logs the survey the way TalkClip keeps WAVs.
- All PDM mic + `bsp/common/dsp` FFT on the display CPU — native, main idle.

**BandScope — sub-GHz listen (+ the "owned gadgets" leg).**
- **Owned-gadget markers**: mark a peak as *mine* (label + frequency) and show
  whether it is currently present. This folds ISMburst's owned-gadget store
  idea into *listen* without adding capture or replay to KitHome — it is
  receive-only presence, which is what you want when confirming your own beacon
  or remote is alive on site.
- **Waterfall history strip** (time down the Y axis) using the same per-bin
  patching, to catch an intermittent emitter across a survey window.
- Persist peak-hold and top-5 across band switches (the per-band hold arrays
  already exist) with the existing RED = clear.
- **No transmit modes.** Listening, marking, and holding only.

Together these make the pair a real recon kit: MicScope for the room, BandScope
for the spectrum, HostDeck to drive the laptop taking notes — all against
assets and a site you are scoped for.

## 4. Button map for the landing

The debounce machine and `fwog_power_poll()` already give press/down/released
edges and held-time; the landing just needs one reserved gesture and a
tap-vs-hold split. Threshold: **700 ms**, matching HostDeck/TwinFox today.

**On the KitHome landing screen** (shipped in KitHome 001)
- **GRAY** = next tile; **RED tap** = previous tile. RED hold 6 s is still
  ship (`fwog_power_poll`). Take buttons from the poll's return value
  (contract rule 2).
- **GREEN** = open the selected app.
- **BLUE** = free on the landing.

**Inside any app** (shipped)
- **YELLOW held ≥ 700 ms = return to the landing.**
- **BandScope yellow-tap** still changes band; the hold suppresses the step.
- **HostDeck pages on blue-hold** (toggle); yellow-hold is home, not page.
- **MicScope** had no yellow binding, so yellow-hold = home dropped in.

## 5. What ogemu can prove vs what needs a board

**ogemu proves (host, no board):**
- The landing itself: tile layout, gray/red selection movement, green-open
  dispatch, and yellow-hold-home return — all via injected buttons (the real
  debounce machine) asserted against the text-overlay log.
- **Draw discipline / the flicker fixes.** The overlay log records every
  `lcd_text_draw_padded[_changed]` call, so you can assert a line blits *only*
  on change — which is exactly how the TwinFox/TireEar/MicScope fixes above
  were verified. This proves the *mechanism* (no redundant full-area fills or
  identical re-blits are issued).
- Tap-vs-hold timing on virtual milliseconds, including the 700 ms yellow split
  and that a RED tap navigates while a `hold red 6000` still logs "ship would
  run."
- Receive-side chrome: `bs_status`, `te_status`, `tf_status`, `mic_rms` /
  `mic_tone`, etc. render BandScope's spectrum, MicScope's readout, and
  owned-gadget markers so they can be asserted headless.

**Needs a real board (out of ogemu scope):**
- The **6 s red-hold actually powering the board off** — contract rule 2's
  evidence is a hardware observation, and KitHome's landing must be seen doing
  it. ogemu runs the ship machine but does not write the charger.
- The **KitHome-main radio engine**: real CC1101 sweep, antenna switching, and
  the watchdog kick. ogemu has no main CPU and no CC1101; `bs_status` is
  injected, not generated.
- **HostDeck's macro delivery to this PC** — TinyUSB/CDC is not emulated; link
  TX is DIAG-dropped.
- **Real audio through the PDM mic** — ogemu feeds a synthetic square
  (`mic_rms`, spectrogram blank) or sine (`mic_tone`), so peak-hold and the
  baseline/delta survey features must be confirmed against a real room.
- **Perceived flicker.** ogemu's RAM framebuffer shows the *final* image, not
  the DMA strobe, so it confirms we stopped issuing redundant fills but a board
  confirms the flash is actually gone to the eye.
- 1200-baud BOOTSEL, flashing, the USB identity strings on the wire, the
  display-image embed/CRC-skip transfer, and the button→pin map (only a real
  press proves the pin map).
