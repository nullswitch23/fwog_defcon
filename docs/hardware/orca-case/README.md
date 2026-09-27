# Orca Bottlenose snap-together case

A 4-piece FDM shell for the **Bottlenose** ESP32-C6 Orca only — not a
FreeWili OG enclosure. It covers the PCB island between the two 2×10
headers, leaves both headers free to mate, and turns the painful BOOT /
RESET tactiles into two **round tap plungers**. Hold BOOT with a finger
and tap RESET; there is no latch and no slider.

The C6 roof is an **intentional RF window** — no plastic slab over the
module’s can or PCB antenna. The USB-C / Qwiic (+Y) edge is a **4.0 mm**
lid collar with port bosses. The **bottom** is the larger pocket; the
**top clicks into it** (the old wrap-around lid never met at the LED
edge, even empty). Walls / lid / floor are **2.0 / 2.4 / 2.0 mm**.
Male clips hang off the top skirt; windows are in the bottom walls.

From the button wells, both X edges are **taken in 2 mm** so the lid is
**31 mm** and drops between the GPIO header inners. The CN4 skirt hangs
**2 mm lower** than CN2 and sits on the gold pins. There is no LED hole
under BOOT. Plunger pads are **5.0 mm** so they stand ~2 mm above the lid.

USB-C on Bottlenose is **debug / flash only, not power**. Keep the OG
powered. Unplug this USB-C for UART ROM download (the C6 ROM prefers
USB-Serial/JTAG when that cable is in). The case marks the port with
three pits, not a power icon.

Source: `orca_bottlenose_case.scad`. STLs in this folder were compiled
from that file with OpenSCAD 2021.01. If you change a parameter, rebuild
the STLs — do not edit triangles by hand.

Photo used: [`docs/images/BottleNose_FACE.webp`](../../images/BottleNose_FACE.webp)
(the only image under `docs/images`). Firmware hookup:
[`firmware/bottlenose/README.md`](../../../firmware/bottlenose/README.md).
Header family heights: [`../microsd.md`](../microsd.md).

## Parts

| STL | What it is |
|---|---|
| `orca_case_bottom.stl` | Pocket tray the lid clicks into; open C6 floor; open USB/Qwiic bay |
| `orca_case_top.stl` | Lid + skirt: 4.0 mm USB-C/Qwiic collar, male clips, RF roof, two wells, CN4 lip 2 mm lower |
| `orca_case_boot_plunger.stl` | Round tap plunger for BOOT (print in a contrasting colour) |
| `orca_case_reset_plunger.stl` | Round tap plunger for RESET (same geometry, same colour) |

Four clips, no screws. The halves should snap **with the board out**.
Print **two plungers** (BOOT + RESET); they share the 5.4 mm pad / stem.
`orca_case_boot_slider.stl` is gone — BOOT is not a slider.

CN2 (left in the FACE photo) and CN4 (right) are not covered. **CN4 is
the OG mate** (Henry, 2026-09-12). CN2 stays the pass-through 2×10.
Both sides stay open. Both header slots are the same size: **33 mm**
shroud (measured, CN2 end-on) plus `fit_gap` each end.

## Button map

FACE photo, USB-C / Qwiic on the **top** edge, C6 antenna toward the
**bottom** edge:

```
        Qwiic        USB-C  (debug — not power)
     +--------+------------------+
CN2  |  RESET  (round tap)       |  CN4  (OG 20-pin)
     |                           |
     |  BOOT   (round tap)       |
     |  LED LED   [antenna win]  |
     +--------+------------------+
```

- **RESET (S1)** is the **upper** tactile, closer to USB-C.
- **BOOT (S2)** is the **lower** tactile, closer to the LEDs / antenna.
- Pads are **5.4 mm** at 7 mm C-C. No outer finger ridge — it floated the
  lid off the bed. The two round pads are the tell. Same geometry for
  BOOT and RESET.

### UART / OG flash (hold BOOT, tap RESET)

PingHalo / LanFerry / BleDeck Yellow writer, ~1 minute:

1. OG powered. **Unplug Orca USB-C.**
2. Hold **BOOT** down with a finger (round plunger). Leave it held.
3. **Tap RESET** (the other round plunger).
4. Green on the OG while BOOT is still held.
5. When the write finishes, release BOOT.

USB-C + `idf.py flash` still works: the tunnel is sized for a cable plug.
Unplug it again before a UART ROM write.

## Print

Typical FDM, **0.4 mm nozzle**, **0.2 mm layers**. PETG for the clips is
happier than PLA; PLA is fine for a first fit check. Contrasting PETG
for the two plungers is optional and useful.

