// Orca Bottlenose snap-together case
// FreeWili OG ESP32-C6 add-on — Orca only, not an OG shell.
// Nest: larger bottom pocket, top clicks in (males on the lid skirt).
// Island is 31 mm between header inners — no overhang onto CN2/CN4.
//
// OpenSCAD 2021.01+. Every linear size is a named parameter below.
// Tags: measured | photo_inferred | guess_mm | needs_henry
//
// Export (integers avoid PowerShell eating quoted strings):
//   openscad -D part=1 -o orca_case_top.stl            orca_bottlenose_case.scad
//   openscad -D part=2 -o orca_case_bottom.stl         orca_bottlenose_case.scad
//   openscad -D part=3 -o orca_case_boot_plunger.stl   orca_bottlenose_case.scad
//   openscad -D part=4 -o orca_case_reset_plunger.stl  orca_bottlenose_case.scad
//   openscad -D part=5 -o orca_case_plate.stl          orca_bottlenose_case.scad
//
// part = 0 shows a ghost PCB + assembled shell (GUI F5 preview).
// BOOT and RESET share one round tap plunger (same 6×6 family). Print two.

part = 0; // 0 preview, 1 top, 2 bottom, 3 boot_plunger, 4 reset_plunger, 5 plate

$fn = 32;
eps = 0.05;

// ---------------------------------------------------------------------------
// Coordinate system (component side up, USB-C / Qwiic on +Y)
//   origin = overall SW corner: CN2 outer wall, LED / antenna edge
//   +X     = toward CN4
//   +Y     = toward USB-C
//   +Z     = up from the PCB bottom face
// Photo: docs/images/BottleNose_FACE.webp (the only image under docs/images).
// Scale: 0.06055 mm/px from ESP32-C6-MINI-1 shield width 13.2 mm (218 px).
// ---------------------------------------------------------------------------

// --- board outline ----------------------------------------------------------
// Henry 2026-09-13 photos: island 31 (magenta), shroud 9 (green), Y 33 (orange).
pcb_t = 1.5;         // measured, Henry
hdr_w = 9.0;         // measured, CN2 end-on shroud (green, top photo)
island_span = 31.0;  // measured, CN2 inner to CN4 inner (magenta)
pcb_x = hdr_w + island_span + hdr_w; // 49.0; was 48.3 photo_inferred
hdr_len = 33.0;      // measured, shroud / island Y (orange)
pcb_y = hdr_len;
hdr_above = 9.0;     // measured, CN2 shroud above PCB (blue, side photo)
hdr_above_cn4 = 6.0; // measured, CN4 / OG mate stack above PCB (green, side)
hdr_below = 6.0;     // guess_mm, now tied to the 6 mm OG mate; needs_henry
hdr_pin_inset = 3.3; // photo_inferred, gold pin field inboard of shroud

// CN4 (right, +X) is the female onto the OG. CN2 stays the pass-through.
// Both sides stay uncovered so the OG stack and the exposed 2×10 still mate.
og_header_is_cn4 = true; // measured, Henry 2026-09-12

// --- ESP32-C6-MINI-1 (MOD1) -------------------------------------------------
c6_w = 13.2;         // photo_inferred, can width matches MINI-1 (px 219)
c6_l = 16.6;         // photo_inferred, can ~12.0 + antenna ~4.6 (MINI-1 family)
c6_h = 2.4;          // guess_mm, MINI-1 typical; needs_henry
c6_x = 22.1;         // photo_inferred +0.6 (hdr_w 8.4→9.0)
c6_y = 1.7;          // photo_inferred, antenna toward PCB -Y (LED edge)
ant_l = 4.6;         // photo_inferred, black PCB antenna on the module
ant_keepout = 1.5;   // guess_mm, extra window margin around the antenna
// Roof over the whole module is cut away on purpose (Henry 2026-09-13):
// 2.4 GHz PCB antenna must not sit under a dielectric slab. Metal can too.
c6_rf_margin = 1.5;  // extra lid cutout around can + antenna

