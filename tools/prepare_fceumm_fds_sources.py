#!/usr/bin/env python3
# AURORA_FCEUMM_FDS_V0_6
# Generate lean FDS-only/two-pad build overlays without modifying FCEUmm itself.

from __future__ import annotations

import argparse
from pathlib import Path
import os
import tempfile


def fail(msg: str) -> None:
    raise SystemExit("prepare_fceumm_fds_sources.py: " + msg)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8").replace("\r\n", "\n")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected anchor exactly once, found {count}")
    return text.replace(old, new, 1)


def replace_between_once(text: str, start: str, end: str, new: str, label: str) -> str:
    if text.count(start) != 1 or text.count(end) != 1:
        fail(
            f"{label}: expected unique span anchors; "
            f"start={text.count(start)} end={text.count(end)}"
        )
    i = text.index(start)
    j = text.index(end, i)
    if j <= i:
        fail(f"{label}: invalid span order")
    return text[:i] + new + text[j:]


def write_atomic(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=path.name + ".", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        os.replace(tmp, path)
    finally:
        try:
            os.unlink(tmp)
        except FileNotFoundError:
            pass


def patch_fceu(text: str) -> str:
    # These headers only feed features deliberately removed by this FDS build.
    for inc, label in (
        ('#include  "netplay.h"\n', "netplay include"),
        ('#include  "cheat.h"\n', "cheat include"),
        ('#include  "movie.h"\n', "movie include"),
        ('#include  "vsuni.h"\n', "VS include"),
    ):
        text = replace_once(text, inc, "", "src/fceu.c " + label)

    loader = (
        "\tif (iNESLoad(name, fp))\n"
        "\t\tgoto endlseq;\n"
        "\tif (NSFLoad(fp))\n"
        "\t\tgoto endlseq;\n"
        "\tif (UNIFLoad(name, fp))\n"
        "\t\tgoto endlseq;\n"
        "\tif (FDSLoad(name, fp))\n"
        "\t\tgoto endlseq;\n"
    )
    fds_only = (
        "\t/* AURORA_FCEUMM_FDS_V0_6_FDS_ONLY_LOADER */\n"
        "\tif (FDSLoad(name, fp))\n"
        "\t\tgoto endlseq;\n"
    )
    text = replace_once(text, loader, fds_only, "src/fceu.c loader chain")

    text = replace_between_once(
        text,
        "int CopyFamiLoad(void);\n",
        "int FCEUI_Initialize(void)",
        "/* AURORA_FCEUMM_FDS_V0_6_NO_COPYFAMI */\n"
        "FCEUGI *FCEUI_CopyFamiStart(void) {\n"
        "\treturn NULL;\n"
        "}\n\n",
        "src/fceu.c CopyFamicom span",
    )

    text = replace_once(
        text,
        "\t\tif (FCEUnetplay)\n\t\t\tFCEUD_NetworkClose();\n",
        "\t\t/* AURORA_FCEUMM_FDS_V0_6_NO_NETPLAY_CLOSE */\n",
        "src/fceu.c netplay close",
    )
    text = replace_once(
        text,
        "\t\tif (GameInfo->type != GIT_NSF)\n\t\t\tFCEU_FlushGameCheats(0, 0);\n",
        "\t\t/* AURORA_FCEUMM_FDS_V0_6_NO_CHEAT_FLUSH */\n",
        "src/fceu.c cheat flush",
    )
    text = replace_once(
        text,
        "\t\tFCEU_CloseGenie();\n",
        "\t\t/* AURORA_FCEUMM_FDS_V0_6_NO_GAME_GENIE_CLOSE */\n",
        "src/fceu.c Game Genie close",
    )

    load_extras = (
        "\tFCEU_ResetVidSys();\n"
        "\tif (GameInfo->type != GIT_NSF)\n"
        "\t\tif (FSettings.GameGenie)\n"
        "\t\t\tFCEU_OpenGenie();\n\n"
        "\tPowerNES();\n"
        "\tFCEUSS_CheckStates();\n"
        "\tFCEUMOV_CheckMovies();\n\n"
        "\tif (GameInfo->type != GIT_NSF) {\n"
        "\t\tFCEU_LoadGamePalette();\n"
        "\t\tFCEU_LoadGameCheats(0);\n"
        "\t}\n"
    )
    load_lean = (
        "\tFCEU_ResetVidSys();\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_LEAN_LOAD: FDS has no movie/cheat/Genie frontend. */\n"
        "\tPowerNES();\n"
        "\tFCEUSS_CheckStates();\n"
        "\tFCEU_LoadGamePalette();\n"
    )
    text = replace_once(text, load_extras, load_lean, "src/fceu.c post-load extras")

    text = replace_once(
        text,
        "void FCEUI_Kill(void) {\n\tFCEU_KillVirtualVideo();\n\tFCEU_KillGenie();\n}\n",
        "void FCEUI_Kill(void) {\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_GAME_GENIE_KILL */\n"
        "\tFCEU_KillVirtualVideo();\n"
        "}\n",
        "src/fceu.c kill Game Genie",
    )
    text = replace_once(
        text,
        "\tif (geniestage != 1) FCEU_ApplyPeriodicCheats();\n",
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_PERIODIC_CHEATS */\n",
        "src/fceu.c per-frame cheats",
    )
    text = replace_once(
        text,
        "void ResetNES(void) {\n\tFCEUMOV_AddCommand(FCEUNPCMD_RESET);\n",
        "void ResetNES(void) {\n\t/* AURORA_FCEUMM_FDS_V0_6_NO_MOVIE_RESET */\n",
        "src/fceu.c reset movie command",
    )
    text = replace_once(
        text,
        "void PowerNES(void) {\n"
        "\tFCEUMOV_AddCommand(FCEUNPCMD_POWER);\n"
        "\tif (!GameInfo) return;\n\n"
        "\tFCEU_CheatResetRAM();\n"
        "\tFCEU_CheatAddRAM(2, 0, RAM);\n\n"
        "\tFCEU_GeniePower();\n",
        "void PowerNES(void) {\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_LEAN_POWER */\n"
        "\tif (!GameInfo) return;\n",
        "src/fceu.c power cheat/movie/Genie hooks",
    )
    text = replace_once(
        text,
        "\tGameInterface(GI_POWER);\n"
        "\tif (GameInfo->type == GIT_VSUNI)\n"
        "\t\tFCEU_VSUniPower();\n",
        "\tGameInterface(GI_POWER);\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_VSUNI_RUNTIME */\n",
        "src/fceu.c VS power hook",
    )
    text = replace_once(
        text,
        "\tX6502_Power();\n\tFCEU_PowerCheats();\n",
        "\tX6502_Power();\n\t/* AURORA_FCEUMM_FDS_V0_6_NO_POWER_CHEATS */\n",
        "src/fceu.c power cheats",
    )

    # AURORA_FCEUMM_FDS_V0_6_15_PRUNED_LINK_STUBS
    # ppu.c retains declarations/calls for MMC5 and NSF even in an FDS-only
    # build.  Those paths are unreachable here: MMC5Hack remains false and
    # the loader can never create GIT_NSF.  Keep the pruned core self-contained
    # with minimal ABI-compatible definitions instead of linking mmc5.c/nsf.c.
    text = replace_once(
        text,
        "uint64 timestampbase;\n",
        "uint64 timestampbase;\n\n"
        "/* AURORA_FCEUMM_FDS_V0_6_15_PRUNED_LINK_STUBS */\n"
        "uint8 mmc5ABMode = 0;\n"
        "void MMC5_hb(int scanline) { (void)scanline; }\n"
        "void DoNSFFrame(void) { }\n\n",
        "src/fceu.c pruned MMC5/NSF linker stubs",
    )

    for marker in (
        "AURORA_FCEUMM_FDS_V0_6_FDS_ONLY_LOADER",
        "AURORA_FCEUMM_FDS_V0_6_NO_COPYFAMI",
        "AURORA_FCEUMM_FDS_V0_6_LEAN_LOAD",
        "AURORA_FCEUMM_FDS_V0_6_NO_PERIODIC_CHEATS",
        "AURORA_FCEUMM_FDS_V0_6_LEAN_POWER",
        "AURORA_FCEUMM_FDS_V0_6_NO_NETPLAY_CLOSE",
    ):
        if marker not in text:
            fail(f"src/fceu.c marker missing after patch: {marker}")
    return text


def patch_state(text: str) -> str:
    # AURORA_FCEUMM_FDS_V0_6_14_STATE_MEMSTREAM_ASPRINTF_FIX
    # state.c historically includes endian.h, whose prototypes are FILE *.
    # The libretro state path uses MEM_TYPE=memstream_t, while the pinned
    # core already provides fceu-endian.h with the correct MEM_TYPE API.
    text = replace_once(
        text,
        '#include "endian.h"\n',
        '#include "fceu-endian.h"\n',
        "src/state.c MEM_TYPE endian header",
    )
    # Aurora owns state slots/files.  FCEUmm only serializes to the supplied
    # memstream, so its FILE-based state-directory probe is both unnecessary
    # and type-invalid under HAVE_MEMSTREAM.
    text = replace_between_once(
        text,
        "void FCEUSS_CheckStates(void) {\n",
        "void ResetExState(void (*PreSave)(void), void (*PostSave)(void)) {",
        "void FCEUSS_CheckStates(void) {\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_14_NO_FILE_STATE_SCAN */\n"
        "\tmemset(SaveStateStatus, 0, sizeof(SaveStateStatus));\n"
        "\tCurrentState = 0;\n"
        "\tStateShow = 0;\n"
        "}\n\n",
        "src/state.c FILE-based state scan",
    )
    text = replace_once(text, '#include "movie.h"\n', "", "src/state.c movie include")
    text = replace_once(text, '#include "netplay.h"\n', "", "src/state.c netplay include")
    text = replace_once(text, "extern int geniestage;\n", "", "src/state.c geniestage extern")

    genie_save = (
        "\tif (geniestage == 1) {\n"
        "\t\tFCEU_DispMessage(\"Cannot save FCS in GG screen.\");\n"
        "\t\treturn;\n"
        "\t}\n\n"
    )
    text = replace_once(
        text, genie_save,
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_GENIE_STATE_GUARD */\n",
        "src/state.c save Game Genie guard",
    )
    genie_load = (
        "\tif (geniestage == 1) {\n"
        "\t\tFCEU_DispMessage(\"Cannot load FCS in GG screen.\");\n"
        "\t\treturn(0);\n"
        "\t}\n\n"
    )
    text = replace_once(
        text, genie_load,
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_GENIE_STATE_GUARD */\n",
        "src/state.c load Game Genie guard",
    )
    text = replace_once(
        text,
        "\tFCEUI_SelectMovie(-1);\n\n",
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_MOVIE_STATE_SELECTOR */\n\n",
        "src/state.c movie selector",
    )
    text = replace_between_once(
        text,
        "void FCEUI_LoadState(char *fname) {\n",
        "void FCEU_DrawSaveStates(uint8 *XBuf) {",
        "void FCEUI_LoadState(char *fname) {\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_LOCAL_STATE_LOAD */\n"
        "\tStateShow = 0;\n"
        "\t(void)FCEUSS_Load(fname);\n"
        "}\n\n",
        "src/state.c movie/netplay load-state span",
    )
    for bad in ("FCEUMOV_", "FCEUnetplay", "FCEUNET_"):
        if bad in text:
            fail(f"src/state.c retained forbidden runtime reference: {bad}")
    return text


def patch_general(text: str) -> str:
    # AURORA_FCEUMM_FDS_V0_6_14_STATE_MEMSTREAM_ASPRINTF_FIX_ASPRINTF
    # The pinned source already carries a bounded local asprintf fallback.
    # Force that fallback in this generated TU instead of depending on the
    # PS2 libc declaration selected by the global HAVE_ASPRINTF define.
    text = replace_once(
        text,
        "#ifndef HAVE_ASPRINTF\n",
        "/* AURORA_FCEUMM_FDS_V0_6_14_LOCAL_ASPRINTF */\n"
        "#undef HAVE_ASPRINTF\n"
        "#ifndef HAVE_ASPRINTF\n",
        "src/general.c local asprintf fallback",
    )
    text = replace_once(text, '#include "movie.h"\n', "", "src/general.c movie include")
    old_override = (
        "\tif (GameInfo) {\t/* Rebuild cache of present states/movies. */\n"
        "\t\tif (which == FCEUIOD_STATE)\n"
        "\t\t\tFCEUSS_CheckStates();\n"
        "\t\telse if (which == FCEUIOD_MOVIE)\n"
        "\t\t\tFCEUMOV_CheckMovies();\n"
        "\t}\n"
    )
    new_override = (
        "\t/* AURORA_FCEUMM_FDS_V0_6_STATE_ONLY_DIR_OVERRIDE */\n"
        "\tif (GameInfo && which == FCEUIOD_STATE)\n"
        "\t\tFCEUSS_CheckStates();\n"
    )
    text = replace_once(text, old_override, new_override, "src/general.c dir override")

    text = replace_between_once(
        text,
        '\tcase FCEUMKF_NPTEMP: asprintf(&ret, "%s"PSS "m590plqd94fo.tmp", BaseDirectory); break;\n',
        "\tcase FCEUMKF_STATE:\n",
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_MOVIE_NETPLAY_FILENAMES */\n",
        "src/general.c movie/netplay filename cases",
    )
    cheat_case = (
        "\tcase FCEUMKF_CHEAT:\n"
        "\t\tif (odirs[FCEUIOD_CHEATS])\n"
        "\t\t\tasprintf(&ret, \"%s\"PSS \"%s.cht\", odirs[FCEUIOD_CHEATS], FileBase);\n"
        "\t\telse\n"
        "\t\t\tasprintf(&ret, \"%s\"PSS \"cheats\"PSS \"%s.cht\", BaseDirectory, FileBase);\n"
        "\t\tbreak;\n"
    )
    text = replace_once(
        text, cheat_case,
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_CHEAT_FILENAME */\n",
        "src/general.c cheat filename case",
    )
    text = replace_once(
        text,
        '\tcase FCEUMKF_GGROM: asprintf(&ret, "%s"PSS "gg.rom", BaseDirectory); break;\n',
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_GAME_GENIE_FILENAME */\n",
        "src/general.c Game Genie filename case",
    )
    if "FCEUMOV_" in text:
        fail("src/general.c retained movie reference")
    return text


def patch_libretro(text: str) -> str:
    for inc, label in (
        ('#include "cheat.h"\n', "cheat include"),
        ('#include "ines.h"\n', "iNES include"),
        ('#include "unif.h"\n', "UNIF include"),
    ):
        text = replace_once(text, inc, "", "libretro " + label)
    text = replace_once(text, "extern CartInfo iNESCart;\n", "", "libretro iNESCart extern")
    text = replace_once(text, "extern CartInfo UNIFCart;\n", "", "libretro UNIFCart extern")

    text = replace_once(
        text,
        '\tinfo->valid_extensions = "fds|FDS|zip|ZIP|nes|NES|unif|UNIF";\n',
        '\t/* AURORA_FCEUMM_FDS_V0_6_FDS_ONLY_EXTENSIONS */\n'
        '\tinfo->valid_extensions = "fds|FDS";\n',
        "libretro valid_extensions",
    )

    old_data = """void *retro_get_memory_data(unsigned id) {
\tif (id != RETRO_MEMORY_SAVE_RAM)
\t\treturn NULL;

\tif (iNESCart.battery)
\t\treturn iNESCart.SaveGame[0];
\tif (UNIFCart.battery)
\t\treturn UNIFCart.SaveGame[0];

\treturn 0;
}
"""
    new_data = """void *retro_get_memory_data(unsigned id) {
\t/* AURORA_FCEUMM_FDS_V0_6_NO_CART_SRAM */
\t(void)id;
\treturn NULL;
}
"""
    text = replace_once(text, old_data, new_data, "libretro cartridge SRAM data")

    old_size = """size_t retro_get_memory_size(unsigned id) {
\tif (id != RETRO_MEMORY_SAVE_RAM)
\t\treturn 0;

\tif (iNESCart.battery)
\t\treturn iNESCart.SaveGameLen[0];
\tif (UNIFCart.battery)
\t\treturn UNIFCart.SaveGameLen[0];

\treturn 0;
}
"""
    new_size = """size_t retro_get_memory_size(unsigned id) {
\t/* AURORA_FCEUMM_FDS_V0_6_NO_CART_SRAM */
\t(void)id;
\treturn 0;
}
"""
    if old_size not in text and old_size.endswith("\n"):
        old_size = old_size[:-1]
    text = replace_once(text, old_size, new_size, "libretro cartridge SRAM size")

    fceu_init_anchor = """static void fceu_init(const char * full_path) {
\tFCEUI_Initialize();

"""
    fceu_init_new = """/* AURORA_FCEUMM_FDS_V0_6_SYSTEM_BIOS
 * Aurora passes its already-existing SNESticle/SYSTEM directory here.
 * FCEU_MakeFName(FCEUMKF_FDSROM) then resolves exactly:
 *     <SYSTEM>/disksys.rom
 * Keep a relative SYSTEM fallback for isolated-core tests.
 */
static char aurora_system_directory[PATH_MAX] = "SYSTEM";

void aurora_fds_set_system_directory(const char *dir) {
\tif (!dir || !dir[0]) {
\t\tstrcpy(aurora_system_directory, "SYSTEM");
\t\treturn;
\t}
\tstrncpy(aurora_system_directory, dir, sizeof(aurora_system_directory) - 1);
\taurora_system_directory[sizeof(aurora_system_directory) - 1] = 0;
}

static void fceu_init(const char * full_path) {
\tFCEUI_Initialize();
\tFCEUI_SetBaseDirectory(aurora_system_directory);

"""
    text = replace_once(
        text, fceu_init_anchor, fceu_init_new, "libretro SYSTEM/disksys.rom setup"
    )

    text = replace_once(
        text,
        "\tGameInfo = FCEUI_LoadGame(full_path);\n\temulator_set_input();\n",
        "\tGameInfo = FCEUI_LoadGame(full_path);\n"
        "\tif (!GameInfo)\n"
        "\t\treturn;\n"
        "\temulator_set_input();\n",
        "libretro failed FDS load guard",
    )

    text = replace_once(
        text,
        "\tinfo->timing.sample_rate = 32040.5;\n",
        "\t/* AURORA_FCEUMM_FDS_V0_6_EXACT_AUDIO_RATE */\n"
        "\tinfo->timing.sample_rate = 32050.0;\n",
        "libretro FDS audio rate",
    )

    old_run = """void retro_run(void) {
\tunsigned y, x;
\tuint8_t *gfx;
\tstatic uint16_t video_out[256 * 240];
\tint32 ssize = 0;

\tupdate_input();

\tFCEUI_Emulate(&gfx, &sound, &ssize, 0);

\tgfx = XBuf;
\tfor (y = 0; y < 240; y++)
\t\tfor (x = 0; x < 256; x++, gfx++)
\t\t\tvideo_out[y * 256 + x] = palette[*gfx];

\tvideo_cb(video_out, 256, 240, 512);

\tfor (y = 0; y < ssize; y++)
\t\tsound[y] = (sound[y] << 16) | (sound[y] & 0xffff);

\taudio_batch_cb((const int16_t*)sound, ssize);
}
"""
    new_run = """/* AURORA_FCEUMM_FDS_V0_6_SAFE_SKIP
 * One-shot frontend video skip. CPU/FDS/APU and audio always advance.
 */
static int aurora_skip_video = 0;

void aurora_fds_set_skip_video(int skip) {
\taurora_skip_video = skip ? 1 : 0;
}

void retro_run(void) {
\tunsigned y, x;
\tuint8_t *gfx;
\tstatic uint16_t video_out[256 * 240];
\tint32 ssize = 0;
\tint skip_video = aurora_skip_video;
\taurora_skip_video = 0;

\tupdate_input();

\tFCEUI_Emulate(&gfx, &sound, &ssize, skip_video ? 1 : 0);

\tif (!skip_video) {
\t\tgfx = XBuf;
\t\tfor (y = 0; y < 240; y++)
\t\t\tfor (x = 0; x < 256; x++, gfx++)
\t\t\t\tvideo_out[y * 256 + x] = palette[*gfx];

\t\tvideo_cb(video_out, 256, 240, 512);
\t}

\tfor (y = 0; y < ssize; y++)
\t\tsound[y] = (sound[y] << 16) | (sound[y] & 0xffff);

\taudio_batch_cb((const int16_t*)sound, ssize);
}
"""
    text = replace_once(text, old_run, new_run, "libretro one-shot FDS video skip")

    # AURORA_FCEUMM_FDS_V0_6_STATE_SIZE_SAFETY
    # The old frontend probes state size through a temporary 1 MB memstream.
    # Keep the established format, but fail cleanly if that one-time allocation
    # cannot be satisfied on the 32 MB EE heap.
    text = replace_once(
        text,
        "\t\tuint8_t *buffer = (uint8_t*)malloc(1000000);\n"
        "\t\tmemstream_set_buffer(buffer, 1000000);\n",
        "\t\tuint8_t *buffer = (uint8_t*)malloc(1000000);\n"
        "\t\tif (!buffer)\n"
        "\t\t\treturn 0;\n"
        "\t\tmemstream_set_buffer(buffer, 1000000);\n",
        "libretro serialize-size allocation guard",
    )

    old_load = """bool retro_load_game(const struct retro_game_info *game) {
\tfceu_init(game->path);

\treturn TRUE;
}
"""
    new_load = """bool retro_load_game(const struct retro_game_info *game) {
\t/* AURORA_FCEUMM_FDS_V0_6_LOAD_RESULT */
\tif (!game || !game->path || !game->path[0])
\t\treturn false;
\t/* AURORA_FCEUMM_FDS_V0_6_STATE_SIZE_PER_GAME */
\tserialize_size = 0;
\tfceu_init(game->path);
\treturn GameInfo != NULL;
}
"""
    text = replace_once(text, old_load, new_load, "libretro FDS load result")

    text = replace_once(
        text,
        "void retro_unload_game(void) {\n"
        "\tFCEUI_CloseGame();\n"
        "}\n",
        "void retro_unload_game(void) {\n"
        "\tFCEUI_CloseGame();\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_STATE_SIZE_PER_GAME */\n"
        "\tserialize_size = 0;\n"
        "}\n",
        "libretro per-game serialize-size reset",
    )

    for marker in (
        "AURORA_FCEUMM_FDS_V0_6_FDS_ONLY_EXTENSIONS",
        "AURORA_FCEUMM_FDS_V0_6_NO_CART_SRAM",
        "AURORA_FCEUMM_FDS_V0_6_SYSTEM_BIOS",
        "AURORA_FCEUMM_FDS_V0_6_SAFE_SKIP",
        "AURORA_FCEUMM_FDS_V0_6_LOAD_RESULT",
        "AURORA_FCEUMM_FDS_V0_6_EXACT_AUDIO_RATE",
        "AURORA_FCEUMM_FDS_V0_6_STATE_SIZE_PER_GAME",
    ):
        if marker not in text:
            fail(f"libretro marker missing: {marker}")
    return text


def patch_video(text: str) -> str:
    for inc, label in (
        ('#include "movie.h"\n', "movie include"),
        ('#include "nsf.h"\n', "NSF include"),
        ('#include "vsuni.h"\n', "VS include"),
    ):
        text = replace_once(text, inc, "", "src/video.c " + label)

    old_dummy = (
        "void FCEU_PutImageDummy(void) {\n"
        "\t#ifdef SHOWFPS\n"
        "\tShowFPS();\n"
        "\t#endif\n"
        "\tif (GameInfo->type != GIT_NSF) {\n"
        "\t\tFCEU_DrawNTSCControlBars(XBuf);\n"
        "\t\tFCEU_DrawSaveStates(XBuf);\n"
        "\t\tFCEU_DrawMovies(XBuf);\n"
        "\t}\n"
        "\tif (howlong) howlong--;\t/* DrawMessage() */\n"
        "}\n"
    )
    new_dummy = (
        "void FCEU_PutImageDummy(void) {\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_LEAN_VIDEO_DUMMY */\n"
        "\t#ifdef SHOWFPS\n"
        "\tShowFPS();\n"
        "\t#endif\n"
        "\tFCEU_DrawNTSCControlBars(XBuf);\n"
        "\tFCEU_DrawSaveStates(XBuf);\n"
        "\tif (howlong) howlong--;\t/* DrawMessage() */\n"
        "}\n"
    )
    text = replace_once(text, old_dummy, new_dummy, "src/video.c frameskip draw path")

    text = replace_between_once(
        text,
        "void FCEU_PutImage(void) {\n",
        "void FCEU_DispMessage(char *format, ...) {",
        "void FCEU_PutImage(void) {\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_LEAN_VIDEO */\n"
        "\t#ifdef SHOWFPS\n"
        "\tShowFPS();\n"
        "\t#endif\n"
        "\tFCEU_DrawSaveStates(XBuf);\n"
        "\tFCEU_DrawNTSCControlBars(XBuf);\n"
        "\tDrawMessage();\n"
        "\tFCEU_DrawInput(XBuf);\n"
        "}\n\n",
        "src/video.c FDS-only image presentation",
    )
    for bad in ("FCEU_DrawMovies(", "FCEU_VSUniDraw(", "DrawNSF("):
        if bad in text:
            fail(f"src/video.c retained forbidden frontend draw reference: {bad}")
    return text


def audit_sources(fceu: str, libretro: str, input_text: str, pads_text: str,
                  state: str, general: str, video: str) -> None:
    required = {
        "src/fceu.c": (
            "FCEU_ApplyPeriodicCheats();", "FCEUMOV_CheckMovies();",
            "FCEU_FlushGameCheats(0, 0);", "FCEUnetplay",
            "FCEU_GeniePower();",
        ),
        "libretro.c": (
            '#include "cheat.h"', "extern CartInfo iNESCart;",
            "void retro_cheat_reset(void)",
        ),
        "src/state.c": (
            '#include "movie.h"', '#include "netplay.h"',
            "FCEUMOV_Stop();", "FCEUnetplay",
        ),
        "src/general.c": (
            '#include "movie.h"', "FCEUMOV_CheckMovies();",
            "case FCEUMKF_CHEAT:", "case FCEUMKF_GGROM:",
        ),
        "src/video.c": (
            '#include "movie.h"', '#include "nsf.h"', '#include "vsuni.h"',
            "FCEU_DrawMovies(XBuf);", "FCEU_VSUniDraw(XBuf);", "DrawNSF(XBuf);",
        ),
        "src/input.c": (
            "extern INPUTC *FCEU_InitJoyPad(int w);",
            "extern INPUTCFC *FCEU_InitBarcodeWorld(void);",
            "void FCEUI_SetInputFC(int type, void *ptr, int attrib)",
        ),
        "src/input/pads.c": (
            "FCEUMOV_AddJoy(joy);", "FCEU_VSUniSwap(&joy[0], &joy[1]);",
            "INPUTC *FCEU_InitJoyPad(int w)",
        ),
    }
    texts = {
        "src/fceu.c": fceu, "libretro.c": libretro,
        "src/state.c": state, "src/general.c": general, "src/video.c": video,
        "src/input.c": input_text, "src/input/pads.c": pads_text,
    }
    for name, anchors in required.items():
        for anchor in anchors:
            if anchor not in texts[name]:
                fail(f"{name} audit anchor missing: {anchor}")


INPUT_FDS_C = """/* AURORA_FCEUMM_FDS_V0_6_TWO_PAD_INPUT
 * Minimal FDS input dispatcher derived from the pinned FCEUmm input.c.
 * FDS integration exposes only the two built-in standard joypads.
 */
#include "fceu-types.h"
#include "x6502.h"
#include "fceu.h"
#include "input.h"
#include "driver.h"

extern INPUTC *FCEU_InitJoyPad(int w);

static void *InputDataPtr[2] = { 0, 0 };
uint8 LastStrobe = 0;

static int JPAttrib[2] = { 0, 0 };
static int JPType[2] = { SI_NONE, SI_NONE };
static INPUTC DummyJPort = { 0, 0, 0, 0, 0, 0 };
static INPUTC *JPorts[2] = { &DummyJPort, &DummyJPort };

void (*InputScanlineHook)(uint8 *bg, uint8 *spr, uint32 linets, int final) = 0;

static DECLFR(JPRead) {
\tuint8 ret = 0;
\tif (JPorts[A & 1]->Read)
\t\tret |= JPorts[A & 1]->Read(A & 1);
\tret |= X.DB & 0xC0;
\treturn ret;
}

static DECLFW(B4016) {
\tif (JPorts[0]->Write)
\t\tJPorts[0]->Write(V & 1);
\tif (JPorts[1]->Write)
\t\tJPorts[1]->Write(V & 1);

\tif ((LastStrobe & 1) && (!(V & 1))) {
\t\tif (JPorts[0]->Strobe)
\t\t\tJPorts[0]->Strobe(0);
\t\tif (JPorts[1]->Strobe)
\t\t\tJPorts[1]->Strobe(1);
\t}
\tLastStrobe = V & 1;
}

void FCEU_DrawInput(uint8 *buf) {
\tint x;
\tfor (x = 0; x < 2; x++)
\t\tif (JPorts[x]->Draw)
\t\t\tJPorts[x]->Draw(x, buf, JPAttrib[x]);
}

void FCEU_UpdateInput(void) {
\tint x;
\tfor (x = 0; x < 2; x++)
\t\tif (JPorts[x]->Update)
\t\t\tJPorts[x]->Update(x, InputDataPtr[x], JPAttrib[x]);
}

static void SetInputStuff(int x) {
\tif (JPType[x] == SI_GAMEPAD)
\t\tJPorts[x] = FCEU_InitJoyPad(x);
\telse
\t\tJPorts[x] = &DummyJPort;

\tInputScanlineHook = 0;
}

void InitializeInput(void) {
\tLastStrobe = 0;
\tSetReadHandler(0x4016, 0x4017, JPRead);
\tSetWriteHandler(0x4016, 0x4016, B4016);
\tSetInputStuff(0);
\tSetInputStuff(1);
}

void FCEUI_SetInput(int port, int type, void *ptr, int attrib) {
\tif (port < 0 || port > 1)
\t\treturn;
\tJPAttrib[port] = attrib;
\tJPType[port] = (type == SI_GAMEPAD) ? SI_GAMEPAD : SI_NONE;
\tInputDataPtr[port] = ptr;
\tSetInputStuff(port);
}

void FCEUI_SetInputFC(int type, void *ptr, int attrib) {
\t(void)type;
\t(void)ptr;
\t(void)attrib;
}
"""

PADS_FDS_C = """/* AURORA_FCEUMM_FDS_V0_6_TWO_PAD_PADS
 * Minimal two-pad implementation derived from pinned FCEUmm input/pads.c.
 * Movie/netplay/VS/FourScore hooks are deliberately absent.
 */
#include "../fceu-types.h"
#include "../input.h"
#include "../fceu.h"
#include "../state.h"

static uint8 joy_readbit[2] = { 0, 0 };
static uint8 joy[4] = { 0, 0, 0, 0 };

extern uint8 LastStrobe;

SFORMAT FCEUCTRL_STATEINFO[] = {
\t{ joy_readbit, 2, "JYRB" },
\t{ joy, 4, "JOYS" },
\t{ &LastStrobe, 1, "LSTS" },
\t{ 0 }
};

void FCEUI_DisableFourScore(int s) {
\t(void)s;
}

uint8 FCEU_GetJoyJoy(void) {
\treturn joy[0] | joy[1] | joy[2] | joy[3];
}

static void FP_FASTAPASS(1) StrobeGP(int w) {
\tjoy_readbit[w] = 0;
}

static uint8 FP_FASTAPASS(1) ReadGP(int w) {
\tuint8 ret = 1;
\tif (joy_readbit[w] < 8)
\t\tret = (joy[w] >> joy_readbit[w]) & 1;
\tif (joy_readbit[w] != 0xFF)
\t\tjoy_readbit[w]++;
\treturn ret;
}

static void FP_FASTAPASS(3) UpdateGP(int w, void *data, int arg) {
\tconst uint32 *ptr = (const uint32 *)data;
\t(void)arg;
\tif (w < 0 || w > 1)
\t\treturn;
\tjoy[w] = ptr ? (uint8)(*ptr & 0xFF) : 0;
\tjoy[w + 2] = 0;
}

static INPUTC GPC = { ReadGP, 0, StrobeGP, UpdateGP, 0, 0 };

INPUTC *FCEU_InitJoyPad(int w) {
\tjoy_readbit[w] = 0;
\tjoy[w] = 0;
\tjoy[w + 2] = 0;
\treturn &GPC;
}
"""


def audit_generated(files: dict[str, str]) -> None:
    # AURORA_FCEUMM_FDS_V0_6_15_PRUNED_LINK_STUBS_AUDIT
    fceu_fds = files["fceu_fds.c"]
    required_pruned_stubs = (
        "AURORA_FCEUMM_FDS_V0_6_15_PRUNED_LINK_STUBS",
        "uint8 mmc5ABMode = 0;",
        "void MMC5_hb(int scanline) { (void)scanline; }",
        "void DoNSFFrame(void) { }",
    )
    for token in required_pruned_stubs:
        if fceu_fds.count(token) != 1:
            fail(f"fceu_fds.c pruned linker stub invalid: {token}")
    # AURORA_FCEUMM_FDS_V0_6_13_GENERATED_INPUT_FIX
    # AURORA_FCEUMM_FDS_V0_6_14_STATE_MEMSTREAM_ASPRINTF_FIX_AUDIT
    state_fds = files["state_fds.c"]
    general_fds = files["general_fds.c"]
    if '#include "endian.h"' in state_fds:
        fail("state_fds.c retained FILE-based endian.h")
    if '#include "fceu-endian.h"' not in state_fds:
        fail("state_fds.c missing MEM_TYPE-aware fceu-endian.h")
    if "AURORA_FCEUMM_FDS_V0_6_14_NO_FILE_STATE_SCAN" not in state_fds:
        fail("state_fds.c retained FILE-based FCEUSS_CheckStates")
    if "AURORA_FCEUMM_FDS_V0_6_14_LOCAL_ASPRINTF" not in general_fds:
        fail("general_fds.c did not enable local asprintf fallback")
    for generated_name in ("input_fds.c", "pads_fds.c"):
        if "\\t" in files[generated_name]:
            fail(f"{generated_name}: retained literal \\t escape in generated C")
    combined = "\n".join(files.values())
    forbidden = (
        "FCEU_ApplyPeriodicCheats(", "FCEU_LoadGameCheats(",
        "FCEU_FlushGameCheats(", "FCEU_CheatResetRAM(",
        "FCEU_CheatAddRAM(", "FCEU_PowerCheats(",
        "FCEUMOV_", "FCEUnetplay", "FCEUNET_",
        "FCEU_DrawMovies(", "FCEU_VSUniDraw(", "DrawNSF(",
    )
    for token in forbidden:
        if token in combined:
            fail(f"generated lean core retained forbidden runtime token: {token}")
    # ABI callbacks are deliberately still present and empty.
    if "void retro_cheat_reset(void)" not in files["libretro_fds.c"]:
        fail("libretro ABI cheat-reset stub was accidentally removed")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    core = args.core.resolve()
    out = args.out.resolve()
    paths = {
        "fceu": core / "src/fceu.c",
        "libretro": core / "src/drivers/libretro/libretro.c",
        "input": core / "src/input.c",
        "pads": core / "src/input/pads.c",
        "state": core / "src/state.c",
        "general": core / "src/general.c",
        "video": core / "src/video.c",
    }
    for path in paths.values():
        if not path.is_file():
            fail(f"missing pinned FCEUmm source: {path}")

    originals = {name: read(path) for name, path in paths.items()}
    audit_sources(
        originals["fceu"], originals["libretro"], originals["input"],
        originals["pads"], originals["state"], originals["general"],
        originals["video"]
    )

    generated = {
        "fceu_fds.c": patch_fceu(originals["fceu"]),
        "libretro_fds.c": patch_libretro(originals["libretro"]),
        "input_fds.c": INPUT_FDS_C,
        "pads_fds.c": PADS_FDS_C,
        "state_fds.c": patch_state(originals["state"]),
        "general_fds.c": patch_general(originals["general"]),
        "video_fds.c": patch_video(originals["video"]),
    }
    audit_generated(generated)
    for name, body in generated.items():
        write_atomic(out / name, body)

    print("[FCEUmm FDS prepare] generated lean FDS-only/two-pad overlays")
    print("[FCEUmm FDS prepare] removed runtime: cheat/movie/netplay/VS/peripherals")
    print("[FCEUmm FDS prepare] BIOS: <Aurora SYSTEM>/disksys.rom")
    for name in generated:
        print(f"  {out / name}")


if __name__ == "__main__":
    main()