| Part | On the bed | Supports | Notes |
|---|---|---|---|
| top | Outer lid down (flat) | none | Skirt prints up; outward 45° hooks; USB hole is a rounded bridge |
| bottom | Floor down (open side up) | none | Taller pocket; clip windows are short holes in the vertical walls |
| boot_plunger | Round pad down | none | 5.4 mm pad; stem stands up; print one |
| reset_plunger | Round pad down | none | Same STL geometry; print one |

Suggested: 3 perimeters, 20 % infill, 0.4 mm extrusion width. Min wall
in the model is 2.0 mm (4.0 mm on the +Y lid). Do not scale in the slicer — change the
OpenSCAD parameters instead.

Rebuild:

```
openscad -D part=1 -o orca_case_top.stl            orca_bottlenose_case.scad
openscad -D part=2 -o orca_case_bottom.stl         orca_bottlenose_case.scad
openscad -D part=3 -o orca_case_boot_plunger.stl   orca_bottlenose_case.scad
openscad -D part=4 -o orca_case_reset_plunger.stl  orca_bottlenose_case.scad
```

`part=0` is the GUI preview (ghost PCB + assembled shell). `part=5` is a
one-plate layout. On this Windows tree, `export.ps1` runs the four STLs
through `C:\Program Files\OpenSCAD\openscad.com`.

If a snap is tight, raise `print_clear` (plungers) or `fit_gap`
(PCB pocket / header slots) by 0.1 mm and rebuild. If it rattles, drop
`fit_gap` to 0.4.

## Assemble

1. Drop both plungers into the round lid holes **from the inside**
   (after printing, cavity up: pad goes through the well, flange
   catches). The 6.8 mm flange stays under the lid; the 5.0 mm pad
   stands ~2 mm above the plate. One in the BOOT well (closer to the
   LEDs), one in RESET (closer to USB-C). Same part.
2. Seat Bottlenose in the bottom pocket: the 31 mm island drops between
   the GPIO inners. CN4’s gold pins take the lowered right lip. Headers
   hang out left and right, USB-C / Qwiic toward the open bay, antenna
   over the floor vent. The 2×10 shrouds are 33 mm; the slots are 33 mm
   + 0.5 mm per end.
3. Drop the lid into the bottom pocket until four hooks click (two USB,
   two LED). The snap does not need the PCB in place.
4. Plug **CN4** onto the OG header. The case is Orca-only; it does not
   wrap the OG shell. CN2 stays free.

## What is covered, what is not

Covered: PCB edges of the island, USB-C / Qwiic **4.0 mm** lid collar with
port bosses, 5.4 mm corner posts and lid fillets. Underside short
protection is a floor with an open C6 window **merged into the CN4
header slot** (no leftover strip). The tray +Y service edge is an **open
bay** — Qwiic and USB-C are framed by the lid, not by a thin tunnel floor.

Not covered: CN2 and CN4 mating faces (side and bottom). **The ESP32-C6
can and PCB antenna** — the lid is open there on purpose so 2.4 GHz
BLE/Wi-Fi is not under plastic. Do not close that roof. The bottom floor
under the C6 is open for the same reason.

Qwiic (CN3) stays reachable through the +Y lid wall next to USB-C.
LED1 / LED2 sit under the solid lid (no extra hole under BOOT).

## Dimensions — tags

Every linear size is a named parameter at the top of the `.scad`.

| Tag | Meaning |
|---|---|
| **measured** | Calipers on this board, or industry / datasheet size |
| **photo_inferred** | Scaled from `BottleNose_FACE.webp` |
| **guess_mm** | Typical part, conservative clearance, or 3M family height |
| **needs_henry** | Please measure; the model will move when you do |

Photo scale: **0.06055 mm/px**, from the ESP32-C6-MINI-1 shield width
13.2 mm (219 px). Henry’s 2026-09-13 photos freeze the island at **31 mm**
(magenta), each shroud at **9 mm** (green), Y at **33 mm** (orange), PCB
at **1.5 mm**, CN2 height **9 mm** (blue, side), CN4/OG stack **6 mm**
(green, side). Remaining photo-inferred X positions are shifted +0.6 mm
with the shroud width.