// --- USB-C (CN1) — debug / flash ONLY, not power ----------------------------
usb_shell_w = 8.94;  // measured, USB Type-C 16P metal shell width
usb_shell_d = 7.3;   // guess_mm, typical SMT receptacle depth; needs_henry
usb_shell_h = 3.25;  // guess_mm, top-mount shell height; needs_henry
usb_cx = 27.2;       // photo_inferred +0.6 (hdr_w 8.4→9.0)
usb_overhang = 0.8;  // guess_mm, past the +Y PCB edge; needs_henry
usb_plug_w = 13.6;   // guess_mm, cable-plug opening (not the metal shell)
usb_plug_h = 8.0;    // guess_mm, cable-plug opening; extra FDM clearance

// --- Qwiic CN3, JST SM04B-SRSS-TB ------------------------------------------
qwiic_w = 6.0;       // measured, JST SH 4-pin housing width
qwiic_d = 4.25;      // guess_mm, RA housing depth; needs_henry
qwiic_h = 2.9;       // guess_mm, RA housing height; needs_henry
qwiic_cx = 19.2;     // photo_inferred +0.6 (hdr_w 8.4→9.0)
qwiic_overhang = 0.5;// guess_mm; needs_henry
qwiic_open_w = 8.6;  // guess_mm, cable opening
qwiic_open_h = 5.0;  // guess_mm

// --- RESET (S1, upper) and BOOT (S2, lower) ---------------------------------
// Henry: BOOT–RESET centre-to-centre is 7 mm (along +Y). Photo X offset
// ~0.7 mm kept; body still needs 4x4 vs 6x6.
sw_cc = 7.0;         // measured, Henry 2026-09-12
sw_body = 6.0;       // photo_inferred from 7 mm C-C; needs_henry 4x4 vs 6x6
sw_stem_d = 3.5;     // guess_mm, 6x6 tact stem; needs_henry
sw_h = 4.5;          // guess_mm, 6x6 family stem top; plungers target this
sw_travel = 0.4;     // guess_mm, 0.25–0.5 typical; needs_henry
sw_preload = 0.8;    // extra nub so a printed lid still reaches the stem
reset_cx = 17.5;     // photo_inferred +0.6 (hdr_w 8.4→9.0)
boot_cx  = 18.2;     // photo_inferred +0.6
boot_cy  = 5.9;      // photo_inferred
reset_cy = boot_cy + sw_cc; // derived from measured C-C

// --- status LEDs, bottom edge (LED2 left, LED1 right of it) -----------------
led_x = 21.5;        // photo_inferred +0.6 (hdr_w 8.4→9.0)
led_y = 1.4;         // photo_inferred, on the component face
led_d = 1.6;         // guess_mm, 0603; needs_henry
led_pitch = 2.5;     // guess_mm, two LEDs; needs_henry

// --- case / FDM -------------------------------------------------------------
wall_t = 2.0;        // was 1.2; 5× 0.4 mm — stiffer halves (Henry 2026-09-13)
lid_t = 2.4;         // was 1.6
floor_t = 2.0;       // was 1.2
fit_gap = 0.50;      // 0.5 mm per side on the 33 mm shroud (FDM shrinkage)
cn2_widen = 0.0;     // was 2; taken in so the lid fits the 31 mm island
cn4_widen = 1.0;     // was 3; same 2 mm take-in on the CN4 side (button align)
cn4_drop = 2.0;      // CN4 skirt sits 2 mm below CN2, on the gold pins
top_taller = 0.5;    // lid ends flush with the PCB
nest_clear = 0.30;   // FDM gap: top clicks INTO the larger bottom
print_clear = 0.18;  // plungers
outer_r = 2.4;
usb_tunnel = 10.0;
led_extra = 2.5;
inner_clear_z = 2.1; // was 1.6; includes top_taller
usb_wall_t = 4.0;    // +Y lid collar — front plate stays here
usb_gusset = 5.0;
usb_slot_r = 2.0;
usb_post_w = 5.4;
usb_boss = 3.2;
lap_z = 2.0;

