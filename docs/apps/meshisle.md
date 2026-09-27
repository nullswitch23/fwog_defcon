# MeshIsle

**Status:** idea. **Fit:** blocked (on-air). Last: 2026-09-12.

A Meshtastic node on the OG, talking LongFast to the rest of the mesh.

It is **not** going to happen on this silicon. The board is not a LoRa
radio. Bottlenose does not become one. This page exists so the next pass
does not reopen it with "but we have two CC1101s" or "but the C6 has
BLE."

[KissIsle](kissisle.md) is the cousin that *did* pass: Reticulum has a
KISS / packet-radio interface, so the CC1101s can be a real TNC. Meshtastic
does not have that interface. FSK packets on the CC1101s are not
Meshtastic, and other Meshtastic nodes will not hear them.

## Keep-test

1. **Law.** A mesh you are allowed to run — your own ISM allocation, a
   licensed amateur channel with `is_licensed`, a lab. Same ISM /
   amateur-radio rules as TwinFox and KissIsle. Meshtastic itself is not
   the problem. This is not a jammer.
2. **Silicon.** The air PHY is **LoRa CSS chirps** on a Semtech SX126x /
   SX127x / SX128x / LR11xx, typically 868/915 (or 433), protobuf mesh,
   encrypted payload up to **239 bytes** (~200 byte application). Two
   CC1101s are FSK/OOK/ASK packet radios at ~10 dBm with a **~64-byte
   FIFO**. The iCE40 bitstream is frozen, so it will not grow a LoRa PHY.
   Bottlenose is ESP32-C6 Wi-Fi + BLE, still no LoRa. RP2040 as an MCU is
   fine — Meshtastic already ships `firmware-pico` and `firmware-rak11310`
   — but those builds assume an **SX1262 on the board**, not a CC1101.
3. **Remainder.** A fake LongFast on FSK is not Meshtastic. BLE / MQTT /
   serial clients still need a real LoRa node next to them, and the phone
   already is that client. Not worth firmware.

## What Meshtastic actually wants

From [meshtastic.org](https://meshtastic.org/) (docs 2.8):
[overview](https://meshtastic.org/docs/overview/),
[radio settings](https://meshtastic.org/docs/overview/radio-settings/),
[LoRa config](https://meshtastic.org/docs/configuration/radio/lora/),
[supported devices](https://meshtastic.org/docs/hardware/devices/),
[Pico](https://meshtastic.org/docs/hardware/devices/raspberrypi/pico/),
[T-Lora C6](https://meshtastic.org/docs/hardware/devices/community-supported/lilygo/tlorac6/),
[client API](https://meshtastic.org/docs/development/device/client-api/),
[serial module](https://meshtastic.org/docs/configuration/module/serial/),
[MQTT](https://meshtastic.org/docs/configuration/module/mqtt/),
[Bluetooth](https://meshtastic.org/docs/configuration/radio/bluetooth/).

A mesh at the radio layer is nodes that share LoRa spreading factor,
centre frequency, and bandwidth. The on-air packet is preamble chirps,
sync word `0x2B`, then a Meshtastic header plus encrypted protobuf.
Recommended silicon is SX126x or LR11xx; SX127x is the older generation.
There is no CC1101, no FSK preset, and no KISS TNC in that list.

| Their interface | On the OG |
|---|---|
| LoRa on SX126x / SX127x / SX128x / LR11xx | **No.** Two CC1101s. Frozen FPGA. |
| RP2040 + Waveshare SX1262 (`firmware-pico`) | **No.** Official Pico firmware expects that HAT's pinout. Flashing it on main would fight the iCE40 bitstream load on the same SPI, the CC1101s, `GUI_NRESET`, and the display bootloader contract. |
| RAK11310 (RP2040 + SX1262) | Same MCU class, **wrong radio**, wrong board. |
| LILYGO T-Lora C6 (ESP32-C6 + SX1262) | Bottlenose is the C6 **without** the SX1262. |
| BLE to a phone | Client transport **to** a LoRa node. Stock OG has no BLE. Bottlenose can BLE, but then you still need a Meshtastic radio. |
| Wi-Fi MQTT / TCP :4403 | Gateway or client **on** a LoRa node (ESP32). SoftAP is not supported in Meshtastic firmware. nRF52 and RP2040 have no Network section. |
| Serial / USB protobuf (`ToRadio` / `FromRadio`) | Client transport **to** a LoRa node. TtyGlass can already show UART. It does not put chirps on the air. |
| KISS / CC1101 FSK | **Not an interface they offer.** KissIsle is Reticulum, not Meshtastic. |

`cc1101_send_packet()` writes **length + payload into TXFIFO**
(`bsp/main_cpu/radio/cc1101.c`). One air frame is about **63 bytes**.
Meshtastic's LoRa payload ceiling is **239 bytes**. Fragmenting FSK
frames does not make a chirp radio, and it does not join LongFast.

## Remainders that are not an app

These are real Meshtastic transports. They fail (3): the OG is a worse
phone, and the LoRa node is still someone else's board.

| Path | Why it is not MeshIsle |
|---|---|
| **BLE companion** (Bottlenose as GATT central to a nearby Heltec / T-Beam / T-Echo) | Phone apps already speak `6ba1b218-…` ToRadio/FromRadio. Implementing that protobuf client on the C6 duplicates PingHalo-class firmware for a UI the node often already has. |
| **Wi-Fi MQTT viewer** (Bottlenose STA to `mqtt.meshtastic.org` or a LAN broker) | MQTT is how a **LoRa node** uplinks. A subscriber with no radio is a dashboard. Public LongFast MQTT is also a traffic flood they warn against. |
| **UART TEXTMSG / PROTO** (header UART1 to a node's serial module) | [TtyGlass](ttyglass.md) is that terminal. Bottlenose already owns UART1 at 3 Mbaud. Wiring a Heltec to the header does not make the OG the radio. |
| **SX1262 module on the 20-pin header** | New hardware, and header pin 1 is FPGA CS — same trap as [microsd.md](../hardware/microsd.md). Even with GPIO 27 as CS, you still cannot flash `firmware-pico.uf2` onto this dual-CPU BSP. A Meshtastic port of this tree is a different project, not an `apps/` row. |
| **meshtasticd USB stick** (MeshStick / Tadpole) | Linux + a USB LoRa dongle. The OG is a USB **device**, not a host ([StickPeek](stickpeek.md)). |

Buy a T-Beam, talk to it from the official app. The retired
[KissIsle](kissisle.md) design note is the CC1101 KISS remainder
(Reticulum), not a Meshtastic path.

## What we will not build

- Not Meshtastic firmware on either RP2040.
- Not LongFast, ShortFast, or any other LoRa preset on the CC1101s.
- Not `firmware-pico` / `firmware-picow` / `firmware-rak11310` UF2s on
  this board.
- Not a C6 image that pretends Bottlenose is a T-Lora C6.
- Not occupy-TX against someone else's mesh.

VERSION stays **—**. There is no `apps/meshisle/`.
