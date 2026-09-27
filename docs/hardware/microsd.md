# Adding a microSD card to FreeWili OG

The OG has no card socket. Main already runs FatFs on the last 8 MB of
its own flash (drive `0:`). A microSD is a **second SPI disk on the
20-pin header**, the same idea original FreeWili firmware had as drive
`1:` before this BSP dropped that volume.

It is an add-on: an Orca-style header sandwich, then a disk driver.
It is not a USB stick and it does not use the USB jack. DiskGlass
(front-panel browser of that onboard flash) stays on `0:`. This board
is volume `1:` when the driver exists.

**Verdict: buildable.** SPI on this header is unidirectional-enough
per line (CLK/MOSI/CS out, MISO in) if `fwog_io_dir_apply()` sets DIR
that way. That is the opposite of USB D+/D− on the same shifters —
see [stickpeek.md](../apps/stickpeek.md). Do not hang card CS on pin 1.

## The one wiring rule that matters

**Do not use header pin 1 (SPI chip-select) as the card’s CS.**

That pin is GPIO 13. After every boot, main clocks the iCE40 bitstream
out over the same SPI bus and then **holds that CS high on purpose**
so the FPGA stays deselected. The BSP comment in `ice40.c` is blunt:
that header pin is not a free user chip-select. If you hang the card
there, selecting the card also talks to the FPGA’s SPI-slave, and
FPGA configuration traffic at boot (about 170 ms of clocks with CS
low) goes into the card.

This still applies on Bottlenose **CN2**. That port lists pin 1 as
“available for expansion.” It is still FPGA CS. Use pin 3.

Use **GPIO 27 / header pin 3** as the card’s chip-select. Leave pin 1
alone (FPGA, idle high). The data pins can be shared: SCLK, MOSI, and
MISO are released to the header after the bitstream is in.

## Power and voltage

microSD is **3.3 V**. 5 V on the I/O pins will kill a card.

1. Jumper header **pin 4 (`V PINS`) to pin 6 (3.3 V)**. Without that
   jumper the SN74LXC1T45s have no I/O rail and the bus is dead. The
   sandwich BOM below hard-ties 4–6 on the PCB so this is not a
   flying jumper you can forget. Bottlenose also auto-ties pin 4 to
   3.3 V; both ties on the same net are fine.
2. Feed the socket from **pin 6 (3.3 V)** and **pin 19 or 20 (GND)**.
   Pin 2 is 5 V out — do not wire it to the card. No extra LDO is
   required.

## Header pitch (verified, not guessed)

The OG breakout is a **2×10, 0.100″ / 2.54 mm** shrouded box header
(IDC numbering: odd pins 1–19 on one row, even 2–20 on the other,
notch between 10 and 12). Confirmed from:

- the end-on photo in `freewili1-docs/docs/assets/gpio-header.png`
- the pinout diagram in `gpio-pinout.md`
- Bottlenose docs calling CN2 a 2×10 with the same pin numbers

It is **not** 2 mm. Mating parts are ordinary 0.1″ dual-row.

Pin numbers below match `bsp/common/io_pins.c` and
`freewili1-docs/docs/gpio/gpio-pinout.md`.

## Suggested pin map (card nets)

| Card / module | OG header | Main GPIO | Notes |
|---|---|---|---|
| GND | 19 or 20 | — | |
| 3.3 V | 6 | — | And jumper 4–6 |
| CLK | 15 | 14 | SPI1 SCLK, main drives |
| MOSI / DI | 13 | 15 | SPI1 TX, main drives |
| MISO / DO | 12 | 12 | SPI1 RX, main receives |
| CS | **3** | **27** | Dedicated CS; default is already an output |
| CD (optional) | 14 | 26 | Default is an input |

Any cheap “SPI microSD” breakout is still fine on a bench. Avoid
“SDIO only” boards that do not bring out MOSI/MISO/SCLK. The
sandwich below is the thing you actually build.

## Passthrough pin map (this board vs the header on top)