clip_w = 8.0;        // LED-wall clips live on the TOP, catch in the bottom
clip_w_usb = 4.0;
clip_t = 1.2;        // into the bottom pocket wall; top clicks in
clip_hook = 0.70;
clip_hook_z = 1.4;
clip_inset = 5.0;
clip_inset_usb = 0.4;

// Round tap plungers, identical BOOT and RESET.
// 10 mm pads / 14 mm flanges cannot sit at 7 mm C-C. Pads are 5.4 mm
// so both wells fit with a strip of lid between them. No outer fence.
rst_pad_d = 5.4;
rst_pad_h = 5.0;     // ~2 mm proud of the outer lid when the flange is seated
rst_stem_d = 5.1;    // was 3.4; was rattling in a 6 mm hole
rst_flange_d = 6.8;  // 0.2 mm shy of C-C so the pair can sit; 0.55 mm well lip
rst_flange_t = 1.4;
well_extra = 0.08;   // FDM holes print undersize; keep a little slop

// derived
island_x0 = hdr_w;
island_x1 = pcb_x - hdr_w;
island_w  = island_x1 - island_x0;
bot_outset = wall_t + nest_clear;
inner_h   = sw_h + inner_clear_z;
z_floor   = -floor_t;
z_pcb_top = pcb_t;
z_split   = pcb_t + 0.9;
z_lid_in  = pcb_t + inner_h;
z_lid_out = z_lid_in + lid_t;
// Clips hang off the TOP skirt and catch in the BOTTOM pocket wall.
clip_h    = 4.6;
z_clip0   = z_lid_in - clip_h;
z_clip1   = z_clip0 + clip_h;
z_hook    = z_clip0 + clip_h - clip_hook_z / 2;

hdr_open_y  = hdr_len + 2 * fit_gap;
hdr_open_y0 = (pcb_y - hdr_open_y) / 2;
hdr_usb_web = 2.5; // floor/wall must survive at the USB end of the slots

well_d = rst_pad_d + 2 * print_clear + well_extra;

case_x0 = island_x0 + fit_gap - cn2_widen;
case_x1 = island_x1 - fit_gap + cn4_widen;
case_y0 = -led_extra;
case_y1 = pcb_y + usb_tunnel;
case_w  = case_x1 - case_x0;
case_d  = case_y1 - case_y0;

usb_cy = pcb_y - usb_shell_d / 2 + usb_overhang;
qwiic_cy = pcb_y - qwiic_d / 2 + qwiic_overhang;

// ---------------------------------------------------------------------------
module rounded_rect(w, d, r) {
    translate([-w / 2, -d / 2])
        offset(r = r) offset(delta = -r) square([w, d]);
}

module case_outline_2d(outset = 0, r = outer_r) {
    translate([case_x0 - outset, case_y0 - outset])
        offset(r = r) offset(delta = -r)
            square([case_w + 2 * outset, case_d + 2 * outset]);
}

module clip_male() {
    // Hook faces +Y. 45° underside so it prints as a short overhang.
    overlap = 1.5; // bury into the skirt so CGAL unions (was 0.5, two volumes)
    union() {
        translate([0, -overlap, 0])
            cube([clip_w, clip_t + overlap, clip_h]);
        hull() {
            translate([0, clip_t - 0.15, clip_h - clip_hook_z])
                cube([clip_w, 0.15, clip_hook_z]);
            translate([0, clip_t + clip_hook, clip_h - 0.25])
                cube([clip_w, 0.05, 0.25]);
        }
    }
}

module clip_window_cutter() {
    cube([clip_w + 0.8, bot_outset + clip_hook + 1.2, clip_hook_z + 1.0], center = true);
}

