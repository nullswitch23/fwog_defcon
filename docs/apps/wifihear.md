# WifiHear — 2.4 GHz Wi-Fi survey (retired)

**Status:** **retired** (2026-09-27). Reason: too similar to other apps
or not native — [AirMaraud](airmaraud.md) already scans. Not in the
shipped catalog. This page is the old design note.

**Fit was:** add-on (Bottlenose ESP32-C6). Firmware in `apps/` was never
built. Last specified: 2026-09-12.

A **passive** 2.4 GHz access-point and client survey for an authorized
engagement: Bottlenose hears beacons and probe traffic, the OG shows
start/stop and a target list, the **PC** serves a small HTML dashboard
over USB CDC. It does **not** transmit deauth, disassoc, or rogue
beacons. Naming it honestly is the point; the deauther that will not
fit this C6 is [AirMaraud](airmaraud.md).

## Why this and not a captive portal on the C6

Spacehuhn’s ESP8266 UI is a SoftAP the operator joins with a phone.
On this board that fights LanFerry (the C6 already owns one AP story)
and ignores the OG’s panel. [HostDeck](hostdeck.md) already proved the
better pattern: RP2040 CDC → helper on the authorized PC. WifiHear
uses that for a dashboard, not for typing keys.

Useful lines belong on **main** (`093C:2054`, product `FWOG main wifihear …`),
because UART1 to Bottlenose is a main-CPU peripheral — same as PingHalo
and LanFerry. Display CDC is LCD + buttons. Never open either CDC at
1200 baud ([console.md](console.md)).

## Hardware

Stock OG has no 2.4 GHz Wi-Fi. Bottlenose supplies it. Scan uses ordinary
ESP-IDF `esp_wifi_scan_start` / promiscuous RX. That **does** work on
C6. Injecting deauth does not; see AirMaraud.

## UART protocol (design — not on the C6 yet)

Same newline ASCII style as `firmware/bottlenose/main/main.c`. Add
commands; do not delete `BN AP` / `BN BLE` / `BN HID`.

OG → C6:

```
BN WIFI SCAN START
BN WIFI SCAN STOP
```

C6 → OG (mirrored by main as `DIAG` on its CDC):

```
BN WIFI on
BN WIFI off
BN WIFI AP ssid=Cafe bssid=aabbccddeeff ch=6 rssi=-42 auth=wpa2
BN WIFI STA mac=112233445566 bssid=aabbccddeeff rssi=-55
```

SSID values must not contain spaces (sanitize like LanFerry filenames).
Main reprints each `BN WIFI …` line with a `[wifihear]` prefix, matching
PingHalo. The host parser also accepts a short `WH …` alias.

**Never TX until armed** was the deauther rule. This app has no arm
and no TX. Green starts and stops the scan once `BN HELLO` has been
seen. Red hold still ships.

## Host dashboard

```
python tools/wifihear/wifihear.py
```

Opens the **main** CDC at 115200 (refuses 1200), serves
`http://127.0.0.1:8765/`. `--list`, `--port`, `--bind`, `--demo`.
Details: [`tools/wifihear/README.md`](../../tools/wifihear/README.md).

## What it is not

It is not AirMaraud, not Spacehuhn’s firmware on an ESP8266, not a
way to join or knock people off a network you are not allowed on, and
not LanFerry’s file-drop AP. Beacon metadata in an engagement is a
survey; payloads and portals are a different page.

## Flash (when firmware exists)

`wifihear_main` only. Do not UF2-flash `wifihear_display`. The C6 image
is the same `firmware/bottlenose/` tree, flashed like LanFerry (Yellow
+ BOOT/RESET, or USB-C + `idf.py flash`).
