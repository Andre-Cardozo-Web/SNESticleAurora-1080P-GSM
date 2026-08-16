#!/usr/bin/env python3
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path.cwd()
BASE_QUICKNES_ONLY = "6bf301046d3074d9b9ea5f4f1dedbbbfed41dba6"

MAKEFILE = ROOT / "Makefile"
GITMODULES = ROOT / ".gitmodules"
NESSYSTEM = ROOT / "src/nes/quicknes/nessystem_quicknes.cpp"
FCE_BRIDGE_DIR = ROOT / "src/nes/fceumm"
FCE_SUBMODULE = ROOT / "src/third_party/fceumm"
THIRD_PARTY = ROOT / "THIRD_PARTY.md"
OLD_PATCH = ROOT / "aurora_nes_all_mappers_fceumm_fallback.py"

def die(msg):
    print(f"\nERRO: {msg}")
    raise SystemExit(1)

def run(args, cwd=ROOT, check=False):
    r = subprocess.run(
        args,
        cwd=str(cwd),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if check and r.returncode:
        die(f"{' '.join(args)} falhou:\n{r.stdout}")
    return r

def read(p):
    if not p.exists():
        die(f"arquivo obrigatório ausente: {p.relative_to(ROOT)}")
    return p.read_text(encoding="utf-8")

print("============================================================")
print(" SNESticle Aurora - REMOVE FCEUmm / KEEP QuickNES")
print("============================================================")
print()

if not (ROOT / ".git").exists():
    die("rode este script na raiz do repositório SNESticle Aurora")

for p in (MAKEFILE, GITMODULES, NESSYSTEM):
    if not p.exists():
        die(f"arquivo obrigatório ausente: {p.relative_to(ROOT)}")

# ----------------------------------------------------------------------
# 1) Obtain the last known QuickNES-only NesSystem that already contains
#    the fixed QNST save/load implementation.
# ----------------------------------------------------------------------

r = run([
    "git", "show",
    f"{BASE_QUICKNES_ONLY}:src/nes/quicknes/nessystem_quicknes.cpp"
])

if r.returncode != 0:
    die(
        "não consegui recuperar o NesSystem QuickNES-only do commit "
        f"{BASE_QUICKNES_ONLY[:7]}.\n{r.stdout}"
    )

quicknes_only = r.stdout

if (
    "QUICKNES_STATE_MAGIC" not in quicknes_only
    or "QuicknesBridge_SaveState" not in quicknes_only
    or "QuicknesBridge_LoadState" not in quicknes_only
    or "Fceumm" in quicknes_only
    or "FCEUmm" in quicknes_only
):
    die("sanity falhou no NesSystem QuickNES-only recuperado")

print("[PASS] base QuickNES-only recuperada (save/load QNST preservado)")

# ----------------------------------------------------------------------
# 2) Prepare text edits in memory.
# ----------------------------------------------------------------------

make = read(MAKEFILE)

# Remove FCEUmm variable block.
make, n = re.subn(
    r"\n?# FCEUMM_SNESTICLE_BEGIN\n.*?# FCEUMM_SNESTICLE_END\n?",
    "\n",
    make,
    count=1,
    flags=re.S,
)
if n:
    print("[ OK ] Makefile: bloco FCEUMM_SNESTICLE removido")
else:
    print("[PASS] Makefile: bloco FCEUMM_SNESTICLE já ausente")

# Remove bridge source from SRCS.
make, n_src = re.subn(
    r"^[ \t]*src/nes/fceumm/fceumm_bridge\.cpp[ \t]*\\?[ \t]*\n",
    "",
    make,
    count=1,
    flags=re.M,
)
if n_src:
    print("[ OK ] Makefile: fceumm_bridge.cpp removido de SRCS")

# Remove FCEUmm build rule, stopping immediately before $(TARGET).
make, n_rule = re.subn(
    r"\n?# AURORA_FCEUMM_BUILD_RULE_V1\n.*?(?=^\$\(TARGET\):)",
    "\n",
    make,
    count=1,
    flags=re.S | re.M,
)
if n_rule:
    print("[ OK ] Makefile: regra de build FCEUmm removida")
else:
    print("[PASS] Makefile: regra de build FCEUmm já ausente")

# Remove FCE library from target prerequisites/link line, tolerating quotes.
make = make.replace(' "$(FCEUMM_LIB)"', "")
make = make.replace(" $(FCEUMM_LIB)", "")
make = make.replace("$(FCEUMM_LIB) ", "")
make = make.replace("$(FCEUMM_LIB)", "")

# Protect the QuickNES build/link.
if "$(QUICKNES_LIB)" not in make:
    die("proteção: QuickNES sumiu do Makefile")
if "src/nes/quicknes/quicknes_bridge.cpp" not in make:
    die("proteção: quicknes_bridge.cpp sumiu do Makefile")
if "src/nes/quicknes/nessystem_quicknes.cpp" not in make:
    die("proteção: nessystem_quicknes.cpp sumiu do Makefile")

# Remove FCEUmm .gitmodules stanza, preserve QuickNES.
gm = read(GITMODULES)
gm, n_gm = re.subn(
    r'\[submodule "src/third_party/fceumm"\]\n'
    r'(?:[ \t]+[^\n]*\n)*',
    "",
    gm,
    count=1,
)
gm = re.sub(r"\n{3,}", "\n\n", gm).lstrip("\n")

if '[submodule "src/third_party/quicknes"]' not in gm:
    die("proteção: entrada QuickNES sumiu de .gitmodules")

if n_gm:
    print("[ OK ] .gitmodules: FCEUmm removido")
else:
    print("[PASS] .gitmodules: FCEUmm já ausente")

# THIRD_PARTY: remove only the FCEUmm name, preserving the document.
third = None
if THIRD_PARTY.exists():
    third = read(THIRD_PARTY)
    third = third.replace(", FCEUmm", "")
    third = third.replace("FCEUmm, ", "")
    third = re.sub(r"\bFCEUmm\b", "", third)
    third = re.sub(r" {2,}", " ", third)
    print("[ OK ] THIRD_PARTY.md: referência FCEUmm removida")

# ----------------------------------------------------------------------
# 3) Backup before destructive directory removal.
# ----------------------------------------------------------------------

stamp = time.strftime("%Y%m%d-%H%M%S")
backup = ROOT / ".git" / f"remove-fceumm-backup-{stamp}"
backup.mkdir(parents=True, exist_ok=True)

for p in (MAKEFILE, GITMODULES, NESSYSTEM, THIRD_PARTY):
    if p.exists():
        dst = backup / p.relative_to(ROOT)
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, dst)