module rounded_y_slot(w, h, depth) {
    // Slot through a +Y wall, centred, rounded ends. depth along Y.
    r = min(usb_slot_r, w / 2 - 0.2, h / 2 - 0.2);
    rotate([90, 0, 0])
        linear_extrude(depth, center = true)
            offset(r = r) offset(delta = -r)
                square([w, h], center = true);
}

module usb_qwiic_cutters() {
    // Cable openings in the +Y wall. USB-C is a service port, not a PSU inlet.
    // Lid only — the tray is a full open bay (bottom_service_window_cutter).
    translate([usb_cx, case_y1, z_pcb_top + usb_shell_h / 2])
        rounded_y_slot(usb_plug_w, usb_plug_h, 28);
    translate([qwiic_cx, case_y1, z_pcb_top + qwiic_h / 2])
        rounded_y_slot(qwiic_open_w, qwiic_open_h, 28);
}

module bottom_service_window_cutter() {
    // Tray +Y used to keep a 1.2 mm tunnel floor with USB/Qwiic slots cut
    // through a wall shorter than the plug holes. That remnant did not
    // print (Henry 2026-09-13). Leave a completely open bay between the
    // corner posts; the lid collar is the port.
    x0 = case_x0 + usb_post_w;
    x1 = case_x1 - usb_post_w;
    y0 = pcb_y + 0.8; // leave hdr_usb_web so the +Y corners stay on the tray
    w = x1 - x0;
    d = case_y1 - y0 + 5;
    if (w > 2)
        translate([x0, y0, z_floor - 1])
            linear_extrude(z_lid_out - z_floor + 4)
                offset(r = 1.2) offset(delta = -1.2)
                    square([w, d]);
}

module bottom_c6_floor_window() {
    // Floor vent under the whole C6, merged into the CN4 header slot so
    // the old <1 mm strip between can and shroud is not a print feature.
    x0 = c6_x - c6_rf_margin;
    x1 = min(island_x1 + fit_gap, case_x1 - wall_t - 0.2);
    y0 = c6_y - c6_rf_margin;
    y1 = c6_y + c6_l + c6_rf_margin;
    translate([x0, y0, z_floor - 1])
        cube([x1 - x0, y1 - y0, floor_t + 3]);
}

module c6_rf_window_cutter() {
    // Open roof over the whole C6 (can + PCB antenna). Not a vent hole —
    // a dielectric slab over the antenna kills 2.4 GHz. Clip −X so the
    // plunger wells keep a strip of lid.
    aw = c6_w + c6_rf_margin;          // keepout mainly +X / ±Y, not into buttons
    ad = c6_l + 2 * c6_rf_margin;
    ax = c6_x + c6_w / 2 + c6_rf_margin / 2;
    ay = c6_y + c6_l / 2;
    translate([ax, ay, z_lid_in - 1])
        cube([aw, ad, lid_t + 6], center = true);
}

module plunger_well_cutter(cx, cy) {
    // Full cylinder through the lid. 0.4 mm nozzle.
    translate([cx, cy, z_lid_in - 2])
        cylinder(h = lid_t + 8, d = well_d);
}

module boot_reset_well_cutters() {
    plunger_well_cutter(boot_cx, boot_cy);
    plunger_well_cutter(reset_cx, reset_cy);
}

// Header through-slots. Bottom uses the larger outer outline.
// USB-end Y is short of pcb_y so a web keeps the +Y corner attached.
module header_side_cutters(x_outset = 0, usb_web = 0) {
    cut_w = 6 + wall_t + x_outset + 1;
    cut_y0 = hdr_open_y0;
    cut_len = hdr_open_y - usb_web;
    translate([case_x0 - 6 - x_outset, cut_y0, z_floor - 1])
        cube([cut_w, cut_len, 50]);
    translate([case_x1 - wall_t, cut_y0, z_floor - 1])
        cube([cut_w, cut_len, 50]);
}

