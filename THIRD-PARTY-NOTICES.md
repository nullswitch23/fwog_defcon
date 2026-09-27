# Third-party notices

This project is dual licensed — see [LICENSE](./LICENSE) and [NOTICE](./NOTICE).
It vendors or adapts the components below, each of which remains governed
**solely by its own license**.

**These components are not subject to the FreeWili hardware field-of-use
condition in LICENSE Section A, and do not require a commercial license under
Section B.** Their licenses are permissive and grant you rights this project's
license cannot narrow. None imposes a copyleft obligation on this project or on
yours.

Full license text is retained in the corresponding source files; the summaries
here are a map, not a substitute.

## FatFs — `bsp/main_cpu/fs/ff/`

Generic FAT filesystem module by ChaN, vendored unmodified except for
`ffconf.h` configuration.

- Copyright (C) 2022, ChaN
- License: BSD-1-Clause (a one-clause "retain this notice" permissive license)
- Upstream: <http://elm-chan.org/fsw/ff/>
- License text: header comment of `bsp/main_cpu/fs/ff/ff.h` and
  `bsp/main_cpu/fs/ff/00readme.txt`

## CIC decimator — `bsp/common/dsp/cic.{c,h}`

3rd-order CIC filter for PDM-to-PCM decimation, adapted from wilibsp (the
FreeWili 2 BSP) with the core arithmetic transcribed unchanged. Same copyright
holder as this project, so the MIT notice below and this project's copyright
line name the same person.

- License: MIT
- Upstream: <https://github.com/freewili/wilibsp>, `bsp/dsp/cic.{c,h}`
- Provenance and the scope of the adaptation: header comment of
  `bsp/common/dsp/cic.h`

## Pico SDK board header — `bsp/boards/freewili_og.h`

Derived from the Raspberry Pi Pico SDK board-header pattern.

- Copyright (c) Raspberry Pi (Trading) Ltd.
- License: BSD-3-Clause (`SPDX-License-Identifier: BSD-3-Clause`, declared in
  the file)

## LVGL — `bsp/display_cpu/lvgl/lv_conf.h`

`lv_conf.h` is derived from LVGL's own `lv_conf_template.h`, edited for this
board. It is the only LVGL-authored material in this repository.

- Copyright: LVGL Kft — License: MIT
- Upstream: <https://github.com/lvgl/lvgl>, developed against **v9.2.2**
- What is set for this board, and why: the header comment of that file

**LVGL itself is NOT vendored here.** `bsp/display_cpu/lvgl/fwog_lvgl.c` is a
port — glue between LVGL and the ST7789 panel and the five buttons — and the
consuming project supplies the library. See `bsp/display_cpu/lvgl/fwog_lvgl.h`.

## iCE40 FPGA bitstream — `bsp/main_cpu/fpga/fpga_default_v5.bin`

The default gateware image for the board's iCE40 FPGA, redistributed from the
FreeWili 1 release firmware. It is a binary source artifact, not a build
output, and is deliberately not git-ignored.

- Copyright: Intrepid Control Systems, Inc. (FreeWili) — this is product
  gateware redistributed from the shipping FreeWili 1 firmware, not work
  authored in this repository.
- Provenance, and why it must not be substituted: header comment of
  `bsp/main_cpu/fpga/fpga_bitstream.h`
- Its SHA-256 is pinned by `tools/tests/test_fpga_bitstream.py`

## wasm3 — `apps/diskglass/third_party/wasm3/`

WebAssembly interpreter used by DiskGlass to run stock OG `/scripts/*.wasm`.

- Copyright (c) 2019 Steven Massey, Volodymyr Shymanskyy
- License: MIT
- Upstream: <https://github.com/wasm3/wasm3> tag **v0.5.0**
- License text: `apps/diskglass/third_party/wasm3/LICENSE`
- What was copied: `apps/diskglass/third_party/wasm3/README.md`

## Project Nayuki QR Code generator — `apps/lanferry/qrcodegen.{c,h}`

QR rendering used by LanFerry.

- Copyright (c) Project Nayuki
- License: MIT
- Upstream: <https://www.nayuki.io/page/qr-code-generator-library>
- License text: header comment of `apps/lanferry/qrcodegen.h`

## Not vendored, deliberately

Noted here because the driver headers mention them and a reader may otherwise
go looking:

- **OpenPDMFilter** (STMicroelectronics, Apache-2.0) — evaluated for the PDM
  path and declined in favour of the CIC above. No ST code is present.
- **pdm_microphone** (Arm Ltd, Apache-2.0) — the PIO/DMA capture library the
  original C++ firmware wrapped. This BSP reimplements the capture path; no
  Arm code is present.