# Save the submodule's current SHA/status/diff even though it is being removed.
if FCE_SUBMODULE.exists():
    info = []
    for cmd in (
        ["git", "-C", str(FCE_SUBMODULE), "rev-parse", "HEAD"],
        ["git", "-C", str(FCE_SUBMODULE), "status", "--short"],
        ["git", "-C", str(FCE_SUBMODULE), "diff"],
    ):
        rr = run(cmd)
        info.append("$ " + " ".join(cmd) + "\n" + rr.stdout + "\n")
    (backup / "fceumm-submodule-state.txt").write_text(
        "\n".join(info), encoding="utf-8"
    )

    # Preserve source/untracked files, but skip disposable build products.
    dst = backup / "src/third_party/fceumm"
    shutil.copytree(
        FCE_SUBMODULE,
        dst,
        dirs_exist_ok=True,
        ignore=shutil.ignore_patterns(
            "*.o", "*.a", "*.so", "*.d", "*.dylib",
            ".git", "obj", "objs"
        ),
    )

if FCE_BRIDGE_DIR.exists():
    dst = backup / "src/nes/fceumm"
    shutil.copytree(FCE_BRIDGE_DIR, dst, dirs_exist_ok=True)

print(f"[ OK ] backup: {backup.relative_to(ROOT)}")

# ----------------------------------------------------------------------
# 4) Apply the QuickNES-only frontend and Makefile/.gitmodules edits.
# ----------------------------------------------------------------------

MAKEFILE.write_text(make, encoding="utf-8")
GITMODULES.write_text(gm, encoding="utf-8")
NESSYSTEM.write_text(quicknes_only, encoding="utf-8")