module top_header_cutters() {
    // Open the skirt from the PCB top up to the lid plate — do not slice
    // the roof. CN4 also opens the extra 2 mm drop except a pin shelf at
    // the LED end.
    cut_w = 6 + wall_t + 1;
    cut_h = z_lid_in - pcb_t + eps;
    translate([case_x0 - 6, hdr_open_y0, pcb_t])
        cube([cut_w, hdr_open_y, cut_h]);
    translate([case_x1 - wall_t, hdr_open_y0, pcb_t])
        cube([cut_w, hdr_open_y, cut_h]);
    pin_shelf = 2.0;
    translate([case_x1 - wall_t, hdr_open_y0 + pin_shelf, pcb_t - cn4_drop - eps])
        cube([cut_w, hdr_open_y - pin_shelf, cn4_drop + 2 * eps]);
}

module service_edge_bolster(z0, z1) {
    // Inward thicken of the +Y wall. Outer clip face stays at case_y1.
    extra = usb_wall_t - wall_t;
    if (extra > 0.05)
        translate([case_x0, case_y1 - usb_wall_t, z0])
            cube([case_w, extra + wall_t, z1 - z0]);
}

module service_port_bosses() {
    // Extra meat around each lid port; usb_qwiic_cutters punch through after.
    // Z stays in the lid skirt — do not drop a cube through the tray.
    module boss(cx, hole_w) {
        bw = hole_w + 2 * usb_boss;
        translate([cx - bw / 2,
                   case_y1 - usb_wall_t - 2.8,
                   z_split])
            cube([bw, usb_wall_t + 2.8 + 0.6, z_lid_out - z_split]);
    }
    boss(usb_cx, usb_plug_w);
    boss(qwiic_cx, qwiic_open_w);
}

module service_edge_gussets(z_lid) {
    // 45° fillet under the lid along +Y, split around USB / Qwiic holes.
    s = usb_gusset;
    module one_fillet(x0, x1) {
        w = x1 - x0;
        if (w > 1.5) {
            translate([x0, case_y1 - usb_wall_t - s, z_lid - s])
                difference() {
                    cube([w, s, s]);
                    translate([-eps, 0, s])
                        rotate([45, 0, 0])
                            cube([w + 2 * eps, s * 2, s * 2]);
                }
        }
    }
    q0 = qwiic_cx - qwiic_open_w / 2 - usb_boss;
    q1 = qwiic_cx + qwiic_open_w / 2 + usb_boss;
    u0 = usb_cx - usb_plug_w / 2 - usb_boss;
    u1 = usb_cx + usb_plug_w / 2 + usb_boss;
    one_fillet(case_x0 + usb_post_w, q0);
    one_fillet(q1, u0);
    one_fillet(u1, case_x1 - usb_post_w);
    // Solid corner posts — USB-wall clips land on these, not on the ports.
    for (x = [case_x0, case_x1 - usb_post_w])
        translate([x, case_y1 - usb_wall_t - 3.2, z_split])
            cube([usb_post_w, usb_wall_t + 3.2, z_lid - z_split]);
    service_port_bosses();
}

// ----- BOTTOM (pocket tray — top clicks into this; print floor down) --------
module bottom_solid() {
    // Outer pocket. The top drops into this; walls go up to the lid outer.
    difference() {
        union() {
            translate([0, 0, z_floor])
                linear_extrude(floor_t)
                    case_outline_2d(bot_outset, outer_r);
            difference() {
                translate([0, 0, z_floor])
                    linear_extrude(z_lid_out - z_floor)
                        case_outline_2d(bot_outset, outer_r);
                translate([0, 0, z_floor - eps])
                    linear_extrude(z_lid_out - z_floor + 2 * eps)
                        case_outline_2d(nest_clear, max(0.4, outer_r - wall_t));
            }
        }
        header_side_cutters(bot_outset, hdr_usb_web);
        bottom_c6_floor_window();
        translate([island_x0 - fit_gap, hdr_open_y0, z_floor - 1])
            cube([hdr_pin_inset + 0.4 + fit_gap, hdr_open_y - hdr_usb_web, floor_t + 3]);
        translate([island_x1 - hdr_pin_inset - 0.4, hdr_open_y0, z_floor - 1])
            cube([hdr_pin_inset + 0.4 + fit_gap, hdr_open_y - hdr_usb_web, floor_t + 3]);
        // Lid well: from z_lid_in up, open to the top outline so the lid
        // plate sits on a ledge and clicks in.
        translate([0, 0, z_lid_in - eps])
            linear_extrude(lid_t + 2)
                case_outline_2d(nest_clear, max(0.4, outer_r - wall_t));
    }
}