| Parameter | Value | Tag |
|---|---|---|
| `pcb_x` | 49.0 mm | derived (9 + 31 + 9) |
| `island_span` | 31.0 mm | **measured** (magenta, inner-to-inner) |
| `pcb_y` / `hdr_len` | 33.0 mm | **measured** (orange) |
| `pcb_t` | 1.5 mm | **measured** (Henry) |
| `hdr_w` | 9.0 mm | **measured** (green, CN2 end-on) |
| `hdr_open_y` | 34.0 mm | derived (`hdr_len` + 2×`fit_gap`) |
| `hdr_above` (CN2) | 9.0 mm | **measured** (blue, side) |
| `hdr_above_cn4` / `hdr_below` | 6.0 mm | **measured** OG mate stack (green, side) |
| `c6_w` × `c6_l` × `c6_h` | 13.2 × 16.6 × 2.4 mm | photo_inferred / guess_mm height |
| antenna toward PCB −Y | 4.6 mm of the module | photo_inferred |
| C6 RF roof | whole can + antenna + 1.5 mm | **intentional open window** (Henry) |
| USB-C shell width | 8.94 mm | measured (Type-C 16P) |
| `usb_cx` | 27.2 mm | photo_inferred +0.6 |
| USB height, depth, overhang | 3.25 / 7.3 / 0.8 mm | guess_mm |
| `usb_tunnel` / `usb_wall_t` | 10.0 / 4.0 mm | lid collar |
| Qwiic housing width | 6.0 mm | measured (JST SM04B-SRSS-TB) |
| `qwiic_cx` | 19.2 mm | photo_inferred +0.6 |
| Bottom +Y service | open bay between 5.4 mm posts | **open window** |
| C6 floor vent | merged into CN4 slot | **open window** |
| RESET centre | (17.5, `boot_cy`+7.0) mm | photo_inferred X +0.6; Y from measured C-C |
| BOOT centre | (18.2, 5.9) mm | photo_inferred +0.6 |
| `sw_cc` | 7.0 mm | **measured** (Henry) |
| switch body | 6×6 mm | photo_inferred from 7 mm C-C |
| OG mate | CN4 | **measured** (Henry) |
| `sw_h` / preload / travel | 4.5 / 0.8 / 0.4 mm | guess_mm; nub = cavity − stem + preload |
| plunger pad / well / flange | 5.4 / ~5.84 / 6.8 mm | pad **5.0 mm** tall, ~2 mm proud of the lid |
| LEDs | (21.5, 1.4), 0603, 2.5 mm pitch | photo_inferred +0.6; **no lid hole** |
| `fit_gap` / `print_clear` / `nest_clear` | 0.50 / 0.18 / 0.30 mm | FDM; nest is top-into-bottom |
| `cn2_widen` / `cn4_widen` / `cn4_drop` | 0.0 / 1.0 / 2.0 mm | 31 mm island; CN4 lip 2 mm lower (Henry 2026-09-13) |
| `top_taller` / `inner_clear_z` | 0.5 / 2.1 mm | lid ends flush with the PCB |
| `wall_t` / `lid_t` / `floor_t` | 2.0 / 2.4 / 2.0 mm | was 1.2 / 1.6 / 1.2 |
| clip hook / `clip_h` | 0.70 mm, 45° / 4.6 mm | males on the top; windows in the bottom |

No mounting holes are visible on the FACE photo. Location is USB-C +
Qwiic + header inner walls + LED-edge lip. There is no back photo.

## Measurement list for Henry

Calipers, millimetres. One pass is enough to freeze the model.

1. **PCB overall** X × Y × thickness: **49 × 33 × 1.5 mm** (9+31+9, 2026-09-13).
2. **Distance between header inner walls:** **31 mm** (magenta).
3. **CN4** is the OG mate (done). Still need the **height of that female
   below the PCB** if unknown.
4. **CN2 above PCB is 9 mm** (blue). **CN4/OG stack is 6 mm** (green, side).
5. **USB-C:** shell W×D×H, overhang past the PCB edge, height above PCB.
6. **Qwiic:** housing W×D×H, overhang, height above PCB.
7. **BOOT and RESET:** C-C is **7 mm** (done). Still: stem diameter / exact
   height above PCB if the new nubs are long or short.
8. **C6 module:** confirm 13.2×16.6, antenna-end toward which PCB edge,
   any keep-out silk.
9. **LED1 / LED2:** package, centres from two edges.
10. **Back side:** any mounting holes, bottom-side components taller than
    ~1 mm, anything the floor would hit.
11. **Tactile travel** and whether the switches are SMT or through-hole.

Until those land, the cavity uses `fit_gap` 0.5 mm per side on the
measured 33 mm shroud so FDM shrinkage does not repeat the first-print
pinch.
