
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

"""
SNESticle Aurora - Hybrid NES backend patch
============================================

QuickNES remains the fast/default backend.

If QuickNES cannot load a .nes image for ANY reason (unsupported mapper,
unusual NES 2.0 board, etc.), SNESticle transparently retries the same ROM
with the already-vendored FCEUmm core.

This intentionally gives SNESticle access to every mapper/board implemented
by the current FCEUmm submodule without adding FCEUmm's cost to games that
already run in QuickNES.

Known targets:
- Famicom Jump II: Saikyou no 7 Nin -> mapper 153 / Bandai FCG
- SD Keiji Blader -> NES 2.0 mapper 552 / Taito X1-017

The patch also:
- namespaces FCEUmm's libretro symbols so QuickNES/FCEUmm can coexist
  in one static PS2 ELF;
- keeps QuickNES and FCEUmm save-state envelopes separate;
- dispatches SRAM, reset, audio and frame execution to the selected core;
- lets NES 2.0 mapper IDs >255 pass SNESticle's outer .nes gate;
- does not change SNES code, video geometry, audio gain, runtime data dirs,
  or the final SNESticle.elf filename.
"""

ROOT = Path.cwd()
MAKEFILE = ROOT / "Makefile"
NESSYSTEM = ROOT / "src/nes/quicknes/nessystem_quicknes.cpp"
FCE_BRIDGE = ROOT / "src/nes/fceumm/fceumm_bridge.cpp"
FCE_HEADER = ROOT / "src/nes/fceumm/fceumm_bridge.h"
PREFIX_HEADER = ROOT / "src/nes/fceumm/fceumm_symbol_prefix.h"
NESROM = ROOT / "src/nes/system/nesrom.cpp"
FCE_DIR = ROOT / "src/third_party/fceumm"

MARK_HYBRID = "AURORA_NES_HYBRID_FCEUMM_V1"
MARK_PREFIX = "AURORA_FCEUMM_SYMBOL_PREFIX_V1"
MARK_MAKE = "FCEUMM_SNESTICLE_BEGIN"
MARK_GATE = "AURORA_NES2_FALLBACK_GATE_V1"


def die(msg):
    print()
    print("ERRO:", msg)
    raise SystemExit(1)


def read(path):
    if not path.exists():
        die("arquivo ausente: " + str(path.relative_to(ROOT)))
    return path.read_text(encoding="utf-8")


