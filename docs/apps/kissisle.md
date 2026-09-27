# KissIsle

**Status:** retired from the product catalog. **Fit was:** native (KISS TNC
on the CC1101s). Last: 2026-09-23.

This page is the design note for a USB KISS modem so a host running
[Reticulum](https://reticulum.network/) could put ISM packets on the OG's
CC1101s. It is **not** an app in `apps/`, is not in the catalog table, and
will not be built in the current pass. [ISMburst](ismburst.md) is the ISM
OOK tool that exists.

It is **not** an RNode, and it will not talk to LoRa RNodes on the air.

## Keep-test

1. **Law.** Encrypted mesh for a network you are allowed to run — field
   comms, a lab, an authorized assessment. Same ISM / amateur-radio rules
   as TwinFox and FobReplay. This is not a jammer.
2. **Silicon.** Half-duplex ≥ 500 bit/s and a 500-byte MTU is what
   Reticulum asks for. CC1101 packet mode plus USB CDC can do that if
   firmware fragments on the air. LoRa / Semtech SX12xx is what RNode
   needs; this board does not have it. The iCE40 bitstream is frozen, so
   it will not grow a LoRa PHY.
3. **Remainder.** A KISS TNC with a front-panel radio page is a real
   tool. Running full RNS on the RP2040 is not the first cut.

## What Reticulum actually wants

From [reticulum.network/hardware.html](https://reticulum.network/hardware.html)
and the [Communications Hardware](https://reticulum.network/manual/hardware.html)
chapter:

| Their interface | On the OG |
|---|---|
| LoRa using **RNode** | **No.** RNode firmware is SX126x / SX127x / SX1280 on ESP32 or nRF52. Two CC1101s are FSK/OOK/ASK packet radios at ~10 dBm. |
| **KISS** / packet-radio TNC | **Yes.** `KISSInterface` over the main CPU's USB CDC. |
| Serial line | Same CDC, if you ever want `SerialInterface` instead of KISS. |
| Wi-Fi / Ethernet / UDP / TCP | **Bottlenose only.** ESP32-C6 can be a Wi-Fi hop. AutoInterface wants a bridged Ethernet L2; a SoftAP is usually IP/NAT, so **UDPInterface** or **TCPClientInterface** is the honest C6 path (LanFerry already speaks AP + UART to the module). |
| Full RNS on the microcontroller | Possible in other projects (microReticulum, MicroPython-RNS). 264 KB RAM and this C BSP make it a later experiment, not v001. |

Minimum physical channel: half-duplex, **> 500 bit/s**, **500-byte MTU**.
CC1101 data rates in the 1.2–38.4 kbit/s range clear the throughput bar.
The MTU bar does not: `cc1101_send_packet()` stuffs **length + payload
into a 64-byte FIFO** (`bsp/main_cpu/radio/cc1101.c`), so one air frame
is about **63 bytes**. Reticulum still hands the TNC a 500-byte packet as
one KISS DATA frame. KissIsle has to split and reassemble on the radio,
the same job RNode firmware does for LoRa's 255-byte limit. Host config
can set `fixed_mtu = 500`; do not ask RNS to speak 63-byte network MTU.

Two KissIsle boards on the same frequency, modulation, and fragment
header can mesh through two hosts. A KissIsle and a LoRa RNode **cannot**
hear each other. Mix them the way the Reticulum manual describes: a host
(or later the C6) that has both a KISS radio interface and a TCP/UDP/Wi-Fi
interface.

## v001 — USB KISS TNC (native)

Flash **`kissisle_main`**. Do not UF2-flash the display half.

| Piece | Job |
|---|---|
| **Main** | CC1101 **packet mode** (2-FSK or GFSK, CRC on), not the OOK async path ISMburst/FobReplay use. KISS on the main USB CDC. Fragment 500-byte KISS DATA into FIFO-sized air frames with a short sequence header; reassemble before KISS-out. `flow_control` on the host — the FIFO is small. Kick the watchdog every loop. |
| **Display** | Frequency, data rate, TX/RX counts, last RSSI, KISS up/down. Gray/red band or rate, green TX-arm / mute, yellow hold home if this ever becomes a KitHome tile. Buttons go through `fwog_power_poll`. |
| **Radio 1** | Listen / RSSI / CCA so TX can wait for a quiet channel. Do not run two independent KISS ports on v001. |
| **Identity** | Lives on the host (`~/.reticulum`). The OG does not generate destinations. |

Host side (not firmware):

```
pip install rns
```

```
[[OG KissIsle]]
  type = KISSInterface
  enabled = yes
  port = COM?          # FWOG main kissisle NNN
  speed = 115200
  flow_control = true
  preamble = 150
  txtail = 10
```

Identify the port the same way `fw.py` does: PID `093C:2054` and product
prefix `FWOG main `. 1200-baud BOOTSEL on that CDC must stay intact —
KISS is a user of the port, not a replacement for it. If a host program
holds the CDC forever, `fw flash` cannot 1200-baud it; unplug the
Reticulum process first.

Default RF: one ISM centre the CC1101 already uses in BandScope/TwinFox
(433.92 or 915, region-picked on the panel), 2-FSK, something like 9.6 or
38.4 kbit/s, ~10 dBm. Document the exact register set in `kissisle.md`
once it is measured. Two boards must match or they are just noise.

**ogemu** can smoke the panel (freq, counters, mute). It cannot prove
KISS or the air.

## What v001 is not

- Not RNode firmware, not `rnodeconf`, not LoRa.
- Not AX.25. Reticulum prefers raw KISS; AX.25 is extra overhead for
  amateur ID if a licence requires it (`AX25KISSInterface` on the host,
  not in this firmware).
- Not LXMF / Sideband / NomadNet on the LCD. Those apps run on the host
  once the interface is up.
- Not occupy-TX / jammer-class. Packet TX of KISS DATA, then idle.
- Not ChirpMail (short text OG-to-OG with no host). If that page ever
  exists, it can share the fragment header; it is a different app.

## v002 — Bottlenose hop (add-on)

Only after LanFerry's `BN HELLO` path is the one you still want to speak.

C6 joins or hosts a Wi-Fi network. Host RNS uses `UDPInterface` or a
`TCPClientInterface` to a known transport. The OG display shows SSID /
link, not a fake Ethernet AutoInterface. This is how a KissIsle ISM
island reaches a laptop that also has an RNode, without pretending the
CC1101 is LoRa.

Do not promise microReticulum on the C6 until someone builds ESP-IDF for
ESP32-C6 against that tree. C6 is RISC-V; most of those ports say ESP32
or ESP32-S3.

## Bring-up order

1. Host tests for KISS encode/decode and 500-byte split/rejoin (no SDK).
2. ogemu script: panel + mute + rate step.
3. Two boards, same room, two PCs: `rnprobe` / a Sideband announce over
   KISS. That is the hardware evidence. One board looping CDC to itself
   does not prove the air.
4. Then consider a KitHome tile.

VERSION starts at **001** when `apps/kissisle/` exists.
