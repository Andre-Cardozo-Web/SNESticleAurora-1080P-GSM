from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path.cwd().resolve()

MAKEFILE = ROOT / "Makefile"
CHR_CACHE = ROOT / "src/snes/ppu/snppuchrcache.h"
SNSPC_H = ROOT / "src/snes/apu/snspc.h"
SNSPC_C = ROOT / "src/snes/apu/snspc.c"
SNES_CPP = ROOT / "src/snes/core/snes.cpp"
GITMODULES = ROOT / ".gitmodules"
README = ROOT / "README.md"
CREDITS = ROOT / "CREDITS.md"
NES_README = ROOT / "src/nes/README.md"
INTRO = ROOT / "src/platform/ps2/system/mainloop_init.cpp"
THIRD_PARTY = ROOT / "THIRD_PARTY.md"
SMB2_FILE = ROOT / "src/snes/ppu/snppubg.cpp"

TOPGEAR_MARKER = "AURORA_TOPGEAR_HFLIP_CACHE"
RESET_MARKER = "AURORA_SPC_SOFT_RESET"
QUICKNES_MARKER = "AURORA_QUICKNES_CREDIT"
QUICKNES_PATH = "src/third_party/quicknes"
QUICKNES_URL = "https://github.com/itsveenee/QuickNES_Core.git"


def die(msg):
    print(f"\nERRO: {msg}")
    raise SystemExit(1)


def read(path):
    if not path.exists():
        die(f"arquivo não encontrado: {path.relative_to(ROOT)}")
    return path.read_text(encoding="utf-8")


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        die(f"{label}: esperava 1 ocorrência, encontrei {count}")
    return text.replace(old, new, 1)


