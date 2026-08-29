#!/usr/bin/env python3
# AURORA_FCEUMM_FDS_V0_6
# Namespace FCEUmm for embedding beside Aurora's other libretro cores.

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess

FORCED = {
    "retro_init", "retro_deinit", "retro_api_version",
    "retro_get_system_info", "retro_get_system_av_info",
    "retro_set_environment", "retro_set_video_refresh",
    "retro_set_audio_sample", "retro_set_audio_sample_batch",
    "retro_set_input_poll", "retro_set_input_state",
    "retro_set_controller_port_device", "retro_reset", "retro_run",
    "retro_serialize_size", "retro_serialize", "retro_unserialize",
    "retro_cheat_reset", "retro_cheat_set", "retro_load_game",
    "retro_load_game_special", "retro_unload_game", "retro_get_region",
    "retro_get_memory_data", "retro_get_memory_size",
}

SKIP = {"_gp", "__gnu_local_gp"}


def output(cmd):
    return subprocess.check_output(cmd, text=True, errors="replace")


def defined_symbols(nm, archive):
    text = output([nm, "-g", "--defined-only", str(archive)])
    symbols = set()
    for line in text.splitlines():
        line = line.strip()
        if not line or line.endswith(":"):
            continue
        parts = line.split()
        if len(parts) >= 2:
            sym = parts[-1]
            if sym and not sym.endswith(":"):
                symbols.add(sym)
    return symbols


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nm", required=True)
    ap.add_argument("--objcopy", required=True)
    ap.add_argument("--ranlib", required=True)
    ap.add_argument("--raw", required=True, type=Path)
    ap.add_argument("--output", required=True, type=Path)
    args = ap.parse_args()

    if not args.raw.is_file():
        raise SystemExit(f"raw archive missing: {args.raw}")

    print("[FCEUmm FDS namespace] scanning raw symbols...", flush=True)
    raw_symbols = defined_symbols(args.nm, args.raw)
    rename = set(raw_symbols) | FORCED
    rename = {
        s for s in rename
        if s
        and s not in SKIP
        and not s.startswith("FDS_")
        and not s.startswith("__gnu_lto_")
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.raw, args.output)
    map_path = args.output.with_suffix(args.output.suffix + ".symbols.map")

    with map_path.open("w", encoding="utf-8", newline="\n") as f:
        for sym in sorted(rename):
            f.write(f"{sym} FDS_{sym}\n")

    if rename:
        print(f"[FCEUmm FDS namespace] objcopy: renaming {len(rename)} symbols...", flush=True)
        subprocess.check_call([
            args.objcopy,
            f"--redefine-syms={map_path}",
            str(args.output),
        ])

    print("[FCEUmm FDS namespace] ranlib...", flush=True)
    subprocess.check_call([args.ranlib, str(args.output)])

    print("[FCEUmm FDS namespace] verifying final symbols...", flush=True)
    final_symbols = defined_symbols(args.nm, args.output)
    required = {
        "FDS_retro_init",
        "FDS_retro_run",
        "FDS_retro_load_game",
        "FDS_retro_serialize",
        "FDS_retro_serialize_size",
        "FDS_retro_unserialize",
        "FDS_FCEU_FDSEject",
        "FDS_FCEU_FDSSelect",
        "FDS_FCEU_FDSInsert",
        "FDS_aurora_fds_set_system_directory",
        "FDS_aurora_fds_set_skip_video",
    }
    missing = sorted(required - final_symbols)
    if missing:
        raise SystemExit(
            "namespace failed; missing required FDS exports: "
            + ", ".join(missing)
        )

    leaked_retro = sorted(
        s for s in final_symbols
        if s.startswith("retro_")
    )
    if leaked_retro:
        raise SystemExit(
            "namespace failed; unprefixed libretro exports remain: "
            + ", ".join(leaked_retro[:20])
        )

    print(f"[FCEUmm FDS namespace] raw globals: {len(raw_symbols)}")
    print(f"[FCEUmm FDS namespace] mapping entries: {len(rename)}")
    print(f"[FCEUmm FDS namespace] output: {args.output}")
    print(f"[FCEUmm FDS namespace] map:    {map_path}")


if __name__ == "__main__":
    main()

