# Bottlenose firmware (ESP32-C6)

Custom image for **LanFerry** (SoftAP file-drop: 124 KiB mailbox, paste board,
multicast pipe), **BattleBridge** (four-browser arena game), **PingHalo**
(BLE scan), and **BleDeck** (BLE HID remote). It does **not** speak
the original FreeWili IO-app Orca protocol.

## Hookup

Power the Orca from the OG 20-pin header. The C6 USB-C port is **debug / flash
only**, not power — leave the OG on while you flash.

| ESP32-C6 | OG (main UART1) | Role |
|---|---|---|
| GPIO16 | GPIO 9 `UART1_RX` | C6 → OG (ROM U0TXD; app TX) |
| GPIO17 | GPIO 8 `UART1_TX` | OG → C6 (ROM U0RXD; app RX) |
| GPIO4 | GPIO 10 `UART1_CTS` | C6 RTS |
| GPIO5 | GPIO 11 `UART1_RTS` | C6 CTS |

App UART matches the C6 ROM pins so OG hardware uart1 APP (TX GPIO8 /
RX GPIO9) works both ways. FPGA SWAP (OG drive GPIO9) did not reach C6
RX on this Orca. Rewrite Bottlenose from PingHalo after this image.

UART is **115200** 8N1, **no CTS/RTS**. 3 Mbaud through the OG FPGA/shifter
was never proven on the header; ROM download on the same GPIO9 worked at
115200. GPIO16/17 data pins are enough.

## Build (ESP-IDF 5.3+)

```
idf.py set-target esp32c6
idf.py build
```

Bottlenose USB-C Serial/JTAG is a **console** for C6 boot logs, not the
PingHalo programming path. Console logs also come out there if you plug
it in after the header UART image is running.

## Flash from the OG

LanFerry, BattleBridge, PingHalo, and BleDeck embed a merged image and program the C6 ROM
bootloader over the header UART. That path is how PingHalo gets firmware
onto Bottlenose; USB-C `idf.py flash` is not required.

The C6 ROM talks UART0 on GPIO16=TX / GPIO17=RX. The app uses the same
pins so PingHalo can stay on uart1 APP after HELLO.

1. Build this IDF project and merge a single image at offset 0:

```
idf.py set-target esp32c6
idf.py build
esptool.py --chip esp32c6 merge_bin -o firmware/bottlenose/fwog_c6.bin \
    --flash_mode dio --flash_freq 80m --flash_size 4MB \
    0x0 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0x10000 build/fwog_bottlenose.bin
```

Run that merge from `firmware/bottlenose/` so the `build/` paths resolve.

2. Rebuild the OG main app you will flash (`lanferry_main`,
   `battlebridge_main`, `pinghalo_main`, or `bledeck_main`) so the merged bin
   is `.incbin`'d into the UF2.
3. On the board: Yellow, hold **BOOT**, tap **RESET**, release BOOT, Green.

Until `fwog_c6.bin` exists, Yellow still opens the helper but write reports
`no C6 image`.

BleDeck pairing on Windows needs this image: the name in the picker is not
enough. After the first bond the C6 **locks** advertising to that peer.
Gray hold on BleDeck (`BN HID FORGET`) wipes C6 NVS bonds and opens pairing
again; also forget a leftover Windows bond (`python tools/bledeck.py --forget`)
if you are switching PCs. `BN HID pair` / `enc` / `lock=` / `disc=` on main
CDC are the handshake log. Pack percent arrives as `BN HID BAT=n` and is
notified on the HID Battery Service.

## What it will not do

It will not run a rogue AP, a deauther, or decrypt Apple Find My payloads.
LanFerry's SSID is `FWOG-ferry`; the WPA2 password is generated on the C6 and
shown on the OG. Gray hold on LanFerry (`BN AP WIPE`) mints a new password,
kicks every station, and empties the RAM mailbox and paste board. PingHalo flags manufacturer ID `0x004C` as "Apple ADV" only.
BleDeck advertises as `FWOG-BleDeck` and sends the five front-panel keys.