if third is not None:
    THIRD_PARTY.write_text(third, encoding="utf-8")

print("[ OK ] NesSystem restaurado para QuickNES-only")
print("[PASS] QNST save/load do commit de correção preservado")

# ----------------------------------------------------------------------
# 5) Remove FCEUmm frontend + submodule working tree + local git metadata.
#    No git add/commit is performed here.
# ----------------------------------------------------------------------

if FCE_BRIDGE_DIR.exists():
    shutil.rmtree(FCE_BRIDGE_DIR)
    print("[ OK ] src/nes/fceumm removido")

if FCE_SUBMODULE.exists():
    shutil.rmtree(FCE_SUBMODULE)
    print("[ OK ] src/third_party/fceumm removido")

# Remove local submodule config and object/worktree metadata.
run([
    "git", "config", "--remove-section",
    "submodule.src/third_party/fceumm"
])

modules_meta = ROOT / ".git/modules/src/third_party/fceumm"
if modules_meta.exists():
    shutil.rmtree(modules_meta)
    print("[ OK ] .git/modules/.../fceumm removido")

if OLD_PATCH.exists():
    OLD_PATCH.unlink()
    print("[ OK ] script antigo de fallback FCEUmm removido")

# Remove empty parents if applicable, but never remove src/nes itself.
for p in (ROOT / "src/nes/fceumm", ROOT / ".git/modules/src/third_party"):
    try:
        p.rmdir()
    except (FileNotFoundError, OSError):
        pass

# ----------------------------------------------------------------------
# 6) Final sanity.
# ----------------------------------------------------------------------

if "FCEUMM" in MAKEFILE.read_text(encoding="utf-8").upper():
    die("sanity: ainda há FCEUMM no Makefile")

if "FCEUMM" in NESSYSTEM.read_text(encoding="utf-8").upper():
    die("sanity: ainda há FCEUmm em nessystem_quicknes.cpp")

if "FCEUMM" in GITMODULES.read_text(encoding="utf-8").upper():
    die("sanity: ainda há FCEUmm em .gitmodules")

if FCE_BRIDGE_DIR.exists() or FCE_SUBMODULE.exists():
    die("sanity: diretório FCEUmm ainda existe")

# Ensure the fixes we explicitly want to preserve are still present.
qn = NESSYSTEM.read_text(encoding="utf-8")
for needle in (
    "QUICKNES_STATE_MAGIC",
    "QUICKNES_STATE_CAPACITY",
    "QuicknesBridge_SaveState",
    "QuicknesBridge_LoadState",
    "QuicknesBridge_SoftReset",
):
    if needle not in qn:
        die(f"sanity QuickNES falhou: {needle}")

diffcheck = run(["git", "diff", "--check"])
if diffcheck.returncode:
    die("git diff --check falhou:\n" + diffcheck.stdout)

# Search tracked/working files for lingering FCEUmm references.
grep = run(["git", "grep", "-ni", "-e", "fceumm", "--", "."])
remaining = grep.stdout.strip()

print()
print("============================================================")
print(" RESULTADO")
print("============================================================")
print()
print("FCEUmm removido da integração.")
print("QuickNES continua sendo o único backend NES.")
print()
print("Preservado:")
print("  - QuickNES e seu submódulo")
print("  - mappers implementados dentro do QuickNES")
print("  - save/load QNST corrigido")
print("  - nesrom.cpp / parser NES 2.0 / Galaxian")
print("  - SNES, áudio e vídeo não foram alterados")
print()
if remaining:
    print("ATENÇÃO: ainda existem referências textuais a 'fceumm':")
    print(remaining)
    print()
else:
    print("[PASS] nenhuma referência FCEUmm restante em arquivos rastreáveis")
print("[PASS] git diff --check")
print()
print("Agora:")
print("  ./copy.sh")
print()
print("Depois confira:")
print("  git status --short")
print("  git diff --check")
print()
print("Backup disponível em:")
print(" ", backup.relative_to(ROOT))
print()
print("O script NÃO executou git add, commit nem push.")
