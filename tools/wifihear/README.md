# WifiHear host dashboard (retired)

Stub for a scan-only CDC dashboard. **WifiHear is retired** (too similar
to AirMaraud scan). Kept so the helper is not mistaken for shipped product.

Local HTML view of a Bottlenose **Wi-Fi scan**, read from the OG **main**
CPU USB CDC. It does not transmit, and it is not Spacehuhn’s ESP8266
captive portal.

```
python -m pip install pyserial
python tools/wifihear/wifihear.py
```

Then open `http://127.0.0.1:8765/`. `--demo` serves sample rows without
a board. `--list` prints FreeWili CDC ports. `--port COM12` after the
same main-CPU checks.

Identification matches `tools/fw.py`: PID **093C:2054** (main) first,
then the `FWOG main ` product prefix. Display (`093C:2055`) is refused —
survey lines are the C6 UART mirrored by main, same as PingHalo.

Opens at 115200. **Refuses 1200** (BOOTSEL).

Expected CDC lines (firmware not in `apps/` yet):

```
WH on
WH AP ssid=Lab-AP bssid=aabbccddeeff ch=6 rssi=-41 auth=wpa2
WH STA mac=112233445566 bssid=aabbccddeeff rssi=-50
[wifihear] BN WIFI AP ssid=Lab-AP bssid=aabbccddeeff ch=6 rssi=-41 auth=wpa2
```

Protocol: [docs/apps/wifihear.md](../../docs/apps/wifihear.md). Why this
is not a deauther: [docs/apps/airmaraud.md](../../docs/apps/airmaraud.md).
