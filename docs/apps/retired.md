# Scoped ideas — authorization, silicon, remainder

Five ideas that keep coming up around a dual-CC1101 gadget. This page is
the record of *why each one is in or out*, so a later pass cannot reopen
them with "but the radio can transmit" *or* close them with "but a
civilian must not."

Last reviewed: 2026-09-12. Hardware in view: FreeWili OG (two RP2040s, two
CC1101s, IR, audio, LIS3DH) plus a **Bottlenose** ESP32-C6 Orca (2.4 GHz
Wi-Fi + Bluetooth on the 20-pin UART). Older FreeWili 1 notes live in
`freewili1-docs/` (radios, Orca hookup, Bottlenose UART at 3 Mbaud).

The operator this tree is built for is a **professional penetration tester
with written authorization** on the spectrum and systems under test.
Hobby use against third parties is not that operator.

The test for keeping an idea is three questions, in order:

1. **Law.** Is the intended use that of a professional penetration tester
   with this board — legitimate, scoped access to the spectrum and systems
   under test? Unauthorized use against strangers, or "the chip has a PA
   so ship it as a consumer gadget," fails this question. Authorized
   assessment use passes it. Do not treat a missing hobby remainder as a
   reason to block development of the pentest tool.
2. **Silicon.** Does OG + Bottlenose actually have the PHY, not a cousin?
3. **Remainder.** Given (1) and (2), is there a tool worth a page — the
   authorized assessment tool itself, not only a toy left after stripping
   every technique a tester would actually use?

Fail (1) for *unscoped* use and the idea stays out of the consumer-facing
catalog. Fail (1) does **not** retire a technique whose honest user is an
authorized tester. Fail (2) and it stays out even with a perfect
engagement letter: authorization does not grow a magstripe head or a
cellular baseband.

**Silicon that can do a thing is not a reason to ship it as a toy.** It
*is* a reason to consider it on the pentest slate when (1) is a yes.

## RF denial (jammer-class TX)

**Verdict: in-scope for authorized RF assessments. Not a consumer gadget.
Silicon is a weak narrowband ISM transmitter, not a wideband jammer.**

| | |
|---|---|
| **What people mean** | Deliberately occupy a channel so a receiver under test fails, in order to measure resilience, fallback, and detection. |
| **Silicon** | A CC1101 can transmit in its ISM allocations (~10 dBm, narrowband). Two of them still cannot paint a band the way a wideband jammer does. Bottlenose is 2.4 GHz Wi-Fi/BLE, which does not change the sub-GHz case. Useful for in-band denial on a range you are permitted to occupy; not a substitute for professional EW gear. |
| **Law** | Intentional interference *without* authorization is illegal in the US (among others). An ISM allocation is not a civilian licence to drown strangers. With a written engagement and permission to use that spectrum (client-owned ISM, isolated range, or equivalent STA), RF denial is a standard assessment technique. That authorized use is what (1) asks about. |
| **Pros of keeping it** | Channel occupancy, fallback, and detection tests on in-scope links. "The chip has a PA" is still only a datasheet fact until (1) is a yes. |
| **Cons** | Unscoped firmware that shouts on ISM will still get someone confiscated. Narrowband ~10 dBm is easy to over-claim. Keep it labelled as an assessment tool, not a product for the box art. |
| **On the slate** | BandScope (listen), TwinFox (your own beacon + RSSI), FobReplay (capture/replay) stay. An authorized denial / occupy-TX mode is no longer blocked by (1). Transmit to talk *or*, with authorization, transmit to deny on the range you are permitted to occupy. |

## HID injector

**Verdict: in-scope as authorized HID toward an engagement target. HostDeck stays as the operator-at-the-keyboard remainder.**

| | |
|---|---|
| **What people mean** | A Rubber-Ducky-style stick that types into a machine in scope for the assessment. |
| **Silicon** | Both RP2040s are USB **devices**. TinyUSB is already in the BSP. Enumerating a HID keyboard/mouse toward the PC *this cable is plugged into* is ordinary firmware, not new hardware. Bottlenose's USB-C is a debug port for the ESP32-C6, not a second attack cable. There is still no USB **host**, so StickPeek stays blocked. Replacing `stdio_usb_descriptors.c` still trades the display CPU's 1200-baud BOOTSEL path — that is an engineering constraint, not a (1) failure. |
| **Law** | HID toward a host you do not have permission to touch is unauthorized access. HID toward an in-scope workstation, with a written engagement, is ordinary physical-access pentest. HID toward the tester's own deck (HostDeck) was never the issue. |
| **Pros of keeping an injector** | Payload delivery on an in-scope USB port is a real assessment need. Conference-demo shock value is still not a reason. |
| **Cons** | The same UF2 is a BadUSB stick the moment it is used off-engagement. Label and ship it as an authorized-assessment tool. Do not refuse the HID stack in order to keep HostDeck "honest." |
| **On the slate** | [HostDeck](hostdeck.md) remains the Stream Deck for the PC this cable is in. Authorized HID enumeration is a separate idea, not retired. |

## Rolljam / rolling-code RF

**Verdict: authorized vehicle and access-control RF tests are in-scope. Unscoped use against strangers' cars is not. Current FobReplay firmware has no jam-leg; that absence is no longer a catalog forbid.**