module top_clips() {
    xs_led = [case_x0 + clip_inset, case_x1 - clip_inset - clip_w];
    for (x = xs_led) {
        translate([x + clip_w, case_y0, z_clip0])
            rotate([0, 0, 180]) clip_male();
    }
    xs_usb = [case_x0 + clip_inset_usb,
              case_x1 - clip_inset_usb - clip_w_usb];
    for (x = xs_usb) {
        translate([x, case_y1, z_clip0])
            scale([clip_w_usb / clip_w, 1, 1]) clip_male();
    }
}

module bottom_clip_windows() {
    xs_led = [case_x0 + clip_inset + clip_w / 2,
              case_x1 - clip_inset - clip_w / 2];
    for (x = xs_led) {
        translate([x, case_y0 - bot_outset / 2, z_hook])
            clip_window_cutter();
    }
    xs_usb = [case_x0 + clip_inset_usb + clip_w_usb / 2,
              case_x1 - clip_inset_usb - clip_w_usb / 2];
    for (x = xs_usb) {
        translate([x, case_y1 + bot_outset / 2, z_hook])
            scale([clip_w_usb / clip_w, 1, 1]) clip_window_cutter();
    }
}

module bottom() {
    difference() {
        bottom_solid();
        bottom_service_window_cutter();
        header_side_cutters(bot_outset, hdr_usb_web);
        bottom_clip_windows();
    }
}

// ----- TOP (deep lid, print outer-face on the bed) --------------------------
module top_shell() {
    difference() {
        union() {
            translate([0, 0, z_lid_in])
                linear_extrude(lid_t)
                    case_outline_2d(0, outer_r);
            difference() {
                // Skirt to PCB top, except CN4 hangs cn4_drop lower onto the gold pins.
                translate([0, 0, pcb_t - cn4_drop])
                    linear_extrude(z_lid_out - (pcb_t - cn4_drop))
                        case_outline_2d(0, outer_r);
                translate([0, 0, pcb_t - cn4_drop - eps])
                    linear_extrude(z_lid_out - (pcb_t - cn4_drop) + 2 * eps)
                        case_outline_2d(-wall_t, max(0.4, outer_r - wall_t));
                // Shave the extra 2 mm off every edge except the CN4 wall.
                translate([case_x0 - 8, case_y0 - 8, pcb_t - cn4_drop - eps])
                    cube([case_w - wall_t + 8, case_d + 16, cn4_drop + eps]);
            }
            service_edge_bolster(pcb_t, z_lid_out);
            service_edge_gussets(z_lid_in);
            top_clips();
        }
        top_header_cutters();
        usb_qwiic_cutters();
        c6_rf_window_cutter();
        boot_reset_well_cutters();
        for (i = [-1, 0, 1])
            translate([usb_cx + usb_plug_w / 2 + 2.2, case_y1 - 1.0, z_lid_out - 0.5])
                translate([0, i * 1.4, 0])
                    cube([1.2, 0.8, 1.2], center = true);
    }
}

module top() {
    top_shell();
}

