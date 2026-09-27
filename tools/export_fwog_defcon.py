#!/usr/bin/env python3
"""Fresh-history export for github.com/nullswitch23/fwog_defcon.

Does not push. Does not copy .git, build trees, secrets, ogvegas, or the
Cyberpunk MP3. Does copy freewili1-docs (gitignored on henry). Rewrites
catalog github + UF2 release URLs.
"""
from __future__ import annotations

import json
import shutil
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
DST = Path(r"C:\opt\fwog_defcon")
GITHUB = "https://github.com/nullswitch23/fwog_defcon"
RELEASE_TAG = "defcon-2026"
DROP_SLUGS = {"ogvegas", "wilidoro", "orca-catalog"}

SKIP_DIR_NAMES = {
    ".git",
    ".cursor",
    "build",
    "build-tests",
    "build-emu",
    "build-nodiag",
    "T-Echo",
    "apps/ogvegas",
    "tools/wyze_cam",
    "tools/fsbak-out",
    "__pycache__",
    ".venv",
    ".idea",
    ".vs",
    "fwOGAppExplorer",
    "onewili",
    "esp8266_deauther",
}

SKIP_FILE_NAMES = {
    "i-got-a-real-bad-feeling-about-this-cyberpunk-2077.mp3",
    "AGENTS.local.md",
    "fwOGExp.exe",
    "fwogcli.exe",
}


def _ignore(dirpath: str, names: list[str]) -> set[str]:
    rel = Path(dirpath).resolve().relative_to(SRC)
    drop: set[str] = set()
    for n in names:
        child = rel / n if rel.parts else Path(n)
        parts = child.parts
        if n in SKIP_DIR_NAMES or n in SKIP_FILE_NAMES:
            drop.add(n)
            continue
        if "ogvegas" in parts or "wyze_cam" in parts:
            drop.add(n)
            continue
        p = Path(dirpath) / n
        if p.is_file():
            if p.suffix.lower() in {".uf2", ".elf", ".exe", ".map", ".hex"}:
                drop.add(n)
            elif p.suffix.lower() == ".bin" and child.as_posix() != (
                "bsp/main_cpu/fpga/fpga_default_v5.bin"
            ):
                drop.add(n)
    return drop


def copy_tree() -> None:
    if DST.exists():
        shutil.rmtree(DST)
    shutil.copytree(SRC, DST, ignore=_ignore, dirs_exist_ok=False)


def patch_gitignore() -> None:
    gi = DST / ".gitignore"
    text = gi.read_text(encoding="utf-8")
    text = text.replace("fwOGAppExplorer/\nfreewili1-docs/\n", "fwOGAppExplorer/\n")
    gi.write_text(text, encoding="utf-8")


def patch_cmake() -> None:
    cm = DST / "CMakeLists.txt"
    text = cm.read_text(encoding="utf-8")
    text = text.replace("add_subdirectory(apps/ogvegas)\n", "")
    cm.write_text(text, encoding="utf-8")


def rewrite_catalog(path: Path) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    apps = []
    for app in data.get("apps", []):
        if app.get("slug") in DROP_SLUGS:
            continue
        app["github"] = GITHUB
        uf2s = []
        for u in app.get("uf2", []):
            name = u.get("path") or ""
            u = dict(u)
            u["url"] = f"{GITHUB}/releases/download/{RELEASE_TAG}/{name}"
            uf2s.append(u)
        app["uf2"] = uf2s
        apps.append(app)
    data["apps"] = apps
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def patch_example_catalog() -> None:
    p = DST / "apps" / "apps_example.json"
    if not p.exists():
        return
    data = json.loads(p.read_text(encoding="utf-8"))
    data["apps"] = [a for a in data.get("apps", []) if a.get("slug") not in DROP_SLUGS]
    p.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def patch_readme_hole() -> None:
    p = DST / "README.md"
    hole = (
        "# fwog_defcon\n\n"
        "<!-- HUMAN-INTRO: paste the wholly human-written introduction here.\n"
        "     Do not git push this repository until that paragraph is in. -->\n\n"
        "Village firmware cut of the FreeWili OG dual-RP2040 BSP. UF2s for "
        "[FreeWili OG App Explorer](https://github.com/freewili/fwOGAppExplorer) "
        "are GitHub Release assets (`"
        + RELEASE_TAG
        + "`), not files in this tree. Upstream BSP: "
        "[freewili/wiliOGbsp](https://github.com/freewili/wiliOGbsp).\n\n"
        "---\n\n"
    )
    body = p.read_text(encoding="utf-8")
    # Keep the rest of the README after the first heading.
    if body.startswith("# "):
        body = body.split("\n", 1)[1].lstrip("\n")
    p.write_text(hole + body, encoding="utf-8")


def main() -> int:
    print(f"export {SRC} -> {DST}")
    copy_tree()
    patch_gitignore()
    patch_cmake()
    rewrite_catalog(DST / "catalog" / "apps.json")
    rewrite_catalog(DST / "catalog" / "catalog.json")
    patch_example_catalog()
    patch_readme_hole()
    print("done (no git, no push)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