def run(args, cwd=None, timeout=120):
    try:
        return subprocess.run(
            args,
            cwd=str(cwd or ROOT),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return None


def function_span(text, signature):
    start = text.find(signature)
    if start < 0:
        die("função não encontrada: " + signature)

    brace = text.find("{", start)
    if brace < 0:
        die("abre-chaves não encontrado: " + signature)

    depth = 0
    i = brace
    in_string = False
    in_char = False
    line_comment = False
    block_comment = False
    escape = False

    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""

        if line_comment:
            if c == "\n":
                line_comment = False
            i += 1
            continue

        if block_comment:
            if c == "*" and n == "/":
                block_comment = False
                i += 2
                continue
            i += 1
            continue

        if in_string:
            if escape:
                escape = False
            elif c == "\\":
                escape = True
            elif c == '"':
                in_string = False
            i += 1
            continue

        if in_char:
            if escape:
                escape = False
            elif c == "\\":
                escape = True
            elif c == "'":
                in_char = False
            i += 1
            continue

        if c == "/" and n == "/":
            line_comment = True
            i += 2
            continue

        if c == "/" and n == "*":
            block_comment = True
            i += 2
            continue

        if c == '"':
            in_string = True
            i += 1
            continue

        if c == "'":
            in_char = True
            i += 1
            continue

        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return start, i + 1

        i += 1

    die("função sem fechamento: " + signature)


def replace_function(text, signature, replacement):
    a, b = function_span(text, signature)
    return text[:a] + replacement + text[b:]


def insert_after_struct(text, struct_name, addition):
    pos = text.find("struct " + struct_name)
    if pos < 0:
        die("struct não encontrada: " + struct_name)

    brace = text.find("{", pos)
    if brace < 0:
        die("struct sem abre-chaves: " + struct_name)

    depth = 0
    i = brace
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                semi = text.find(";", i)
                if semi < 0:
                    die("struct sem ponto-e-vírgula: " + struct_name)
                return text[:semi + 1] + addition + text[semi + 1:]
        i += 1

    die("struct sem fechamento: " + struct_name)


print("============================================================")
print(" SNESticle Aurora - QuickNES + FCEUmm hybrid mapper fallback")
print("============================================================")
print()

if not (ROOT / ".git").exists():
    die("o diretório inicial não parece ser o repositório SNESticle")

for p in [MAKEFILE, NESSYSTEM, FCE_BRIDGE, FCE_HEADER, NESROM]:
    if not p.exists():
        die("arquivo necessário não encontrado: " + str(p.relative_to(ROOT)))

if not (FCE_DIR / "Makefile.libretro").exists():
    die(
        "submodule FCEUmm não inicializado em src/third_party/fceumm. "
        "Execute: git submodule update --init --recursive"
    )

original = {
    MAKEFILE: read(MAKEFILE),
    NESSYSTEM: read(NESSYSTEM),
    FCE_BRIDGE: read(FCE_BRIDGE),
    NESROM: read(NESROM),
}
modified = dict(original)

# ---------------------------------------------------------------------------
# 1) Namespace every libretro entry point exported by FCEUmm.
# ---------------------------------------------------------------------------

prefix_text = r'''#ifndef AURORA_FCEUMM_SYMBOL_PREFIX_H
#define AURORA_FCEUMM_SYMBOL_PREFIX_H

*(AURORA_FCEUMM_SYMBOL_PREFIX_V1)
 *
 * QuickNES and FCEUmm are both linked statically into the PS2 ELF.
 * Namespace FCEUmm's libretro ABI so a retro_* reference can never bind
 * to QuickNES's libretro.o (or vice versa).
 */

#define retro_init                       fceumm_retro_init
#define retro_deinit                     fceumm_retro_deinit
#define retro_api_version                fceumm_retro_api_version
#define retro_get_system_info            fceumm_retro_get_system_info
#define retro_get_system_av_info         fceumm_retro_get_system_av_info
#define retro_set_controller_port_device fceumm_retro_set_controller_port_device
#define retro_reset                      fceumm_retro_reset
#define retro_run                        fceumm_retro_run
#define retro_serialize_size             fceumm_retro_serialize_size
#define retro_serialize                  fceumm_retro_serialize
#define retro_unserialize                fceumm_retro_unserialize
#define retro_cheat_reset                fceumm_retro_cheat_reset
#define retro_cheat_set                  fceumm_retro_cheat_set
#define retro_load_game                  fceumm_retro_load_game
#define retro_load_game_special          fceumm_retro_load_game_special
#define retro_unload_game                fceumm_retro_unload_game
#define retro_get_region                 fceumm_retro_get_region
#define retro_get_memory_data            fceumm_retro_get_memory_data
#define retro_get_memory_size            fceumm_retro_get_memory_size
#define retro_set_environment            fceumm_retro_set_environment
#define retro_set_video_refresh          fceumm_retro_set_video_refresh
#define retro_set_audio_sample           fceumm_retro_set_audio_sample
#define retro_set_audio_sample_batch     fceumm_retro_set_audio_sample_batch
#define retro_set_input_poll             fceumm_retro_set_input_poll
#define retro_set_input_state            fceumm_retro_set_input_state

#endif
'''

modified[PREFIX_HEADER] = prefix_text
print("[ OK ] FCEUmm: namespace próprio para retro_*")


# Bridge must see the same names as the FCEUmm core.
s = modified[FCE_BRIDGE]

if '#include "fceumm_symbol_prefix.h"' not in s:
    anchor = '#include "libretro.h"'
    if anchor not in s:
        die("fceumm_bridge.cpp: include libretro.h não encontrado")
    s = s.replace(
        anchor,
        '#include "fceumm_symbol_prefix.h"\n' + anchor,
        1,
    )

modified[FCE_BRIDGE] = s
print("[ OK ] FCEUmm bridge: chamadas apontam para símbolos namespaced")


# ---------------------------------------------------------------------------
# 2) Outer SNESticle .nes gate: let mapper >255 NES 2.0 images through.
# ---------------------------------------------------------------------------

s = modified[NESROM]

if MARK_GATE not in s and "AURORA_NESROM_NES2_GATE_V1" not in s:
    old_mapper = "m_uMapperNo    = (m_uFlags6 >> 4) | (m_uFlags7 & 0xF0);"
    if old_mapper not in s:
        # A newer/full NES 2.0 parser may already be present.
        if (
            "pBuf[8]" in s
            and "m_uMapperNo" in s
            and ("NES2" in s or "NES 2.0" in s)
        ):
            print("[PASS] outer .nes gate já parece entender NES 2.0")
        else:
            die(
                "nesrom.cpp: parser não reconhecido; não vou alterar "
                "às cegas"
            )
    else:
        replacement = r'''/* AURORA_NES2_FALLBACK_GATE_V1
     * Decode the 12-bit NES 2.0 mapper number here too. The backend that
     * ultimately accepts the ROM (usually FCEUmm for large mapper IDs)
     * performs the authoritative full header/size validation.
     */
    Bool bAuroraNES2 = (m_uFlags7 & 0x0C) == 0x08;
    if (bAuroraNES2)
    {
        m_uMapperNo =
            (m_uFlags6 >> 4) |
            (m_uFlags7 & 0xF0) |
            ((Uint32)(pBuf[8] & 0x0F) << 8);
    }
    else
    {
        m_uMapperNo = (m_uFlags6 >> 4) | (m_uFlags7 & 0xF0);
    }'''
        s = s.replace(old_mapper, replacement, 1)

        # The legacy wrapper's size formula is iNES-1-only. For NES 2.0,
        # don't reject before FCEUmm/QuickNES gets to parse the real header.
        old_check = "if (uTotal < uExpected)"
        if old_check in s:
            s = s.replace(
                old_check,
                "if (!bAuroraNES2 && uTotal < uExpected)",
                1,
            )
        else:
            die("nesrom.cpp: size guard legado não encontrado")

        print("[ OK ] outer .nes gate: mapper NES 2.0 de 12 bits")
elif "AURORA_NESROM_NES2_GATE_V1" in s:
    print("[PASS] outer .nes gate: parser NES 2.0 completo já presente")
else:
    print("[PASS] outer .nes gate: fallback NES 2.0 já presente")

modified[NESROM] = s


# ---------------------------------------------------------------------------
# 3) Convert NesSystem into a QuickNES-first / FCEUmm-fallback dispatcher.
# ---------------------------------------------------------------------------

s = modified[NESSYSTEM]

if '#include "nes/fceumm/fceumm_bridge.h"' not in s:
    anchor = '#include "nes/quicknes/quicknes_bridge.h"'
    if anchor not in s:
        die("nessystem_quicknes.cpp: include QuickNES bridge não encontrado")
    s = s.replace(
        anchor,
        anchor + '\n#include "nes/fceumm/fceumm_bridge.h"',
        1,
    )

if MARK_HYBRID not in s:
    hybrid_defs = r'''

/* AURORA_NES_HYBRID_FCEUMM_V1
 *
 * QuickNES is still the default/fast path. FCEUmm is instantiated only
 * for a cartridge QuickNES rejects. This extends mapper coverage without
 * changing QuickNES execution for already-supported games.
 */
#define FCEUMM_STATE_MAGIC   0x54534346u /* FCST */
#define FCEUMM_STATE_VERSION 1u

struct FceummStateHeaderT
{
    Uint32 uMagic;
    Uint32 uVersion;
    Uint32 nPayloadBytes;
    Uint32 uFrame;
    Uint32 uLine;
    Uint32 Reserved[3];
};

enum NesBackendE
{
    NES_BACKEND_NONE = 0,
    NES_BACKEND_QUICKNES,
    NES_BACKEND_FCEUMM
};

static NesBackendE s_eNesBackend = NES_BACKEND_NONE;
'''

    s = insert_after_struct(s, "QuicknesStateHeaderT", hybrid_defs)
else:
    print("[PASS] NesSystem híbrido já marcado")


constructor = r'''NesSystem::NesSystem()
{
    m_pNesRom = NULL;
    m_pNesDisk = NULL;
    m_pCHRRam = NULL;
    m_bInitialized = FALSE;
    m_bRomReady = FALSE;
    m_uFrameTick = 0;
    m_uFrame = 0;
    m_uLine = 0;
    s_eNesBackend = NES_BACKEND_NONE;
}'''

destructor = r'''NesSystem::~NesSystem()
{
    QuicknesBridge_Shutdown();
    FceummBridge_Shutdown();
    s_eNesBackend = NES_BACKEND_NONE;

    if (m_pCHRRam)
    {
        free(m_pCHRRam);
        m_pCHRRam = NULL;
    }
}'''

setrom = r'''void NesSystem::SetRom(Emu::Rom *pRom)
{
    m_pNesRom = NULL;
    m_pNesDisk = NULL;
    m_bRomReady = FALSE;
    m_uFrameTick = m_uFrame = m_uLine = 0;

    QuicknesBridge_UnloadGame();
    FceummBridge_UnloadGame();
    s_eNesBackend = NES_BACKEND_NONE;

    if (!pRom)
        return;

    if (pRom != _pNesRom)
    {
        printf("[NesSystem/hybrid] FDS/non-cartridge unsupported\n");
        return;
    }

    NesRom *rom = (NesRom *)pRom;
    Uint8 *data = rom->GetData();
    Uint32 bytes = rom->GetBytes();

    if (!data || bytes < 16 ||
        data[0] != 'N' || data[1] != 'E' ||
        data[2] != 'S' || data[3] != 0x1a)
    {
        printf("[NesSystem/hybrid] invalid iNES/NES2 ROM (%u bytes)\n",
               (unsigned)bytes);
        return;
    }

    /* Fast path: preserve current QuickNES behavior for every ROM it knows. */
    if (QuicknesBridge_LoadGame(data, (size_t)bytes, NULL))
    {
        s_eNesBackend = NES_BACKEND_QUICKNES;
        printf("[NesSystem/hybrid] mapper %u -> QuickNES\n",
               (unsigned)rom->GetMapperNumber());
    }
    else
    {
        /* QuickNES can leave its native emulator initialized after a rejected
         * cart. Unload it before giving the exact same raw image to FCEUmm. */
        QuicknesBridge_UnloadGame();

        printf("[NesSystem/hybrid] QuickNES rejected mapper %u; trying FCEUmm\n",
               (unsigned)rom->GetMapperNumber());

        if (!FceummBridge_LoadGame(data, bytes, NULL))
        {
            printf("[NesSystem/hybrid] LOAD FAILED in both NES cores\n");
            return;
        }

        s_eNesBackend = NES_BACKEND_FCEUMM;
        printf("[NesSystem/hybrid] mapper %u -> FCEUmm fallback\n",
               (unsigned)rom->GetMapperNumber());
    }

    m_pNesRom = rom;
    m_bInitialized = TRUE;
    m_bRomReady = TRUE;

    printf("[NesSystem/hybrid] LOAD OK; backend=%s SRAM=%d\n",
           s_eNesBackend == NES_BACKEND_QUICKNES ? "QuickNES" : "FCEUmm",
           GetSRAMBytes());
}'''

reset = r'''void NesSystem::Reset()
{
    m_uFrameTick = m_uFrame = m_uLine = 0;

    if (!m_bRomReady)
        return;

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
        QuicknesBridge_Reset();
    else if (s_eNesBackend == NES_BACKEND_FCEUMM)
        FceummBridge_Reset();
}'''

softreset = r'''void NesSystem::SoftReset()
{
    m_uFrameTick = m_uFrame = m_uLine = 0;

    if (!m_bRomReady)
        return;

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
        QuicknesBridge_SoftReset();
    else if (s_eNesBackend == NES_BACKEND_FCEUMM)
        FceummBridge_Reset();
}'''

execute = r'''void NesSystem::ExecuteFrame(Emu::SysInputT *pInput,
                             CRenderSurface *pTarget,
                             CMixBuffer *pMixBuf,
                             ModeE eMode)
{
    (void)eMode;

    if (!m_bRomReady || !pTarget)
    {
        if (pTarget)
            DiagnosticPaint(pTarget);
        ++m_uFrameTick;
        ++m_uFrame;
        return;
    }

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
        QuicknesBridge_RunFrame(pInput, pTarget, pMixBuf);
    else if (s_eNesBackend == NES_BACKEND_FCEUMM)
        FceummBridge_RunFrame(pInput, pTarget, pMixBuf);

    ++m_uFrameTick;
    ++m_uFrame;
    m_uLine = 0;
}'''

getstatesize = r'''Int32 NesSystem::GetStateSize()
{
    if (!m_bRomReady)
        return 0;

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
    {
        Int32 bytes = (Int32)(
            sizeof(QuicknesStateHeaderT) + QUICKNES_STATE_CAPACITY
        );
        return bytes <= (Int32)sizeof(NesStateT) ? bytes : 0;
    }

    if (s_eNesBackend == NES_BACKEND_FCEUMM)
    {
        Int32 payload = FceummBridge_GetStateSize();
        if (payload <= 0)
            return 0;

        unsigned long total =
            sizeof(FceummStateHeaderT) + (unsigned long)payload;

        /* _NesState in mainloop_state.cpp is still a NesStateT. Refuse a
         * pathological core state rather than ever writing past it. */
        if (total > sizeof(NesStateT))
        {
            printf("[FCEUmm/state] state too large: %lu > %u\n",
                   total, (unsigned)sizeof(NesStateT));
            return 0;
        }

        return (Int32)total;
    }

    return 0;
}'''

save_generic = r'''void NesSystem::SaveState(void *pState, Int32 nStateBytes)
{
    Int32 required = GetStateSize();
    if (pState && required > 0 && nStateBytes >= required)
        SaveState((NesStateT *)pState);
}'''

restore_generic = r'''void NesSystem::RestoreState(void *pState, Int32 nStateBytes)
{
    Int32 required = GetStateSize();
    if (pState && required > 0 && nStateBytes >= required)
        RestoreState((NesStateT *)pState);
}'''

save_typed = r'''void NesSystem::SaveState(NesStateT *pState)
{
    if (!pState || !m_bRomReady)
        return;

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
    {
        const size_t head = sizeof(QuicknesStateHeaderT);
        const Int32 stateBytes = (Int32)(
            head + QUICKNES_STATE_CAPACITY
        );

        memset(pState, 0, (size_t)stateBytes);

        int payload = QuicknesBridge_SaveState(
            ((Uint8 *)pState) + head,
            QUICKNES_STATE_CAPACITY
        );

        if (payload <= 0 || payload > QUICKNES_STATE_CAPACITY)
        {
            memset(pState, 0, (size_t)stateBytes);
            printf("[QuickNES/state] serialize failed\n");
            return;
        }

        QuicknesStateHeaderT h;
        memset(&h, 0, sizeof(h));
        h.uMagic = QUICKNES_STATE_MAGIC;
        h.uVersion = QUICKNES_STATE_VERSION;
        h.nPayloadBytes = (Uint32)payload;
        h.uFrame = m_uFrame;
        h.uLine = m_uLine;

        /* Header LAST: a failed/partial snapshot never looks valid. */
        memcpy(pState, &h, sizeof(h));

        printf("[QuickNES/state] snapshot committed: native=%d envelope=%d\n",
               payload, (int)stateBytes);
        return;
    }

    if (s_eNesBackend == NES_BACKEND_FCEUMM)
    {
        Int32 payload = FceummBridge_GetStateSize();
        if (payload <= 0)
            return;

        size_t head = sizeof(FceummStateHeaderT);
        size_t total = head + (size_t)payload;

        if (total > sizeof(NesStateT))
        {
            printf("[FCEUmm/state] state too large\n");
            return;
        }

        memset(pState, 0, total);

        if (!FceummBridge_SaveState(
                ((Uint8 *)pState) + head, payload))
        {
            memset(pState, 0, total);
            printf("[FCEUmm/state] serialize failed\n");
            return;
        }

        FceummStateHeaderT h;
        memset(&h, 0, sizeof(h));
        h.uMagic = FCEUMM_STATE_MAGIC;
        h.uVersion = FCEUMM_STATE_VERSION;
        h.nPayloadBytes = (Uint32)payload;
        h.uFrame = m_uFrame;
        h.uLine = m_uLine;

        memcpy(pState, &h, sizeof(h));

        printf("[FCEUmm/state] snapshot committed: native=%d envelope=%u\n",
               payload, (unsigned)total);
    }
}'''

restore_typed = r'''Bool NesSystem::RestoreState(NesStateT *pState)
{
    if (!pState || !m_bRomReady)
        return FALSE;

    Uint32 magic = 0;
    memcpy(&magic, pState, sizeof(magic));

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
    {
        if (magic != QUICKNES_STATE_MAGIC)
            return FALSE;

        QuicknesStateHeaderT h;
        memcpy(&h, pState, sizeof(h));

        const size_t head = sizeof(h);
        const size_t stateBytes =
            head + (size_t)QUICKNES_STATE_CAPACITY;

        if (h.uVersion != QUICKNES_STATE_VERSION ||
            h.nPayloadBytes == 0 ||
            h.nPayloadBytes > QUICKNES_STATE_CAPACITY ||
            head + (size_t)h.nPayloadBytes > stateBytes)
        {
            return FALSE;
        }

        if (!QuicknesBridge_LoadState(
                ((const Uint8 *)pState) + head,
                (int)h.nPayloadBytes))
        {
            return FALSE;
        }

        m_uFrame = h.uFrame;
        m_uLine = h.uLine;
        m_uFrameTick = h.uFrame;
        return TRUE;
    }

    if (s_eNesBackend == NES_BACKEND_FCEUMM)
    {
        if (magic != FCEUMM_STATE_MAGIC)
            return FALSE;

        FceummStateHeaderT h;
        memcpy(&h, pState, sizeof(h));

        Int32 currentPayload = FceummBridge_GetStateSize();
        size_t head = sizeof(h);

        if (h.uVersion != FCEUMM_STATE_VERSION ||
            h.nPayloadBytes == 0 ||
            currentPayload <= 0 ||
            h.nPayloadBytes > (Uint32)(sizeof(NesStateT) - head))
        {
            return FALSE;
        }

        if (!FceummBridge_LoadState(
                ((const Uint8 *)pState) + head,
                (Int32)h.nPayloadBytes))
        {
            return FALSE;
        }

        m_uFrame = h.uFrame;
        m_uLine = h.uLine;
        m_uFrameTick = h.uFrame;
        return TRUE;
    }

    return FALSE;
}'''

getsrambytes = r'''Int32 NesSystem::GetSRAMBytes()
{
    if (!m_bRomReady)
        return 0;

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
        return QuicknesBridge_GetSRAMBytes();

    if (s_eNesBackend == NES_BACKEND_FCEUMM)
        return FceummBridge_GetSRAMBytes();

    return 0;
}'''

getsramdata = r'''Uint8 *NesSystem::GetSRAMData()
{
    if (!m_bRomReady)
        return NULL;

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
        return (Uint8 *)QuicknesBridge_GetSRAMData();

    if (s_eNesBackend == NES_BACKEND_FCEUMM)
        return FceummBridge_GetSRAMData();

    return NULL;
}'''

getsamplerate = r'''Uint32 NesSystem::GetSampleRate()
{
    if (s_eNesBackend == NES_BACKEND_FCEUMM)
        return FceummBridge_GetSampleRate();

    return QuicknesBridge_GetSampleRate();
}'''

replacements = [
    ("NesSystem::NesSystem()", constructor),
    ("NesSystem::~NesSystem()", destructor),
    ("void NesSystem::SetRom(", setrom),
    ("void NesSystem::Reset()", reset),
    ("void NesSystem::SoftReset()", softreset),
    ("void NesSystem::ExecuteFrame(", execute),
    ("Int32 NesSystem::GetStateSize()", getstatesize),
    ("void NesSystem::SaveState(void *pState", save_generic),
    ("void NesSystem::RestoreState(void *pState", restore_generic),
    ("void NesSystem::SaveState(NesStateT *pState)", save_typed),
    ("Bool NesSystem::RestoreState(NesStateT *pState)", restore_typed),
    ("Int32 NesSystem::GetSRAMBytes()", getsrambytes),
    ("Uint8 *NesSystem::GetSRAMData()", getsramdata),
    ("Uint32 NesSystem::GetSampleRate()", getsamplerate),
]

for signature, replacement in replacements:
    s = replace_function(s, signature, replacement)

modified[NESSYSTEM] = s
print("[ OK ] NesSystem: QuickNES-first + FCEUmm fallback")
print("[ OK ] save states: envelopes QuickNES/FCEUmm separados")
print("[ OK ] SRAM/reset/frame/audio despachados por backend")


# ---------------------------------------------------------------------------
# 4) Makefile: compile bridge, build namespaced FCEUmm static core, link both.
# ---------------------------------------------------------------------------

s = modified[MAKEFILE]

if MARK_MAKE not in s:
    qn_block_end = "# QUICKNES_SNESTICLE_END"
    if qn_block_end not in s:
        die("Makefile: bloco QUICKNES não encontrado")

    fce_vars = r'''

# FCEUMM_SNESTICLE_BEGIN
# Accuracy/mapper fallback. QuickNES remains the default NES backend.
FCEUMM_DIR ?= $(CURDIR)/src/third_party/fceumm
FCEUMM_LIB ?= $(FCEUMM_DIR)/fceumm_libretro_ps2.a
FCEUMM_PREFIX_HDR := $(CURDIR)/src/nes/fceumm/fceumm_symbol_prefix.h
# FCEUMM_SNESTICLE_END
'''
    s = s.replace(qn_block_end, qn_block_end + fce_vars, 1)

# Add bridge source next to QuickNES bridge.
if "src/nes/fceumm/fceumm_bridge.cpp" not in s:
    anchor = "src/nes/quicknes/quicknes_bridge.cpp"
    pos = s.find(anchor)
    if pos < 0:
        die("Makefile: quicknes_bridge.cpp não encontrado na lista de fontes")

    line_end = s.find("\n", pos)
    if line_end < 0:
        die("Makefile: linha quicknes_bridge.cpp inválida")

    line = s[pos:line_end]
    indent = re.match(r"\s*", s[s.rfind("\n", 0, pos) + 1:pos]).group(0)

    # Preserve whether this source-list line has a continuation backslash.
    has_slash = line.rstrip().endswith("\\")
    new_line = (
        "\n" + indent + "src/nes/fceumm/fceumm_bridge.cpp" +
        (" \\" if has_slash else "")
    )
    s = s[:line_end] + new_line + s[line_end:]

# Build rule inserted immediately before final ELF target rule.
if "AURORA_FCEUMM_BUILD_RULE_V1" not in s:
    target_pos = s.find("$(TARGET):")
    if target_pos < 0:
        die("Makefile: regra $(TARGET) não encontrada")

    build_rule = r'''
# AURORA_FCEUMM_BUILD_RULE_V1
.PHONY: FORCE_FCEUMM
FORCE_FCEUMM:

$(FCEUMM_LIB): FORCE_FCEUMM $(FCEUMM_PREFIX_HDR)
	@echo "[FCEUmm] Building mapper fallback core..."
	@PATH="$$PS2DEV/bin:$$PS2SDK/bin:$$PATH" \
	$(MAKE) -C "$(FCEUMM_DIR)" -f Makefile.libretro platform=ps2 \
		CC="$(EE_CC) -include $(FCEUMM_PREFIX_HDR)" \
		CXX="$(EE_CXX) -include $(FCEUMM_PREFIX_HDR)"

'''
    s = s[:target_pos] + build_rule + s[target_pos:]

# Add FCEUmm archive to target prerequisites.
target_match = re.search(r"^\$\(TARGET\):[^\n]*$", s, re.M)
if not target_match:
    die("Makefile: linha de prerequisites de $(TARGET) não encontrada")

target_line = target_match.group(0)
if "$(FCEUMM_LIB)" not in target_line:
    if "$(QUICKNES_LIB)" not in target_line:
        die("Makefile: $(TARGET) não depende de QUICKNES_LIB")
    new_target_line = target_line.replace(
        "$(QUICKNES_LIB)",
        "$(QUICKNES_LIB) $(FCEUMM_LIB)",
        1,
    )
    s = s[:target_match.start()] + new_target_line + s[target_match.end():]

# Add FCEUmm archive to the actual linker command.
target_pos = s.find("$(TARGET):")
next_rule = s.find("\n\n", target_pos)
window_end = next_rule if next_rule >= 0 else len(s)
window = s[target_pos:window_end]

if '"$(FCEUMM_LIB)"' not in window:
    qn_token = '"$(QUICKNES_LIB)"'
    qn_pos = window.find(qn_token)
    if qn_pos < 0:
        # Some Makefiles do not quote the archive.
        qn_token = "$(QUICKNES_LIB)"
        qn_pos = window.find(qn_token)

    if qn_pos < 0:
        die("Makefile: QUICKNES_LIB não encontrado na linha de link")

    # Skip the prerequisite occurrence if unquoted.
    if qn_token == "$(QUICKNES_LIB)":
        qn_pos = window.find(qn_token, window.find("\n") + 1)
        if qn_pos < 0:
            die("Makefile: QUICKNES_LIB não encontrado no comando linker")

    insert_pos = target_pos + qn_pos + len(qn_token)
    s = s[:insert_pos] + ' "$(FCEUMM_LIB)"' + s[insert_pos:]

modified[MAKEFILE] = s
print("[ OK ] Makefile: FCEUmm PS2 estático + bridge adicionados")
print("[ OK ] link: QuickNES e FCEUmm coexistem sem retro_* collision")


# ---------------------------------------------------------------------------
# 5) Sanity checks before writing.
# ---------------------------------------------------------------------------

checks = [
    (modified[NESSYSTEM], MARK_HYBRID, "dispatcher híbrido"),
    (modified[NESSYSTEM], "FceummBridge_LoadGame", "fallback load"),
    (modified[NESSYSTEM], "FCEUMM_STATE_MAGIC", "FCEUmm state envelope"),
    (modified[FCE_BRIDGE], '#include "fceumm_symbol_prefix.h"', "bridge prefix"),
    (modified[PREFIX_HEADER], MARK_PREFIX, "prefix header"),
    (modified[MAKEFILE], "FCEUMM_LIB", "FCEUmm library"),
    (modified[MAKEFILE], "src/nes/fceumm/fceumm_bridge.cpp", "FCE bridge source"),
    (modified[MAKEFILE], "$(FCEUMM_LIB): FORCE_FCEUMM", "FCE build rule"),
]

for text, needle, label in checks:
    if needle not in text:
        die("sanity falhou: " + label)

if "SNESticle.elf" not in modified[MAKEFILE]:
    die("proteção: Makefile deixou de conter SNESticle.elf")

# Absolutely no SNES source is touched by this batch.
for p in modified:
    rel = str(p.relative_to(ROOT))
    if rel.startswith("src/snes/"):
        die("proteção: arquivo SNES entrou no batch")

print("[PASS] sanity estrutural")


# ---------------------------------------------------------------------------
# 6) Backup, write, diff check; rollback if generated patch is malformed.
# ---------------------------------------------------------------------------

stamp = time.strftime("%Y%m%d-%H%M%S")
backup_dir = ROOT / ".git" / ("aurora-hybrid-fceumm-" + stamp)
backup_dir.mkdir(parents=True, exist_ok=True)

targets = set(modified)
existed = {}

for p in targets:
    existed[p] = p.exists()
    if p.exists():
        dst = backup_dir / p.relative_to(ROOT)
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, dst)


