# wasm3 v0.5.0 — vendored snapshot

**Upstream:** https://github.com/wasm3/wasm3  
**Tag:** `v0.5.0` (`6b8bcb1e07bf26ebef09a7211b0a37a446eafd52`)  
**License:** MIT (`LICENSE` in this directory)

Interpreter only: the `source/` core, without WASI, uvwasi, libc, extras,
or the CLI. DiskGlass links this from `apps/diskglass/CMakeLists.txt` and
provides the FreeWili `wiliwasm` imports in `dg_wasm.c`.

Do not fetch a newer tag without re-measuring flash/RAM on `diskglass_main`.
The stock OG IO App ran wasm3 against `/scripts/*.wasm`; this is that
runtime, not a new bytecode.
