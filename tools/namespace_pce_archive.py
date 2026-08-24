#!/usr/bin/env python3
# AURORA_PCE_EXPERIMENTAL_V1
# AURORA_PCE_NAMESPACE_CORE_OPTIONS_V2
#
# Namespace Beetle PCE Fast for embedding beside other libretro cores.
#
# Some libretro core-option globals can escape ordinary `nm` enumeration
# in mixed/LTO builds. In addition to all globals seen in the raw PCE
# archive, force the standard retro_* API and discover every
# option_cats_*, option_defs_* and options_* identifier from Beetle's
# core-options headers. objcopy then rewrites definitions and references.

from __future__ import annotations

import argparse
from pathlib import Path
import re
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
    "retro_base_directory",
}

FORCED_CORE_OPTIONS = {
    "option_cats_us",
    "option_defs_us",
    "options_us",
    "options_intl",
    "option_cats_tr",
    "option_defs_tr",
    "options_tr",
}

CORE_OPTION_RE = re.compile(
    r"\b(?:option_cats|option_defs|options)_[A-Za-z0-9_]+\b"
)

def output(cmd):
    return subprocess.check_output(cmd, text=True, errors="replace")

def defined_symbols(nm, archive):
    text = output([nm, "-g", "--defined-only", str(archive)])
    out = set()
    for line in text.splitlines():
        line = line.strip()
        if not line or line.endswith(":"):
            continue
        parts = line.split()
        if len(parts) >= 2:
            sym = parts[-1]
            if sym and not sym.endswith(":"):
                out.add(sym)
    return out

def discover_core_option_symbols(core_dir):
    symbols = set(FORCED_CORE_OPTIONS)
    for path in sorted(core_dir.glob("libretro_core_options*.h")):
        try:
            text = path.read_text(encoding="utf-8-sig", errors="replace")
        except OSError:
            continue
        symbols.update(CORE_OPTION_RE.findall(text))
    return symbols

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nm", required=True)
    ap.add_argument("--objcopy", required=True)
    ap.add_argument("--ranlib", required=True)
    ap.add_argument("--raw", required=True, type=Path)
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--other", action="append", default=[], type=Path)
    args = ap.parse_args()

    if not args.raw.is_file():
        raise SystemExit(f"raw archive missing: {args.raw}")

    pce = defined_symbols(args.nm, args.raw)
    option_symbols = discover_core_option_symbols(args.raw.parent)

    rename = set(pce) | FORCED | option_symbols
    rename = {
        s for s in rename
        if s
        and not s.startswith("PCE_")
        and s not in {"_gp", "__gnu_local_gp"}
        and not s.startswith("__gnu_lto_")
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.raw, args.output)
    map_path = args.output.with_suffix(args.output.suffix + ".symbols.map")

    with map_path.open("w", encoding="utf-8") as f:
        for s in sorted(rename):
            f.write(f"{s} PCE_{s}\n")

    if rename:
        subprocess.check_call([
            args.objcopy,
            f"--redefine-syms={map_path}",
            str(args.output),
        ])

    subprocess.check_call([args.ranlib, str(args.output)])

    final_syms = defined_symbols(args.nm, args.output)
    required = {
        "PCE_retro_init",
        "PCE_retro_run",
        "PCE_retro_load_game",
    }
    missing = sorted(required - final_syms)
    if missing:
        raise SystemExit(
            "namespace failed; missing required PCE exports: "
            + ", ".join(missing)
        )

    critical_map = {
        "option_cats_us",
        "option_defs_us",
        "options_us",
        "options_intl",
        "option_cats_tr",
        "option_defs_tr",
        "options_tr",
    }
    missing_map = sorted(critical_map - rename)
    if missing_map:
        raise SystemExit(
            "namespace map missing core-option symbols: "
            + ", ".join(missing_map)
        )

    print(f"[PCE namespace V2] nm globals: {len(pce)}")
    print(
        "[PCE namespace V2] forced/discovered core-option symbols: "
        f"{len(option_symbols)}"
    )
    print(f"[PCE namespace V2] total mapping entries: {len(rename)}")
    print(f"[PCE namespace V2] output: {args.output}")
    print(f"[PCE namespace V2] map:    {map_path}")

if __name__ == "__main__":
    main()