def rollback():
    print("[ROLLBACK] restaurando arquivos anteriores...")
    for p in targets:
        src = backup_dir / p.relative_to(ROOT)
        if existed[p]:
            if src.exists():
                p.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, p)
        else:
            if p.exists():
                p.unlink()


try:
    for p, text in modified.items():
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8")

    r = run(["git", "diff", "--check"])
    if r is None or r.returncode != 0:
        raise RuntimeError(
            "git diff --check falhou:\n" +
            (r.stdout if r else "timeout")
        )

except Exception as exc:
    rollback()
    die(str(exc))


# ---------------------------------------------------------------------------
# 7) Report.
# ---------------------------------------------------------------------------

print()
print("============================================================")
print(" RESULTADO")
print("============================================================")
print()
print("QuickNES:")
print("  - continua sendo o backend padrão")
print("  - jogos já suportados não passam pelo FCEUmm durante execução")
print()
print("FCEUmm fallback:")
print("  - tenta automaticamente qualquer .nes rejeitado pelo QuickNES")
print("  - herda toda a tabela de mappers/boards do submodule FCEUmm atual")
print("  - inclui mapper 153 / Bandai FCG (Famicom Jump II)")
print("  - inclui NES 2.0 mapper 552 / Taito X1-017 (SD Keiji Blader)")
print()
print("Save/SRAM:")
print("  - QuickNES mantém QNST e seu caminho de state atual")
print("  - FCEUmm usa envelope FCST separado")
print("  - SRAM é consultada no backend realmente ativo")
print()
print("Backup:")
print(" ", backup_dir.relative_to(ROOT))
print()
print("Agora rode:")
print("  make")
print("ou:")
print("  ./copy.sh")
print()
print("Testes prioritários:")
print("  1. Donkey Kong / mapper 0          -> deve continuar QuickNES")
print("  2. Famicom Jump II / mapper 153   -> deve imprimir FCEUmm fallback")
print("  3. SD Keiji Blader / mapper 552   -> deve imprimir FCEUmm fallback")
print("  4. Dragon Buster / mapper 95      -> QuickNES se seu mapper 95 local estiver OK;")
print("                                      senão terá segunda chance no FCEUmm")
print("  5. save/load state em um QuickNES e em um FCEUmm")
print()
print("Para ver exatamente o que mudou:")
print("  git diff -- Makefile src/nes/quicknes/nessystem_quicknes.cpp \\")
print("    src/nes/fceumm/fceumm_bridge.cpp src/nes/fceumm/fceumm_symbol_prefix.h \\")
print("    src/nes/system/nesrom.cpp")
print()
print("[PASS] patch gravado; git diff --check passou")
