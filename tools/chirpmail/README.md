# ChirpMail helper

Compose up to **24 × 20-character** canned lines and push them to a
board running `chirpmail_main`.

```
python tools/chirpmail/chirpmail.py
python tools/chirpmail/chirpmail.py --file tools/chirpmail/canned.txt push
```

The board must be running **`chirpmail_main`**. Push is ASCII `CM1` /
`SLOT` / `END` on that app's main CDC — VoltPet, TwinFox, GlassBak, and
the rest do not parse it. Identify by USB product `FWOG main chirpmail …`
(never 1200 baud). T9 on the OG still overrides a slot (Gray hold, Green
hold to save). FatFs path: `/chirpmail/CANNED.TXT`.
