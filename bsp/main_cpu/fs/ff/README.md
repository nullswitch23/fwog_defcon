# Vendored FatFs R0.15

**This is third-party code. It is the first third-party dependency this BSP has
taken.** That is worth being explicit about rather than letting it arrive
quietly, which is why this file exists.

- **Upstream:** FatFs by ChaN, revision **R0.15** (`ffconf.h`'s
  `FFCONF_DEF 80286`).
- **Copied from:** `freewilimain/fatfsLib/` in the reference FreeWili firmware
  tree, **not** fetched from upstream.

## Why copied from the reference rather than fetched

So the two cannot drift across a revision boundary. FatFs revisions have
changed on-media behaviour before, and this volume's whole reason for being
FatFs rather than LittleFS is that a board can hold files written by either
firmware (see `../fs_geom.h`). Fetching "R0.15" from somewhere else later, or
letting a package manager pick a newer revision, would put that compatibility
at the mercy of a version string.

## Files taken

`ff.c`, `ff.h`, `ffunicode.c`, `ffsystem.c`, `ffconf.h`, `diskio.h`, plus
upstream's `00readme.txt` / `00history.txt` for provenance.

**Not taken:** `diskio.c`. The reference's is written against its own board and
its `WiliGlobLockFlash()` hooks; this BSP's replacement is
`../fs_flash_disk.c`, which is where the RP2040 hazards are handled. Also not
taken: `CMakeLists.txt` and `pico_sdk_import.cmake` — this tree does its own
build wiring in `bsp/CMakeLists.txt`.

## Changes made — there are exactly two

### 1. `ffconf.h`: `FF_VOLUMES` 2 → 1

The reference's drive 1 is an SD card, guarded by `SUPPORT_SDCARD`, which the
classic build does not define. The OG has no SD card. Carrying a phantom
volume would leave dead code and a `"1:"` path prefix that can never mount.

Everything else in `ffconf.h` is carried over unchanged, deliberately:
`FF_USE_LFN 1`, `FF_MAX_LFN 255`, `FF_LFN_UNICODE 2` (UTF-8), `FF_CODE_PAGE
932`, `FF_USE_MKFS 1`, `FF_USE_EXPAND 1`, `FF_FS_EXFAT 0`, `FF_MIN_SS 512` /
`FF_MAX_SS 4096`, `FF_FS_REENTRANT 0`, `FF_FS_TINY 0`.

### 2. `diskio.h`: take the integer types from `ff.h`

The vendored `diskio.h` carried a private block re-declaring
`UINT`/`BYTE`/`WORD`/`DWORD`/`QWORD`/`WCHAR`/`FSIZE_t`/`LBA_t`. It had two
independent bugs, and the full reasoning is in a comment at the patch site.
Briefly:

- it named `uint16_t`/`uint32_t`/`uint64_t` without including `<stdint.h>`,
  and only compiled because every translation unit in the reference happened
  to reach `<stdint.h>` some other way first; and
- even with `<stdint.h>` its types **disagreed** with `ff.h`'s. `ff.h` selects
  its integer types by preprocessor branch, and on a Windows host — which the
  CTest tree is, under MinGW — it takes its `#if defined(_WIN32)` branch and
  gets `DWORD` from `windows.h` as `unsigned long`, while the block said
  `uint32_t`. Both are 32 bits; they are still different types, so `ff.c`
  failed to compile against its own header.

Neither bug fires on the ARM target, so this changes nothing about what the
board runs. It is what lets the host test tree compile FatFs at all — which is
what gives this package `tests/test_fs_api.c`, running the whole filesystem
against a RAM-backed disk.

## What it costs

Measured on `bench_main`, `arm-none-eabi-size` per object:

| object | text | bss |
|---|---:|---:|
| `ffunicode.c` (CP932 tables) | **60,198** | 0 |
| `ff.c` | 15,417 | 519 |
| `fwog_fs.c` | 1,663 | 12,394 |
| `fs_flash_disk.c` | 557 | 1 |
| `fs_geom.c` | 80 | 0 |
| `ffsystem.c` | 0 | 0 |

**About 77 KB of flash and 13 KB of RAM**, and linking `fwog_main_fs` moved
`bench_main` from 188,568 to 265,924 bytes of text.

Two things follow that a future reader should know:

**`FF_CODE_PAGE 932` is 60 KB — 78% of the flash cost.** Shift-JIS code-page
tables, for a board that will almost certainly only ever see ASCII filenames.
Setting `FF_CODE_PAGE 437` would reclaim essentially all of it. It is not done
here because the code page affects how non-ASCII **short** filenames are
encoded on the media, and this volume's entire justification is byte-level
compatibility with the stock firmware — which uses 932. That is a real
trade with a real number attached, and it should be made deliberately by
whoever needs the 60 KB, not silently now.

**12 KB of the 13 KB of RAM is three static buffers in `fwog_fs.c`**: the
`FATFS` object, the one `FIL` object, and `f_mkfs`'s 4 KB work buffer. With
`FF_FS_TINY 0` and `FF_MAX_SS 4096`, each of the first two carries its own
4 KB sector buffer. The `f_mkfs` work buffer is permanently allocated for a
function most apps never call; sharing it would need care about aliasing with
the other two and is not worth doing blind.

`fwog_main_fs` is a **separate, opt-in library** for exactly these reasons — an
app that does not link it pays none of this.

## Licence

FatFs is BSD-1-Clause (a one-clause permissive licence); the text is at the top
of `ff.c` and in `00readme.txt`. It permits redistribution in source and binary
form with the copyright notice retained, which vendoring here does. FatFs is
**not** subject to this project's dual license — neither the FreeWili hardware
field-of-use condition nor the commercial track applies to it. See
`THIRD-PARTY-NOTICES.md` and `NOTICE` at the repository root.