Every pin is carried through plated holes to the far-side 20-pin so
unused functions stay a header, the Bottlenose idea. “This board
uses” means the SD socket (or the 4–6 rail tie, or optional Qwiic)
is also soldered to that net. Used pins are **still present on top**;
whatever plugs in must not drive them.

| Pin | OG net | This board uses? | Still on top? |
|---|---|---|---|
| 1 | SPI1 CS (GPIO 13, FPGA) | no — **do not** as card CS | yes, idle-high FPGA CS |
| 2 | 5 V out | no (not card VDD) | yes |
| 3 | GPIO 27 out | **yes — card CS** | yes, do not drive |
| 4 | V PINS in | **yes — tied to pin 6** | yes (3.3 V) |
| 5 | UART1 RX (GPIO 9) | no | yes — Bottlenose |
| 6 | 3.3 V out | **yes — card VDD** | yes |
| 7 | UART1 CTS (GPIO 10) | no | yes — Bottlenose |
| 8 | I2C0 SCL (GPIO 17) | optional Qwiic only | yes |
| 9 | UART1 TX (GPIO 8) | no | yes — Bottlenose |
| 10 | I2C0 SDA (GPIO 16) | optional Qwiic only | yes |
| 11 | UART1 RTS (GPIO 11) | no | yes — Bottlenose |
| 12 | SPI1 MISO (GPIO 12) | **yes — card DO** | yes, do not drive |
| 13 | SPI1 MOSI (GPIO 15) | **yes — card DI** | yes, do not drive |
| 14 | GPIO 26 in | **yes — card CD** (optional) | yes, do not drive |
| 15 | SPI1 SCLK (GPIO 14) | **yes — card CLK** | yes, do not drive |
| 16 | SWCLK | no | yes |
| 17 | GPIO 25 (board LED) | no — leave the LED | yes |
| 18 | SWDIO | no | yes |
| 19 | GND | **yes** | yes |
| 20 | GND | **yes** | yes |

SPI is shared with the FPGA only at boot, and only while pin 1 is
low. Card CS on pin 3 plus a 10 kΩ pull-up to 3.3 V keeps the card
deaf during that 170 ms. `board_init_pins()` does **not** park GPIO
27; the resistor is the backstop until `fwog_io_dir_apply()`.

## Stack order with Bottlenose

Bottlenose already does this job for UART: it plugs onto the OG
20-pin, uses pins 5/7/9/11 plus power/GND, auto-sets V PINS to
3.3 V, and exposes the rest on female **CN2**. Its Qwiic is I2C0
(pins 8/10). See
`freewili1-docs/docs/extending-with-orcas/bottlenose-wifi-orca/bottlenose-hardware-hookup.md`.

**Use: OG → SD sandwich → Bottlenose.**

1. SD female 2×10 plugs onto the OG shrouded male.
2. Far side of the SD board is a **matching shrouded 2×10 male** so
   Bottlenose (and any other Orca) still keys as if it were on the OG.
3. Put the microSD socket on the **side** of the PCB, right-angle,
   so Bottlenose sitting on the far connector does not block the card.
4. UART pins are untouched traces. Bottlenose still talks 3 Mbaud
   with CTS/RTS. SPI/CS/CD are tapped for the socket and still
   appear on top; Bottlenose does not drive them.

Do **not** put Bottlenose on the OG first and hope leftover pins
are reachable. Bottlenose occupies the physical header. CN2 is a
female, so a sandwich whose OG face is also female cannot plug into
it. If Bottlenose is already on the board, unplug it, insert the SD
sandwich, put Bottlenose on the far male.

A CN2-only dongle (male 2×10 into Bottlenose, socket on that board,
CS still on pin 3) is a different, smaller PCB for people who
always wear Bottlenose. It is not a substitute for the sandwich if
you also want SD without the C6.

Height, ballpark: 3M 8520 female is 10.80 mm insulation, PCB
1.6 mm, 3M N2520 shrouded male 9.91 mm → about **22 mm** of
connector stack plus the card sticking out the side. Check that
Bottlenose USB-C, BOOT, and RESET still clear whatever enclosure
you use. A snap-together Orca-only case (BOOT hold slider, RESET tap)
is in [orca-case/](orca-case/).