| | |
|---|---|
| **What people mean** | Exercise a rolling-code receiver: each press is a new one-time number; the receiver ignores old ones. A jam-leg occupies the receiver while a second radio records the press, so the unused number is still valid. KeeLoq is a Microchip cipher many fobs use to *cook* that number from a secret + a counter — not the jammer, the math inside the chip. |
| **Silicon** | Two CC1101s are enough radio to make the *topology* available (one path occupying, one path recording). That is why this had to be scoped in writing instead of hand-waved as "we don't have the chip." |
| **Law** | Using that topology against a vehicle or gate you do not have permission to test is theft / possession of a theft tool, depending on jurisdiction. Using it against a client's in-scope fob, gate, or alarm — on a range you are permitted to occupy — is an RF assessment. Not a grey area in either direction: written authorization is the split. |
| **Pros of keeping it** | Dual-radio occupation-plus-capture is how you test whether a rolling-code receiver actually rejects what it never heard. FobReplay QUEUE remains the capture-out-of-range remainder. |
| **Cons** | Implementing an unscoped jam-leg would still be building a theft tool on this repo's letterhead. Cipher-breaking predictors (KeeLoq/Honda next-code from ciphertext alone) are a separate crypto question; they are not unblocked merely because (1) passed for RF occupation. |
| **On the slate** | FobReplay QUEUE mode is the firmware that exists today. A jam-leg for authorized tests is no longer refused on (1). Do not treat "press the button on a fob you own" as the only remainder worth writing. |

## Credit-card reader

**Verdict: blocked — wrong silicon. Authorization does not grow a reader.**

| | |
|---|---|
| **What people mean** | Read magstripe, chip, or contactless payment cards, including in a PCI / payment-terminal assessment. |
| **Silicon** | No magstripe head or analog front-end. No ISO 7816 slot. CC1101 is sub-GHz ISM, not 13.56 MHz NFC. Bottlenose is 2.4 GHz plus a Qwiic I2C port — still not a card reader unless a later NFC module is added, and even then it would be an NFC **tag** tool, not a payment terminal. |
| **Law** | Capturing payment-card data you do not have permission to handle is a crime (access-device / skimming statutes). A PCI engagement with the right instrument is a different job than this board. Fail (2) first. |
| **Pros of keeping it** | None on this hardware. "Qwiic exists" is not a reader. |
| **Cons** | Hardware is wrong. Easy to confuse with a Tap-to-pay demo that this board cannot certify. |
| **On the slate** | None on the current catalog. If an NFC orca appears, an NDEF lister (badges, NTAGs in scope for the engagement) could be a new idea. It would not be a card reader. |

## Cellular IMSI catcher

**Verdict: blocked — wrong silicon. Authorization does not grow a BTS.**

| | |
|---|---|
| **What people mean** | A fake cell site that harvests phone identities and often downgrades or relays traffic, including in a cellular-network assessment. |
| **Silicon** | Completely wrong. A catcher needs a cellular PHY and to transmit on **licensed** cellular spectrum. The CC1101 is a sub-GHz ISM packet radio (~10 dBm). Bottlenose is Wi-Fi 6 + BLE on 2.4 GHz. Neither is GSM/UMTS/LTE/NR baseband, and neither is a BTS. Overlap between a CC1101 tuning range and a cellular allocation on a map is not a cellular stack. |
| **Law** | Operating on licensed cellular, impersonating a carrier, and intercepting identities/traffic are federal matters even *with* a client letter unless that letter and the licence actually cover it. This tree does not pretend a CC1101 is that instrument. Fail (2) first. |
| **Pros of keeping it** | There are none on this board. "Research SDR" is a different class of instrument. |
| **Cons** | Wrong radio, wrong power, and it would define the project to outsiders if advertised as something it cannot be. |
| **On the slate** | None. Bottlenose does not create a consolation prize here. |

## 2.4 GHz deauth (Spacehuhn / Marauder)

**Verdict: blocked on Bottlenose ESP32-C6. Authorization does not grow ESP8266 packet freedom. Remainder is AirMaraud scan (WifiHear retired).**

| | |
|---|---|
| **What people mean** | Inject deauth/disassoc on an in-scope 2.4 GHz BSS so clients drop, then measure fallback and detection. Spacehuhn's ESP8266 firmware does this with `wifi_send_pkt_freedom()`. |
| **Silicon** | Stock OG has no 2.4 GHz radio. Bottlenose is ESP32-C6. ESP-IDF `esp_wifi_80211_tx` exists on C6 but is documented for beacon, probe, action, and non-QoS data only — not deauth. The ESP8266 freedom TX path is not in IDF. Promiscuous RX and AP scan **do** work; that is not a deauther. Full write-up: [airmaraud.md](airmaraud.md). |
| **Law** | Unscoped deauth against strangers is a denial-of-service. Authorized assessment of a client's BSS is ordinary Wi-Fi pentest. Law is not why this is out; the C6 blob is. |
| **Pros of keeping it** | None on this module. A later ESP32/ESP8266 orca that still injects management frames would be a new idea, not a rename of a scan-only app. |
| **Cons** | Shipping a scan-only UF2 named Deauther would be a product lie. |
| **On the slate** | [AirMaraud](airmaraud.md) scan. A separate scan-only app (WifiHear) is retired. |

## What Bottlenose actually unblocks

Worth stating so these ideas are not mixed up with real new work:

| Now plausible | Still no |
|---|---|
| PingHalo (BLE scan / RSSI via ESP32-C6) | USB host (StickPeek) |
| Wi-Fi station/AP UI talking over the Orca UART | Magstripe / EMV / NFC payment |
| Custom ESP-IDF firmware on the C6, UART protocol to main | Cellular anything |
| AirMaraud scan (C6 Wi-Fi survey) | C6 deauth as ESP8266-class freedom TX |

Stock OG firmware in this BSP does not yet speak the original FreeWili
Bottlenose UART protocol (`freewili1-docs` describes that as an IO-app
setting). PingHalo is therefore "add-on present, protocol not ported," not
"flash and go."