// ----- BOOT / RESET plunger (print pad on bed; same 5.4 mm tap, two copies) --
module tact_plunger() {
    // Flange against inner lid; nub must reach sw_h even if the print is a
    // layer tall. Old 7 mm clips never locked, so the lid sat high and the
    // short nub missed the stem.
    nub_z = inner_h - sw_h + sw_travel + sw_preload;
    union() {
        cylinder(h = rst_pad_h, d = rst_pad_d);
        translate([0, 0, -lid_t - 0.1])
            cylinder(h = lid_t + 0.2, d = rst_stem_d);
        translate([0, 0, -lid_t - rst_flange_t])
            cylinder(h = rst_flange_t, d = rst_flange_d);
        translate([0, 0, -lid_t - rst_flange_t - nub_z])
            cylinder(h = nub_z + 0.2, d = rst_stem_d);
    }
}

// ----- print orientations ---------------------------------------------------
module top_for_print() {
    // Outer lid flat on the bed. Skirt prints up. No ridge, nothing floating.
    translate([0, 0, z_lid_out])
        rotate([180, 0, 0])
            top();
}

module bottom_for_print() {
    translate([0, 0, -z_floor])
        bottom();
}

module plunger_for_print() {
    translate([0, 0, rst_pad_h])
        rotate([180, 0, 0])
            tact_plunger();
}

module plate() {
    translate([0, 0, 0]) bottom_for_print();
    translate([case_w + 8, 0, 0]) top_for_print();
    translate([0, -case_d / 2 - 16, 0]) plunger_for_print();
    translate([22, -case_d / 2 - 16, 0]) plunger_for_print();
}

// ----- preview ghost --------------------------------------------------------
module pcb_ghost() {
    color("red", 0.85)
        translate([0, 0, 0]) cube([pcb_x, pcb_y, pcb_t]);
    color("black", 0.9) {
        cube([hdr_w, hdr_len, pcb_t + hdr_above]);
        translate([pcb_x - hdr_w, 0, 0])
            cube([hdr_w, hdr_len, pcb_t + hdr_above_cn4]);
        translate([0, 0, -hdr_below]) cube([hdr_w, hdr_len, hdr_below]);
        translate([pcb_x - hdr_w, 0, -hdr_below])
            cube([hdr_w, hdr_len, hdr_below]);
    }
    color("silver")
        translate([c6_x, c6_y + ant_l, pcb_t])
            cube([c6_w, c6_l - ant_l, c6_h]);
    color("black")
        translate([c6_x, c6_y, pcb_t])
            cube([c6_w, ant_l, c6_h * 0.4]);
    color("silver")
        translate([usb_cx - usb_shell_w / 2,
                   pcb_y - usb_shell_d + usb_overhang, pcb_t])
            cube([usb_shell_w, usb_shell_d, usb_shell_h]);
    color("beige")
        translate([qwiic_cx - qwiic_w / 2,
                   pcb_y - qwiic_d + qwiic_overhang, pcb_t])
            cube([qwiic_w, qwiic_d, qwiic_h]);
    color("dimgray") {
        translate([reset_cx, reset_cy, pcb_t])
            cube([sw_body, sw_body, sw_h - 1.2], center = true);
        translate([reset_cx, reset_cy, pcb_t + sw_h - 1.2])
            cylinder(h = 1.2, d = sw_stem_d);
        translate([boot_cx, boot_cy, pcb_t])
            cube([sw_body, sw_body, sw_h - 1.2], center = true);
        translate([boot_cx, boot_cy, pcb_t + sw_h - 1.2])
            cylinder(h = 1.2, d = sw_stem_d);
    }
}

module preview() {
    pcb_ghost();
    color("white", 0.45) bottom();
    color("white", 0.45) top();
    color("orange")
        translate([boot_cx, boot_cy, z_lid_out])
            tact_plunger();
    color("orange")
        translate([reset_cx, reset_cy, z_lid_out])
            tact_plunger();
}

if      (part == 1) top_for_print();
else if (part == 2) bottom_for_print();
else if (part == 3) plunger_for_print();
else if (part == 4) plunger_for_print();
else if (part == 5) plate();
else                preview();