## DigiKey BOM (checked 2026-09-12)

One board. Prices are unit, that day, and will move. Prefer
cut-tape / “1” qty for a hobby build. The Hirose socket and the
JST-SH are SMT (hot plate or a stencil); the 2×10s are through-hole.

| Qty | DigiKey / mfr PN | Role | Notes |
|---|---|---|---|
| 1 | [3M **8520-4500PL**](https://www.digikey.com/en/products/detail/3m/8520-4500PL/1306249) | Female 2×10 0.1″, plugs onto OG | 20 pos, 2 row, TH. ~19 k factory stock, ~$2.40. Insulation 10.80 mm. Same family as the OG box header. |
| 1 | [3M **N2520-6002-RB**](https://www.digikey.com/en/products/detail/3m/N2520-6002-RB/755179) | Shrouded male 2×10 0.1″ on the far side | 4-wall, 20 pos, TH. ~1.9 k stock. This is the OG-shaped header Bottlenose expects. |
| 1 | [Hirose **DM3AT-SF-PEJM5**](https://www.digikey.com/en/products/detail/hirose-electric-co-ltd/DM3AT-SF-PEJM5/2533565) (DigiKey **HR1964CT-ND** cut tape) | microSD push-push, 3.3 V, SPI | SMT right-angle, 8 contacts + 2 CD switch, 1.68 mm above board. ~$3.55. Not SDIO-only. |
| 5 | [YAGEO **RC0603FR-0710KL**](https://www.digikey.com/en/products/detail/yageo/RC0603FR-0710KL/726880) | 10 kΩ 0603 1 % | Pull-ups: CS (pin 3), MISO, DAT1, DAT2, CD. |
| 1 | [Samsung **CL10B104KB8NNNC**](https://www.digikey.com/en/products/detail/samsung-electro-mechanics/CL10B104KB8NNNC/3886658) | 100 nF 50 V 0603 X7R | Socket VDD decoupling. |
| 1 | Samsung **CL10A106KA8NNNC** (search that PN) | 10 µF 0603 bulk on 3.3 V | Any 10 µF / ≥10 V 0603 X5R/X7R substitute is fine. |
| 1 | PCB trace or 0603 0 Ω | Tie header pin 4 to pin 6 | 3.3 V for the shifters and the card. Do not tie pin 2. |

Optional:

| Qty | DigiKey / mfr PN | Role | Notes |
|---|---|---|---|
| 1 | [JST **SM04B-SRSS-TB**](https://www.digikey.com/en/products/detail/jst-sales-america-inc/SM04B-SRSS-TB/926710) | Qwiic / STEMMA QT | 4-pin JST-SH 1.00 mm, SMT RA. ~4 k stock, ~$1.53. SDA=pin 10, SCL=pin 8, 3.3 V=pin 6, GND=pin 19. I2C0 is free after the SD SPI assignment. PCA9517 already buffers that bus; call `board_init_i2c()` and leave the expander pull-ups on. Skip extra 4.7 kΩ. |
| 1 | [Samtec **TSW-110-07-G-D**](https://www.digikey.com/en/products/detail/samtec-inc/TSW-110-07-G-D/1101267) | Unshrouded male 2×10 | Cheaper far-side header (~$1.59) if you will never plug Bottlenose on top. No 4-wall keying. |
| 1 | [Samtec **SSQ-110-03-G-D**](https://www.digikey.com/en/products/detail/samtec-inc/SSQ-110-03-G-D/1111453) | Alternate female 2×10 | 8.51 mm tall. Listed; some days this is Marketplace. Prefer 8520-4500PL. |

No LDO. Header 3.3 V is the rail. Pin 2 exists if some *other* stacked
module wants 5 V; the card must not see it.

Hirose DM3AT SPI wiring (socket pin → OG header): CMD→13, DAT0→12,
CLK→15, CD/DAT3→3, VDD→6, VSS→19, DAT1 and DAT2 pulled to 3.3 V,
CD switch →14. Confirm the switch closure against the current
datasheet (card-in typically shorts the two CD pads).

## What firmware has to do

Bring-up order is the same as any main app, plus the header:

1. `board_init()` — clocks, watchdog, FPGA bitstream. Until this
   finishes, MOSI/SCLK will have carried configuration traffic. Keep
   the card’s CS high (GPIO 27 plus the 10 kΩ) so the card ignores it.
2. `fwog_display_update_run()` — display has to be running, because
   level-shifter direction lives on the display’s PCAL6416.
3. `fwog_io_dir_apply()` with the **default** SPI directions (MOSI /
   SCLK / CS out, MISO in, GPIO 27 out, GPIO 26 in). That is the
   three-way agreement: FPGA `io_buffer`, expander, RP2040 pads. Do
   not skip it. After `board_init()` the SPI pads are floating inputs
   until this call.
4. Put SPI1 back on those pins (`spi_init` on `FWOG_FPGA_SPI`,
   GPIO_FUNC_SPI on 12/14/15). CS for the card is a plain GPIO (27),
   idle high, same pattern the FPGA already uses for pin 1.
5. SD SPI init at **400 kHz**, SPI mode 0. After the card is in,
   raise the clock; through the shifters, a few megahertz is a
   realistic target, not 25 MHz.

FatFs already allows mixed sector sizes (`FF_MIN_SS` 512, `FF_MAX_SS`
4096): flash is 4 KB sectors, SD is 512. What this BSP changed is
`FF_VOLUMES` 2 → 1 and it never took the reference `diskio.c` SD
path. Restoring the card means:

- set `FF_VOLUMES` back to 2
- in `disk_read` / `disk_write` / `disk_initialize`, `pdrv == 0` stays
  the flash volume; `pdrv == 1` is the card
- mount as `"1:"` (or a named volume)
- kick the watchdog on long multi-block writes, same reason flash
  already does
- still **one open file at a time** unless you grow `fwog_fs` — that
  is a RAM budget, not an SD issue

ChaN’s generic `mmc_spi` / `sd_spi` example is the usual recipe for
CMD0 / CMD8 / ACMD41. Do not bit-bang SDIO on these pins.

**DiskGlass hook (do not steal that app).** Onboard flash remains
`0:`. When DiskGlass grows a second volume, the SPI driver above is
`pdrv == 1`. Implement the `diskio` cases there; do not invent a
second FatFs tree.

## Leftover limitations

- UART1 (pins 5/7/9/11) is Bottlenose. With the C6 on top you do not
  also get TtyGlass on those pins.
- Pin 1 is never a user CS, on OG or on CN2.
- Pin 17 is the main-CPU status LED. Pass it through; do not reuse it
  as a second CD/WP.
- SWD (16/18) is pass-through only. Do not put a debugger and an SD
  card detect on the same net without thinking.
- Qwiic on this board and Qwiic on Bottlenose are the **same** I2C0.
  Two connectors in parallel is normal. Addresses still have to be
  unique.
- Speed is SPI through SN74LXC1T45s, not a 4-bit SD host. Fine for
  logs and TalkClip WAVs. Not a camcorder.
- One FatFs file at a time until `fwog_fs` grows RAM.

## What can go wrong

- No 4–6 jumper: silent dead bus. The sandwich should make this a
  copper pour, not a flying wire.
- CS on pin 1: FPGA and card fight; boot stream corrupts the card.
- 5 V I/O jumper: dead card.
- Talking SPI before `fwog_io_dir_apply()`: shifters pointing the
  wrong way, or outputs fighting.
- Expecting USB-stick speeds: this is SPI through level shifters.
- Stacking SD *on* Bottlenose CN2 with a female-in sandwich: two
  females, no mate. Unplug Bottlenose, SD in the middle, Bottlenose
  on the far male.

TalkClip and InertialTrail can keep using flash as the small always-
there disk and treat `"1:"` as the walk-sized volume once this exists.
