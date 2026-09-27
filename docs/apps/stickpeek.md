# StickPeek

**Status:** idea. **Fit:** blocked on stock OG.

Plug into another gadget over USB, try MSC / MTP / PTP / raw SCSI, mount
what answers, and copy a tree off for later review.

## Hardware gap

Both RP2040s enumerate as **USB devices** (CDC) on the board's own USB
jack. That jack is how a PC talks to the OG. It is not a USB-A host
port, and reversing it (charger boost onto VBUS while a computer is
plugged in) is how you fight a host, not how you mount a stick.

## USB host on the GPIO header?

On a bare Raspberry Pi Pico, people do this: two GPIOs, the
`pico-pio-usb` program, TinyUSB in host mode, 15 kΩ pulldowns, and 5 V
for the gadget. Full-speed USB is 12 Mbit/s and the wires have to turn
around every packet.

The OG header is not a Pico header.

- Pin 2 really is **5 V out**, so powering a stick is the easy half.
  Pin 4 (`V PINS`) still has to be jumpered to 3.3 V or 5 V or the
  buffers are dead.
- Data would have to ride **GPIO 26 and 27** (or another pair), through
  the frozen iCE40 `io_buffer` and an **SN74LXC1T45** on each line. That
  shifter is direction-controlled: the FPGA/expander picks IN or OUT
  and leaves it there. USB D+ and D− are a bidirectional differential
  pair. You cannot flip DIR over I2C in the middle of a 12 MHz packet.
- GPIO 26 defaults to IN and GPIO 27 to OUT. `fwog_io_dir_apply()` can
  change that, still one direction at a time, still through the same
  shifter.
- The iCE40 bitstream is frozen. It will not grow a USB PHY. The FT232H
  on that front end is another USB *device* toward a PC (FT1248), not a
  host for a flash drive.

So: PIO-USB on the header is not “wire D+ to pin 14.” It would be
fighting the level shifters. I would not promise it.

A **USB-host add-on that talks SPI** is a different story — a MAX3421E
(or a tiny second RP2040 that does PIO-USB itself and sits on UART/SPI)
is electrically normal on this header. That is an Orca-shaped dongle,
not a firmware-only trick. For extra minutes of [TalkClip](talkclip.md)
or trail logs, a **SPI microSD** is still simpler than hosting a stick.

## What the OG *can* do instead

- Main already has FatFs on the last 8 MB of flash (`fwog_fs`). An app
  can log there and a PC can pull the files over CDC.
- The PC is the right USB host: `fwogcli` / a Python tool talking to
  the board's serial port.

So "review a filesystem later" fits as **OG-as-logger, PC-as-host**, not
OG-as-forensic-dongle.

## Extra disk, in order of sanity

1. The 8 MB FatFs volume, dump to the PC over CDC when it fills.
2. A 3.3 V SPI microSD on the header. Wiring and firmware:
   [docs/hardware/microsd.md](../hardware/microsd.md). Do not use pin 1
   as the card CS (that pin holds the FPGA deselected).
3. A SPI USB-host dongle, if you specifically want a USB stick rather
   than a card.
4. WILEye's card is the camera Orca's, for pictures and video.
