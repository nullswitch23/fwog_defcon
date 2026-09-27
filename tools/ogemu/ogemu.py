#!/usr/bin/env python3
"""Drive a FreeWili OG display-CPU app on the host (no board, no Pico SDK).

Example:
  python tools/ogemu/ogemu.py --app hostdeck --script tools/ogemu/scripts/smoke.jsonl --dump build-emu/hostdeck.png
"""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
OGEMU = pathlib.Path(__file__).resolve().parent
BUILD = REPO / "build-emu"


def _fw():
    """Same host compiler as `fw test` / `fw emu` — one cmake cache."""
    tools = pathlib.Path(__file__).resolve().parent.parent
    if str(tools) not in sys.path:
        sys.path.insert(0, str(tools))
    import fw
    return fw


def host_env() -> dict[str, str]:
    return _fw().host_env()


def host_toolchain_args() -> list[str]:
    return _fw()._host_toolchain_args()


def exe_name(app: str) -> pathlib.Path:
    name = f"ogemu_{app}"
    if sys.platform == "win32":
        name += ".exe"
    return BUILD / name


def configure_and_build(app: str) -> pathlib.Path:
    BUILD.mkdir(parents=True, exist_ok=True)
    env = host_env()
    cfg = ["cmake", "-S", str(OGEMU), "-B", str(BUILD)] + host_toolchain_args()
    subprocess.check_call(cfg, env=env)
    build = ["cmake", "--build", str(BUILD), "--target", f"ogemu_{app}"]
    subprocess.check_call(build, env=env)
    path = exe_name(app)
    if not path.is_file():
        # Multi-config generators (Visual Studio) put the exe in Debug/.
        candidates = list(BUILD.rglob(path.name))
        if not candidates:
            raise SystemExit(f"ogemu: built {path.name} not found under {BUILD}")
        path = candidates[0]
    return path


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description="Host-side FreeWili OG display UI emulator (v001)")
    p.add_argument("--app", default="hostdeck",
                   help="apps/<app>/display/main.c (default: hostdeck)")
    p.add_argument("--script", help="JSONL or line-oriented script")
    p.add_argument("--dump", help="PNG path for the 320x240 panel")
    p.add_argument("--text", help="overlay text dump (default: dump with .txt)")
    p.add_argument("--ms", type=int, help="run this many virtual ms if no script")
    p.add_argument("--stdin", action="store_true",
                   help="live JSONL from stdin (EOF sends quit)")
    p.add_argument("--listen", type=int, metavar="PORT",
                   help="live JSONL on 127.0.0.1:PORT (until a quit command)")
    p.add_argument("--frame", help="live P6 PPM path for the tk GUI")
    p.add_argument("--gui", action="store_true",
                   help="tkinter panel: tap the five buttons, insert JSONL")
    p.add_argument("--scale", type=int, default=2,
                   help="GUI LCD integer zoom (default 2)")
    p.add_argument("--no-build", action="store_true",
                   help="do not cmake/build; run an existing binary")
    p.add_argument("passthrough", nargs=argparse.REMAINDER,
                   help="extra args for the C binary (--press green ...)")
    args = p.parse_args(argv)

    if args.gui:
        import gui as ogemu_gui
        return ogemu_gui.run_gui(
            app=args.app,
            port=args.listen if args.listen is not None else 9320,
            no_build=args.no_build,
            dump=args.dump,
            frame=args.frame,
            scale=args.scale,
        )

    if args.no_build:
        binary = exe_name(args.app)
        if not binary.is_file():
            found = list(BUILD.rglob(f"ogemu_{args.app}*"))
            if not found:
                raise SystemExit(f"ogemu: {binary} missing; rerun without --no-build")
            binary = found[0]
    else:
        binary = configure_and_build(args.app)

    dump = args.dump
    if dump is None:
        dump = str(BUILD / f"{args.app}.png")
    dump_path = pathlib.Path(dump)
    dump_path.parent.mkdir(parents=True, exist_ok=True)

    cmd = [str(binary), "--dump", str(dump_path)]
    if args.text:
        cmd += ["--text", args.text]
    if args.script:
        cmd += ["--script", args.script]
    if args.ms is not None:
        cmd += ["--ms", str(args.ms)]
    if args.stdin:
        cmd += ["--stdin"]
    if args.listen is not None:
        cmd += ["--listen", str(args.listen)]
    if args.frame:
        cmd += ["--frame", args.frame]
    extra = list(args.passthrough)
    if extra and extra[0] == "--":
        extra = extra[1:]
    cmd += extra
    print("+", " ".join(cmd), flush=True)
    return subprocess.call(cmd, env=host_env())


if __name__ == "__main__":
    sys.exit(main())