def run(args, timeout=None):
    try:
        return subprocess.run(
            args,
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return None


if not (ROOT / ".git").exists():
    die("execute este script na raiz do repositório SNESticle Aurora")

files = [
    MAKEFILE,
    CHR_CACHE,
    SNSPC_H,
    SNSPC_C,
    SNES_CPP,
    GITMODULES,
    README,
    CREDITS,
    NES_README,
    INTRO,
]

original = {p: read(p) for p in files}
modified = dict(original)
third_party_original = (
    THIRD_PARTY.read_text(encoding="utf-8") if THIRD_PARTY.exists() else None
)

print("====================================================")
print(" SNESticle Aurora - Top Gear / QuickNES / soft reset (v2)")
print("====================================================\n")

# ---------------------------------------------------------------------
# SMB2: já testado pelo usuário. Não tocar no código do fix.
# ---------------------------------------------------------------------
if SMB2_FILE.exists():
    print("[PASS] SMB2: snppubg.cpp será deixado intacto")

# ---------------------------------------------------------------------
# TOP GEAR: cache da forma H-flipada de linhas CHR 4bpp.
# ---------------------------------------------------------------------
s = modified[MAKEFILE]

if "SNES_CHR_HFLIP_CACHE ?=" not in s:
    anchor = "SNES_BG_CACHE ?= 0\n"
    if s.count(anchor) != 1:
        die("Makefile: não encontrei exatamente 'SNES_BG_CACHE ?= 0'")

    s = s.replace(
        anchor,
        anchor
        + "\n"
        + "# Cache the H-flipped form of decoded 4bpp CHR rows.\n"
        + "# Avoids recalculating the 64-bit byte reversal on every cache hit.\n"
        + "# Set to 0 for A/B testing.\n"
        + "SNES_CHR_HFLIP_CACHE ?= 1\n",
        1,
    )

flag = "-DSNPPU_CHR_CACHE_HFLIP=$(SNES_CHR_HFLIP_CACHE)"
if flag not in s:
    # The Aurora Makefile may have SNPPU_BG_CACHE either as the final
    # define in CFLAGS/CXXFLAGS (no trailing backslash) or followed by
    # more defines (with a trailing backslash). Handle both layouts.
    lines = s.splitlines(keepends=True)
    indexes = []

    for i, line in enumerate(lines):
        body = line.rstrip("\r\n")
        normalized = body.rstrip()

        if normalized.endswith("\\"):
            normalized = normalized[:-1].rstrip()

        if normalized == "-DSNPPU_BG_CACHE=$(SNES_BG_CACHE)":
            indexes.append(i)

    if len(indexes) != 2:
        die(
            "Makefile: esperava localizar SNPPU_BG_CACHE exatamente "
            f"2 vezes (CFLAGS/CXXFLAGS), encontrei {len(indexes)}"
        )

    for i in reversed(indexes):
        line = lines[i]
        eol = "\n" if line.endswith("\n") else ""
        body = line[:-1] if eol else line

        if body.rstrip().endswith("\\"):
            # There are already more flags after this one. Insert a
            # continued define and leave the rest of the block intact.
            lines.insert(
                i + 1,
                "\t-DSNPPU_CHR_CACHE_HFLIP="
                "$(SNES_CHR_HFLIP_CACHE) \\\n"
            )
        else:
            # SNPPU_BG_CACHE is currently the last define. Turn it into
            # a continued line and make the new define the final one.
            lines[i] = body + " \\\n"
            lines.insert(
                i + 1,
                "\t-DSNPPU_CHR_CACHE_HFLIP="
                "$(SNES_CHR_HFLIP_CACHE)\n"
            )

    s = "".join(lines)

modified[MAKEFILE] = s

s = modified[CHR_CACHE]
if TOPGEAR_MARKER not in s:
    s = replace_once(
        s,
        '#include "types.h"\n',
        '#include "types.h"\n\n'
        '/* AURORA_TOPGEAR_HFLIP_CACHE\n'
        ' * Cache a pre-flipped copy of decoded 4bpp CHR rows.\n'
        ' * Both orientations share the same VRAM validity bitmap.\n'
        ' */\n'
        '#ifndef SNPPU_CHR_CACHE_HFLIP\n'
        '#define SNPPU_CHR_CACHE_HFLIP 1\n'
        '#endif\n',
        "CHR cache: include/marker",
    )

    old_struct = (
        "\tUint64 uData4[SNPPU_CHR4_TILE_COUNT][8];\n"
        "\tUint8  uOpaque4[SNPPU_CHR4_TILE_COUNT][8];\n"
        "\tUint8  uValid4[SNPPU_CHR4_TILE_COUNT];\n"
    )
    new_struct = (
        "\tUint64 uData4[SNPPU_CHR4_TILE_COUNT][8];\n"
        "\tUint8  uOpaque4[SNPPU_CHR4_TILE_COUNT][8];\n\n"
        "#if SNPPU_CHR_CACHE_HFLIP\n"
        "\tUint64 uData4HFlip[SNPPU_CHR4_TILE_COUNT][8];\n"
        "\tUint8  uOpaque4HFlip[SNPPU_CHR4_TILE_COUNT][8];\n"
        "#endif\n\n"
        "\tUint8  uValid4[SNPPU_CHR4_TILE_COUNT];\n"
    )
    s = replace_once(s, old_struct, new_struct, "CHR cache: storage")

    lookup_start = s.find("_INLINE Bool SnesPPUChrCacheLookup4(")
    lookup_end = s.find("_INLINE void SnesPPUChrCacheStore2(", lookup_start)
    if lookup_start < 0 or lookup_end < 0:
        die("CHR cache: não consegui isolar SnesPPUChrCacheLookup4")

    new_lookup = '''_INLINE Bool SnesPPUChrCacheLookup4(const SnesPPUChrCacheT *pCache,
\tUint32 uRowAddress, Bool bHFlip, Uint64 *pData, Uint32 *pOpaque)
{
\tUint32 uAddress = uRowAddress & SNPPU_VRAM_WORD_MASK;
\tUint32 uTile = uAddress >> 4;
\tUint32 uRow = uAddress & 7u;

\tif (!(pCache->uValid4[uTile] & (1u << uRow)))
\t\treturn FALSE;

#if SNPPU_CHR_CACHE_HFLIP
\tif (bHFlip)
\t{
\t\t*pData = pCache->uData4HFlip[uTile][uRow];
\t\t*pOpaque = pCache->uOpaque4HFlip[uTile][uRow];
\t}
\telse
\t{
\t\t*pData = pCache->uData4[uTile][uRow];
\t\t*pOpaque = pCache->uOpaque4[uTile][uRow];
\t}
#else
\t*pData = pCache->uData4[uTile][uRow];
\t*pOpaque = pCache->uOpaque4[uTile][uRow];
\tif (bHFlip)
\t\tSnesPPUChrCacheFlipRow(pData, pOpaque);
#endif
\treturn TRUE;
}

'''
    s = s[:lookup_start] + new_lookup + s[lookup_end:]

    store_start = s.find("_INLINE void SnesPPUChrCacheStore4(")
    store_end = s.find("_INLINE void SnesPPUChrCacheInvalidateAll(", store_start)
    if store_start < 0 or store_end < 0:
        die("CHR cache: não consegui isolar SnesPPUChrCacheStore4")

    new_store = '''_INLINE void SnesPPUChrCacheStore4(SnesPPUChrCacheT *pCache,
\tUint32 uRowAddress, Uint64 uData, Uint32 uOpaque)
{
\tUint32 uAddress = uRowAddress & SNPPU_VRAM_WORD_MASK;
\tUint32 uTile = uAddress >> 4;
\tUint32 uRow = uAddress & 7u;

\tpCache->uData4[uTile][uRow] = uData;
\tpCache->uOpaque4[uTile][uRow] = (Uint8)uOpaque;

#if SNPPU_CHR_CACHE_HFLIP
\tpCache->uData4HFlip[uTile][uRow] = SnesPPUChrCacheReverseBytes(uData);
\tpCache->uOpaque4HFlip[uTile][uRow] =
\t\tSnesPPUChrCacheReverseMask((Uint8)uOpaque);
#endif

\tpCache->uValid4[uTile] |= (Uint8)(1u << uRow);
}

'''
    s = s[:store_start] + new_store + s[store_end:]
    modified[CHR_CACHE] = s
    print("[ OK ] Top Gear: H-flip CHR cache preparado")
else:
    print("[SKIP] Top Gear: patch já aplicado")

# ---------------------------------------------------------------------
# SNES SOFT RESET / Super Castlevania IV.
# Preserve APURAM, but restart SPC registers/counters/IPL ROM.
# ---------------------------------------------------------------------
s = modified[SNSPC_H]
if "void SNSPCSoftReset(" not in s:
    s = replace_once(
        s,
        "void SNSPCResetRegs(SNSpcT *pCpu);\n"
        "void SNSPCReset(SNSpcT *pCpu, Bool bHardReset);\n",
        "void SNSPCResetRegs(SNSpcT *pCpu);\n"
        "void SNSPCReset(SNSpcT *pCpu, Bool bHardReset);\n"
        "void SNSPCSoftReset(SNSpcT *pCpu);\n",
        "snspc.h: declaração SNSPCSoftReset",
    )
modified[SNSPC_H] = s

s = modified[SNSPC_C]
if RESET_MARKER not in s:
    anchor = "Uint8 SNSPCPeek8(SNSpcT *pCpu, Uint32 uAddr)\n"
    if s.count(anchor) != 1:
        die("snspc.c: não encontrei SNSPCPeek8 como âncora")

    helper = '''/* AURORA_SPC_SOFT_RESET
 * Restart SPC700 from the IPL ROM without clearing APURAM.
 */
void SNSPCSoftReset(SNSpcT *pCpu)
{
\tSNSPCResetRegs(pCpu);
\tSNSPCResetCounters(pCpu);

\t/* Re-enable the IPL ROM while preserving hidden $FFC0-$FFFF RAM. */
\tSNSPCSetRomEnable(pCpu, TRUE);

\t/* FALSE preserves APURAM; registers/counters were reset above. */
\tSNSPCReset(pCpu, FALSE);
}


'''
    s = s.replace(anchor, helper + anchor, 1)
    modified[SNSPC_C] = s
    print("[ OK ] SPC700: soft reset completo adicionado")
else:
    print("[SKIP] SPC700: soft reset já aplicado")

s = modified[SNES_CPP]
soft_start = s.find("void SnesSystem::SoftReset()")
soft_end = s.find("void SnesSystem::SetRom(", soft_start)
if soft_start < 0 or soft_end < 0:
    die("snes.cpp: não consegui isolar SnesSystem::SoftReset")

soft = s[soft_start:soft_end]
if "SNSPCSoftReset(&m_Spc);" not in soft:
    if "SNSPCResetCounters(&m_Spc);" not in soft:
        die("snes.cpp: SNSPCResetCounters não encontrado no SoftReset atual")
    soft = soft.replace("SNSPCResetCounters(&m_Spc);\n", "", 1)

    if "SNSPCReset(&m_Spc, false);" in soft:
        soft = soft.replace(
            "SNSPCReset(&m_Spc, false);",
            "/* Restart SPC from IPL ROM while preserving APURAM. */\n"
            "SNSPCSoftReset(&m_Spc);",
            1,
        )
    elif "SNSPCReset(&m_Spc, FALSE);" in soft:
        soft = soft.replace(
            "SNSPCReset(&m_Spc, FALSE);",
            "/* Restart SPC from IPL ROM while preserving APURAM. */\n"
            "SNSPCSoftReset(&m_Spc);",
            1,
        )
    else:
        die("snes.cpp: SNSPCReset(... false) não encontrado no SoftReset")

    s = s[:soft_start] + soft + s[soft_end:]
    modified[SNES_CPP] = s
    print("[ OK ] SNES: in-game soft reset atualizado")
else:
    print("[SKIP] SNES: novo soft reset já está em uso")

# ---------------------------------------------------------------------
# QuickNES submodule: public HTTPS clone + branch metadata.
# ---------------------------------------------------------------------
s = modified[GITMODULES]
s = s.replace(
    "url = git@github.com:itsveenee/QuickNES_Core.git",
    "url = " + QUICKNES_URL,
)

match = re.search(
    r'(\[submodule "src/third_party/quicknes"\]\n(?:\t.*\n)*)',
    s,
)
if not match:
    die(".gitmodules: seção QuickNES não encontrada")
section = match.group(1)
if "branch = master" not in section:
    url_line = "\turl = " + QUICKNES_URL + "\n"
    if url_line not in section:
        die(".gitmodules: URL HTTPS do QuickNES não encontrada")
    new_section = section.replace(url_line, url_line + "\tbranch = master\n", 1)
    s = s[:match.start(1)] + new_section + s[match.end(1):]
modified[GITMODULES] = s
print("[ OK ] QuickNES: submodule HTTPS + branch master")

# ---------------------------------------------------------------------
# README.
# ---------------------------------------------------------------------
s = modified[README]
if QUICKNES_MARKER not in s:
    credit_anchor = (
        "**Huge thanks to ReyFxck** for his work on SNESticle Revive "
        "and for providing the foundation from which Aurora was created. "
        "**Huge thanks to Icer Addis** for creating the original "
        "SNESticle and its codebase.\n"
    )
    if s.count(credit_anchor) != 1:
        die("README: parágrafo de créditos principal não encontrado")
    s = s.replace(
        credit_anchor,
        credit_anchor
        + "\n<!-- AURORA_QUICKNES_CREDIT -->\n"
        + "**QuickNES credit:** NES emulation through QuickNES is based on "
        + "the **QuickNES core originally by Shay Green**, with the libretro "
        + "core maintained by **libretro contributors**. Aurora uses "
        + "`itsveenee/QuickNES_Core` as a pinned Git submodule for its PS2 "
        + "integration. See [THIRD_PARTY.md](THIRD_PARTY.md) and the license "
        + "notices inside the QuickNES submodule.\n",
        1,
    )

if "## Building from Git" not in s:
    sep = "\n---\n"
    if sep not in s:
        die("README: separador inicial '---' não encontrado")
    block = '''

## Building from Git

Clone Aurora together with its pinned third-party cores:

```bash
git clone --recurse-submodules https://github.com/itsveenee/SNESticleAurora.git
cd SNESticleAurora
git submodule update --init --recursive
make
```

If the repository was cloned without `--recurse-submodules`, run
`git submodule update --init --recursive` afterwards.
'''
    s = s.replace(sep, block + sep, 1)

s = re.sub(
    r'^\* Super Mario Bros\. 2 '
    r'\(SNES, inside Super Mario All Stars\) '
    r'court?ain transition effect not working\s*\n',
    '',
    s,
    flags=re.MULTILINE,
)

if "Improved SNES offset-per-tile rendering" not in s:
    anchor = "* QuickNES for NES games\n"
    if s.count(anchor) != 1:
        die("README: feature QuickNES não encontrada")
    s = s.replace(
        anchor,
        anchor
        + "* Improved SNES offset-per-tile rendering "
        + "(including the Super Mario Bros. 2 curtain transition "
        + "in Super Mario All-Stars)\n",
        1,
    )

s = s.replace(
    "* Top Gear (SNES) is very slow",
    "* Top Gear (SNES) performance still needs PS2 hardware retesting "
    "after the CHR H-flip cache optimization",
)
modified[README] = s
print("[ OK ] README atualizado")

# ---------------------------------------------------------------------
# CREDITS.md.
# ---------------------------------------------------------------------
s = modified[CREDITS]
if "## QuickNES" not in s:
    anchor = "## Third-party components"
    if anchor not in s:
        die("CREDITS.md: 'Third-party components' não encontrado")
    block = '''## QuickNES

The NES QuickNES integration in Aurora uses a pinned fork of the
QuickNES/libretro codebase:

- **Original QuickNES / Nes_Emu:** Shay Green
- **Libretro QuickNES core:** libretro contributors
- **Aurora PS2 integration fork:** `itsveenee/QuickNES_Core`
- **Aurora integration and PS2-specific glue:** Vinícius Nunes (`@itsveenee`)

The QuickNES submodule retains its own license and source-file notices.
Those notices remain authoritative and must be preserved.

'''
    s = s.replace(anchor, block + anchor, 1)
modified[CREDITS] = s
print("[ OK ] CREDITS.md atualizado")

# ---------------------------------------------------------------------
# src/nes/README.md.
# ---------------------------------------------------------------------
s = modified[NES_README]
if "## QuickNES integration" not in s:
    anchor = "## Upstream"
    if anchor not in s:
        die("src/nes/README.md: 'InfoNES upstream' não encontrado")
    block = '''## QuickNES integration

Aurora also integrates **QuickNES**, originally by **Shay Green**, through
the libretro QuickNES codebase.

The Aurora integration is pinned at `src/third_party/quicknes`.
Initialize it with:

```bash
git submodule update --init --recursive
```

The integration fork is maintained at `itsveenee/QuickNES_Core`.
See the submodule's own license and source-file notices.

'''
    s = s.replace(anchor, block + anchor, 1)
modified[NES_README] = s
print("[ OK ] src/nes/README.md atualizado")

# ---------------------------------------------------------------------
# Boot credits.
# ---------------------------------------------------------------------
s = modified[INTRO]
needle = 'ScrPrintf("QuickNES core by Shay Green / libretro");'
if needle not in s:
    candidates = [
        '    ScrPrintf("Thanks to Icer Addis for the original");',
        '\tScrPrintf("Thanks to Icer Addis for the original");',
    ]
    found = next((c for c in candidates if c in s), None)
    if found is None:
        die("mainloop_init.cpp: crédito de Icer Addis não encontrado")
    indent = "\t" if found.startswith("\t") else "    "
    s = s.replace(found, found + "\n" + indent + needle, 1)
modified[INTRO] = s
print("[ OK ] Boot credits atualizados")

# ---------------------------------------------------------------------
# THIRD_PARTY.md.
# ---------------------------------------------------------------------
quick_section = '''## QuickNES

Aurora integrates QuickNES as the pinned Git submodule:

`src/third_party/quicknes`

Attribution:

- Original QuickNES / Nes_Emu: **Shay Green**
- Libretro QuickNES core: **libretro contributors**
- Aurora PS2 integration fork: **itsveenee/QuickNES_Core**

The QuickNES source tree contains its own license and source-file notices.
These files must be preserved when redistributing the QuickNES code.
'''

if third_party_original is None:
    third_party_new = (
        "# Third-party components\n\n"
        "SNESticle Aurora contains or integrates code from third-party "
        "projects. Their original license files and source-file notices "
        "remain authoritative.\n\n"
        + quick_section
        + "\n## Other components\n\n"
        + "Aurora also contains or integrates third-party components inherited "
        + "from SNESticle/SNESticle Revive, including InfoNES, FCEUmm, miniz, "
        + "libxmp-lite and PS2SDK-related libraries. Refer to each component's "
        + "bundled license and source-file notices.\n"
    )
else:
    third_party_new = third_party_original
    if "## QuickNES" not in third_party_new:
        third_party_new = third_party_new.rstrip() + "\n\n" + quick_section + "\n"

# ---------------------------------------------------------------------
# Pre-write protections.
# ---------------------------------------------------------------------
checks = [
    (modified[MAKEFILE], "SNES_CHR_HFLIP_CACHE ?= 1", "Top Gear Makefile switch"),
    (modified[MAKEFILE], flag, "Top Gear compiler define"),
    (modified[CHR_CACHE], TOPGEAR_MARKER, "Top Gear cache marker"),
    (modified[SNSPC_H], "void SNSPCSoftReset(SNSpcT *pCpu);", "SPC declaration"),
    (modified[SNSPC_C], RESET_MARKER, "SPC implementation"),
    (modified[SNES_CPP], "SNSPCSoftReset(&m_Spc);", "SNES reset call"),
    (modified[GITMODULES], QUICKNES_URL, "QuickNES HTTPS"),
    (modified[README], QUICKNES_MARKER, "README QuickNES marker"),
    (modified[INTRO], needle, "QuickNES boot credit"),
]
for text, wanted, label in checks:
    if wanted not in text:
        die(f"sanity check falhou: {label}")

if "$(OBJ_DIR)/SNESticle.elf" not in modified[MAKEFILE]:
    die("proteção ELF: build/SNESticle.elf deixou de existir no Makefile")

for text in list(modified.values()) + [third_party_new]:
    for bad in ("SNESticle_Aurora/", "SNESticle Aurora/", "SNESticleAurora/"):
        if bad in text:
            die(f"proteção de compatibilidade: caminho proibido {bad}")

# ---------------------------------------------------------------------
# Backup and write.
# ---------------------------------------------------------------------
stamp = time.strftime("%Y%m%d-%H%M%S")
backup_dir = ROOT / ".git" / f"aurora-batch-backup-{stamp}"
backup_dir.mkdir(parents=True, exist_ok=True)

changed_existing = [p for p in files if modified[p] != original[p]]
for p in changed_existing:
    rel = p.relative_to(ROOT)
    dest = backup_dir / rel
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(p, dest)

for p in changed_existing:
    p.write_text(modified[p], encoding="utf-8")
THIRD_PARTY.write_text(third_party_new, encoding="utf-8")

print("\n[WRITE] alterações gravadas")

# ---------------------------------------------------------------------
# Validate. Revert automatically if git diff --check fails.
# ---------------------------------------------------------------------
r = run(["git", "diff", "--check"])
if r is None or r.returncode != 0:
    print("\ngit diff --check falhou:")
    print(r.stdout if r else "timeout")
    print("Restaurando backup...")

    for p in changed_existing:
        src = backup_dir / p.relative_to(ROOT)
        if src.exists():
            shutil.copy2(src, p)

    if third_party_original is None:
        if THIRD_PARTY.exists():
            THIRD_PARTY.unlink()
    else:
        THIRD_PARTY.write_text(third_party_original, encoding="utf-8")

    die("alterações revertidas porque git diff --check falhou")

print("[PASS] git diff --check")

# ---------------------------------------------------------------------
# Sync QuickNES URL locally and audit the gitlink.
# ---------------------------------------------------------------------
r = run(["git", "submodule", "sync", "--", QUICKNES_PATH])
if r is not None and r.returncode == 0:
    print("[ OK ] QuickNES: configuração local sincronizada")
else:
    print("[WARN] git submodule sync não concluiu; não é fatal")

print("\n====================================================")
print(" QUICKNES SUBMODULE AUDIT")
print("====================================================")

gitlink_sha = None
r = run(["git", "ls-files", "-s", "--", QUICKNES_PATH])
if r and r.returncode == 0 and r.stdout.strip():
    fields = r.stdout.strip().split()
    if len(fields) >= 2:
        gitlink_sha = fields[1]

if gitlink_sha:
    print("gitlink Aurora :", gitlink_sha)
else:
    print("[WARN] não consegui ler o gitlink QuickNES")

quick_dir = ROOT / QUICKNES_PATH
local_head = None
if quick_dir.exists():
    r = run(["git", "-C", str(quick_dir), "rev-parse", "HEAD"])
    if r and r.returncode == 0:
        local_head = r.stdout.strip()
        print("checkout local :", local_head)
        dirty = run(["git", "-C", str(quick_dir), "status", "--porcelain"])
        if dirty and dirty.stdout.strip():
            print("[WARN] QuickNES possui mudanças locais")
        if gitlink_sha and local_head != gitlink_sha:
            print("[WARN] checkout QuickNES != gitlink Aurora")
            print("       git submodule update --init --recursive src/third_party/quicknes")
else:
    print("[INFO] QuickNES não está inicializado localmente")

r = run(["git", "ls-remote", QUICKNES_URL, "refs/heads/master"], timeout=15)
if r and r.returncode == 0 and r.stdout.strip():
    remote_master = r.stdout.split()[0]
    print("master remoto  :", remote_master)
    if gitlink_sha and remote_master != gitlink_sha:
        print("\n[ACTION] master público QuickNES != gitlink Aurora")
        if local_head == gitlink_sha:
            print("  git -C src/third_party/quicknes push origin HEAD:master")
        else:
            print("  git submodule update --init --recursive src/third_party/quicknes")
            print("  git -C src/third_party/quicknes push origin HEAD:master")
    elif gitlink_sha:
        print("[PASS] QuickNES master remoto == gitlink Aurora")
else:
    print("[INFO] não foi possível consultar o master remoto agora")

print("\n====================================================")
print(" AURORA BATCH CONCLUÍDO")
print("====================================================")
print("Top Gear : H-flip CHR cache aplicado (A/B: SNES_CHR_HFLIP_CACHE=0)")
print("Reset    : SPC700 reinicia via IPL ROM preservando APURAM")
print("QuickNES : HTTPS + documentação/créditos")
print("SMB2     : código funcional não foi alterado")
print("ELF      : SNESticle.elf preservado")
print("Dados    : diretórios SNESticle preservados")
print("Backup   :", backup_dir.relative_to(ROOT))
print("\nPróximos comandos:")
print("  git diff --check")
print("  git diff --stat")
print("  git diff")
print("  ./copy.sh")
