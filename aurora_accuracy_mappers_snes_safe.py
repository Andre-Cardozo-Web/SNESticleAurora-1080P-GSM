#!/usr/bin/env python3
"""
SNESticle Aurora accuracy/speed mapper batch.

Goals:
- QuickNES: NES 2.0 mapper/submapper parsing with zero per-frame overhead.
- QuickNES: align already-present mapper headers with Nes_Mapper::create().
- QuickNES: improve Namco 108 family behavior, especially mapper 206.
- QuickNES: add low-risk mapper 76 and 95 implementations from documented
  Namco 108 behavior, cross-checked against MesenCE.
- QuickNES: emulate controller reads correctly while $4016 strobe is high.
- SNES: fix the forced-blank accessor.
- SNES: avoid recomputing the same 4bpp H-flip on the first OBJ cache miss.

The script intentionally does NOT touch:
- SNES geometry / PolyRect / PCRTC positioning
- audio gain
- SMB2 offset-per-tile code (snppubg.cpp)
- the final ELF filename
- SNESticle runtime data directory names
"""

from pathlib import Path
import subprocess
import shutil
import re
import sys
import time

ROOT = Path(".").resolve()
QN = ROOT / "src/third_party/quicknes"
QN_EMU = QN / "nes_emu"
QN_MAPPERS = QN_EMU / "mappers"

CART_H = QN_EMU / "Nes_Cart.h"
CART_CPP = QN_EMU / "Nes_Cart.cpp"
CORE_CPP = QN_EMU / "Nes_Core.cpp"
MAPPER_CPP = QN_EMU / "Nes_Mapper.cpp"
MAPPER_206 = QN_MAPPERS / "mapper206.hpp"
MAPPER_088 = QN_MAPPERS / "mapper088.hpp"
MAPPER_076 = QN_MAPPERS / "mapper076.hpp"
MAPPER_095 = QN_MAPPERS / "mapper095.hpp"

SNPPU_H = ROOT / "src/snes/ppu/snppu.h"
CHR_CACHE_H = ROOT / "src/snes/ppu/snppuchrcache.h"
RENDER8_CPP = ROOT / "src/snes/ppu/snppurender8.cpp"
SMB2_BG = ROOT / "src/snes/ppu/snppubg.cpp"
MAKEFILE = ROOT / "Makefile"

MARK_NES2 = "AURORA_NES2_HEADER_V1"
MARK_STROBE = "AURORA_NES_STROBE_HIGH_V1"
MARK_206 = "AURORA_NAMCO206_MESEN_ALIGN_V1"
MARK_88 = "AURORA_NAMCO88_154_MESEN_ALIGN_V1"
MARK_76 = "AURORA_MAPPER076_NAMCO108_V1"
MARK_95 = "AURORA_MAPPER095_NAMCO108_V1"
MARK_FORCEBLANK = "AURORA_SNES_FORCEBLANK_V1"
MARK_HFLIP_MISS = "AURORA_HFLIP_MISS_REUSE_V1"


def die(msg):
    print()
    print("ERRO:", msg)
    raise SystemExit(1)


def run(args, cwd=None, timeout=None):
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


def read(path):
    if not path.exists():
        die("arquivo não encontrado: " + str(path.relative_to(ROOT)))
    return path.read_text(encoding="utf-8")


def replace_once(text, old, new, label):
    n = text.count(old)
    if n != 1:
        die(f"{label}: esperava 1 ocorrência, encontrei {n}")
    return text.replace(old, new, 1)


def sub_once(pattern, repl, text, label, flags=0):
    out, n = re.subn(pattern, repl, text, count=1, flags=flags)
    if n != 1:
        die(f"{label}: padrão não encontrado de forma única")
    return out


def function_end(text, start):
    brace = text.find("{", start)
    if brace < 0:
        die("não encontrei início de função/classe")
    depth = 0
    i = brace
    in_str = False
    in_chr = False
    esc = False
    line_comment = False
    block_comment = False

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

        if in_str:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
            i += 1
            continue

        if in_chr:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == "'":
                in_chr = False
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
            in_str = True
            i += 1
            continue
        if c == "'":
            in_chr = True
            i += 1
            continue

        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1

    die("bloco C/C++ sem fechamento")


def has_trailing_ws(text):
    return any(line.endswith((" ", "\t")) for line in text.splitlines())


print("============================================================")
print(" SNESticle Aurora - NES mapper/accuracy + SNES safe pass")
print("============================================================")
print()

if not (ROOT / ".git").exists():
    die("execute este .py na raiz do SNESticle Aurora")

for p in [
    CART_H, CART_CPP, CORE_CPP, MAPPER_CPP, MAPPER_206, MAPPER_088,
    SNPPU_H, CHR_CACHE_H, RENDER8_CPP, MAKEFILE
]:
    if not p.exists():
        die("arquivo necessário ausente: " + str(p.relative_to(ROOT)))

if not QN_MAPPERS.is_dir():
    die("submodule QuickNES não inicializado")

# Preserve all current user changes by preparing everything in memory first.
paths = [
    CART_H, CART_CPP, CORE_CPP, MAPPER_CPP, MAPPER_206, MAPPER_088,
    SNPPU_H, CHR_CACHE_H, RENDER8_CPP,
]
original = {p: read(p) for p in paths}
modified = dict(original)
new_files = {}

if SMB2_BG.exists():
    print("[PASS] SNES SMB2: snppubg.cpp não será tocado")

make_text = read(MAKEFILE)
if "$(OBJ_DIR)/SNESticle.elf" not in make_text:
    die("proteção: TARGET build/SNESticle.elf não encontrado no Makefile")
print("[PASS] ELF final continua SNESticle.elf")

# ---------------------------------------------------------------------
# QuickNES Nes_Cart.h: keep raw iNES flags, but store parsed mapper ID
# and NES 2.0 submapper separately.
# ---------------------------------------------------------------------
s = modified[CART_H]

if MARK_NES2 not in s:
    decl = "\tvoid set_mapper( int mapper_lsb, int mapper_msb );\n"
    if decl not in s:
        die("Nes_Cart.h: declaração set_mapper não encontrada")
    s = s.replace(
        decl,
        decl +
        "\tvoid set_mapper_nes2( int mapper_lsb, int mapper_msb, int mapper_ext );\n",
        1
    )

    decl2 = "\tint mapper_code() const;\n"
    if decl2 not in s:
        die("Nes_Cart.h: mapper_code() não encontrado")
    s = s.replace(
        decl2,
        decl2 + "\tint submapper_code() const;\n",
        1
    )

    field = "\tunsigned mapper;\n"
    if field not in s:
        die("Nes_Cart.h: campo mapper não encontrado")
    s = s.replace(
        field,
        field +
        "\tunsigned mapper_code_value;\n"
        "\tunsigned submapper;\n",
        1
    )

    pat = (
        r'inline void Nes_Cart::set_mapper\( int mapper_lsb, int mapper_msb \)\s*'
        r'\{\s*mapper = mapper_msb \* 0x100 \+ mapper_lsb;\s*\}\s*'
        r'inline int Nes_Cart::mapper_code\(\) const\s*'
        r'\{\s*return \(\(mapper >> 8\) & 0xf0\) \| \(\(mapper >> 4\) & 0x0f\);\s*\}'
    )

    repl = r'''/* AURORA_NES2_HEADER_V1
 * Keep legacy byte-6 flags in mapper for battery/mirroring, while exposing
 * the decoded mapper ID independently. This lets NES 2.0 use mapper bits
 * 8-11 and a submapper without changing the hot emulation path.
 */
inline void Nes_Cart::set_mapper( int mapper_lsb, int mapper_msb )
{
	mapper = mapper_msb * 0x100 + mapper_lsb;
	mapper_code_value =
		(mapper_msb & 0xF0) | ((mapper_lsb >> 4) & 0x0F);
	submapper = 0;
}

inline void Nes_Cart::set_mapper_nes2(
	int mapper_lsb, int mapper_msb, int mapper_ext )
{
	mapper = mapper_msb * 0x100 + mapper_lsb;
	mapper_code_value =
		((mapper_ext & 0x0F) << 8) |
		(mapper_msb & 0xF0) |
		((mapper_lsb >> 4) & 0x0F);
	submapper = (mapper_ext >> 4) & 0x0F;
}

inline int Nes_Cart::mapper_code() const
{
	return (int) mapper_code_value;
}

inline int Nes_Cart::submapper_code() const
{
	return (int) submapper;
}'''

    s = sub_once(pat, repl, s, "Nes_Cart.h: implementação mapper/NES2", re.S)
    modified[CART_H] = s
    print("[ OK ] NES: estrutura de mapper/submapper NES 2.0 preparada")
else:
    print("[SKIP] NES 2.0 já marcado em Nes_Cart.h")


# ---------------------------------------------------------------------
# QuickNES Nes_Cart.cpp: Mesen-style iNES/NES2 header classification.
# This runs only while loading a ROM.
# ---------------------------------------------------------------------
s = modified[CART_CPP]

if MARK_NES2 not in s:
    old_clear = """\
\tprg_size_ = 0;
\tchr_size_ = 0;
\tmapper = 0;
"""
    if old_clear not in s:
        die("Nes_Cart.cpp: clear() esperado não encontrado")
    s = s.replace(
        old_clear,
        """\
\tprg_size_ = 0;
\tchr_size_ = 0;
\tmapper = 0;
\tmapper_code_value = 0;
\tsubmapper = 0;
""",
        1
    )

    start = s.find("const char * Nes_Cart::load_ines(")
    if start < 0:
        die("Nes_Cart.cpp: load_ines() não encontrado")
    end = function_end(s, start)

    new_loader = r'''/* AURORA_NES2_HEADER_V1
 *
 * Header parsing follows the NES 2.0/iNES distinction used by modern NES
 * emulators: byte 7 bits 2-3 == 2 identifies NES 2.0; == 0 is iNES; the
 * remaining patterns are treated as archaic iNES, whose upper mapper nibble
 * is unreliable.
 */
static long AuroraNes2RomSize(
	uint8_t lsb, uint8_t msb_nibble, long linear_unit )
{
	if ( msb_nibble != 0x0F )
	{
		unsigned long count =
			((unsigned long) msb_nibble << 8) | (unsigned long) lsb;
		unsigned long long bytes =
			(unsigned long long) count *
			(unsigned long long) linear_unit;

		if ( bytes > 0x7FFFFFFFULL )
			return -1;
		return (long) bytes;
	}

	/* NES 2.0 exponent/multiplier notation:
	 * size = 2^E * (2*M + 1) bytes. */
	unsigned exponent = lsb >> 2;
	unsigned multiplier = ((unsigned) lsb & 3u) * 2u + 1u;

	if ( exponent >= 31 )
		return -1;

	unsigned long long bytes =
		((unsigned long long) 1 << exponent) * multiplier;

	if ( bytes > 0x7FFFFFFFULL )
		return -1;
	return (long) bytes;
}

const char * Nes_Cart::load_ines( Auto_File_Reader in )
{
	RETURN_ERR( in.open() );

	ines_header_t h;
	RETURN_ERR( in->read( &h, sizeof h ) );

	if ( 0 != memcmp( h.signature, "NES\x1A", 4 ) )
		return not_ines_file;

	const bool nes2 = (h.flags2 & 0x0C) == 0x08;
	const bool ines = (h.flags2 & 0x0C) == 0x00;

	long prg_bytes;
	long chr_bytes;

	if ( nes2 )
	{
		set_mapper_nes2( h.flags, h.flags2, h.zero [0] );

		const uint8_t size_msb = h.zero [1];
		prg_bytes = AuroraNes2RomSize(
			h.prg_count, size_msb & 0x0F, 16 * 1024L );
		chr_bytes = AuroraNes2RomSize(
			h.chr_count, (size_msb >> 4) & 0x0F, 8 * 1024L );
	}
	else
	{
		/* Archaic headers can contain garbage in byte 7. Match the
		 * conservative old-iNES rule and take the mapper from byte 6 only. */
		set_mapper( h.flags, ines ? h.flags2 : 0 );

		/* In iNES 1.0, a PRG count of zero is the historical encoding
		 * for 256 x 16 KiB banks. */
		long prg_banks = h.prg_count ? (long) h.prg_count : 256L;
		prg_bytes = prg_banks * 16 * 1024L;
		chr_bytes = (long) h.chr_count * 8 * 1024L;
	}

	if ( prg_bytes < 0 || chr_bytes < 0 )
		return "Unsupported NES 2.0 ROM size";

	if ( h.flags & 0x04 )
		RETURN_ERR( in->skip( 512 ) );

	RETURN_ERR( resize_prg( prg_bytes ) );
	RETURN_ERR( resize_chr( chr_bytes ) );

	RETURN_ERR( in->read( prg(), prg_size() ) );
	RETURN_ERR( in->read( chr(), chr_size() ) );

	return 0;
}'''

    s = s[:start] + new_loader + s[end:]
    modified[CART_CPP] = s
    print("[ OK ] NES: parser iNES/NES 2.0 atualizado")
else:
    print("[SKIP] NES 2.0 já marcado em Nes_Cart.cpp")


# ---------------------------------------------------------------------
# Controller: transparent latch while strobe is high.
# Same branch that already existed; no new per-frame loop.
# ---------------------------------------------------------------------
s = modified[CORE_CPP]

if MARK_STROBE not in s:
    old = r'''	if ( (addr & 0xFFFE) == 0x4016 )
	{
		// to do: to aid with recording, doesn't emulate transparent latch,
		// so a game that held strobe at 1 and read $4016 or $4017 would not get
		// the current A status as occurs on a NES
		unsigned long result = joypad.joypad_latches [addr & 1];
		if ( !(joypad.w4016 & 1) )
			joypad.joypad_latches [addr & 1] = (result >> 1) | 0x80000000;
		return result & 1;
	}
'''

    new = r'''	/* AURORA_NES_STROBE_HIGH_V1
	 * With $4016 strobe held high, the NES controller port exposes the
	 * current A button continuously instead of shifting the saved latch.
	 */
	if ( (addr & 0xFFFE) == 0x4016 )
	{
		if ( joypad.w4016 & 1 )
			return current_joypad [addr & 1] & 1;

		unsigned long result = joypad.joypad_latches [addr & 1];
		joypad.joypad_latches [addr & 1] =
			(result >> 1) | 0x80000000;
		return result & 1;
	}
'''

    if old not in s:
        die("Nes_Core.cpp: bloco antigo do controller strobe não encontrado")
    s = s.replace(old, new, 1)
    modified[CORE_CPP] = s
    print("[ OK ] NES: leitura de controle com strobe alto corrigida")
else:
    print("[SKIP] controller strobe já corrigido")


# ---------------------------------------------------------------------
# Mapper 206: Namco 108 + WRAM + NES2 submapper 1 (unbanked 32 KiB PRG).
# ---------------------------------------------------------------------
s = modified[MAPPER_206]

if MARK_206 not in s:
    pos = s.find("class Mapper206")
    if pos < 0:
        die("mapper206.hpp: class Mapper206 não encontrada")

    new_class = r'''/* AURORA_NAMCO206_MESEN_ALIGN_V1
 * Namco 108 behavior cross-checked against MesenCE.
 * Mapper 206 has hardwired mirroring, WRAM at $6000-$7FFF and no IRQ.
 * NES 2.0 submapper 1 denotes the Namcot boards with unbanked 32 KiB PRG.
 */
class Mapper206 : public Nes_Mapper, namco_34xx_state_t {
public:
	Mapper206()
	{
		namco_34xx_state_t *state = this;
		register_state( state, sizeof *state );
	}

	virtual void reset_state()
	{
		enable_sram();
	}

	virtual void apply_mapping()
	{
		set_chr_bank( 0x0000, bank_2k, bank [ 0 ] );
		set_chr_bank( 0x0800, bank_2k, bank [ 1 ] );
		for ( int i = 0; i < 4; i++ )
			set_chr_bank( 0x1000 + ( i << 10 ), bank_1k, bank [ i + 2 ] );

		if ( cart().submapper_code() == 1 )
		{
			set_prg_bank( 0x8000, bank_32k, 0 );
		}
		else
		{
			set_prg_bank( 0x8000, bank_8k, bank [ 6 ] );
			set_prg_bank( 0xA000, bank_8k, bank [ 7 ] );
			set_prg_bank( 0xC000, bank_8k, ~1 );
			set_prg_bank( 0xE000, bank_8k, ~0 );
		}
	}

	virtual void write( nes_time_t, nes_addr_t addr, int data )
	{
		switch ( addr & 0xE001 )
		{
		case 0x8000:
			mode = data & 0x07;
			break;

		case 0x8001:
			switch ( mode )
			{
			case 0: case 1:
				bank [ mode ] = data >> 1;
				set_chr_bank(
					0x0000 + ( mode << 11 ),
					bank_2k, bank [ mode ] );
				break;

			case 2: case 3: case 4: case 5:
				bank [ mode ] = data;
				set_chr_bank(
					0x1000 + ( ( mode - 2 ) << 10 ),
					bank_1k, bank [ mode ] );
				break;

			case 6: case 7:
				bank [ mode ] = data;
				if ( cart().submapper_code() != 1 )
				{
					set_prg_bank(
						0x8000 + ( ( mode - 6 ) << 13 ),
						bank_8k, bank [ mode ] );
				}
				break;
			}
			break;
		}
	}
};
'''

    # mapper206.hpp has only this class after the state declaration.
    s = s[:pos] + new_class
    modified[MAPPER_206] = s
    print("[ OK ] NES mapper 206: Namco 108/WRAM/submapper 1 alinhados")
else:
    print("[SKIP] mapper 206 já alinhado")


# ---------------------------------------------------------------------
# Mapper 88/154: same Namco family. WRAM and mapper154 mirroring from
# every mapper write (bit 6), matching the board behavior.
# ---------------------------------------------------------------------
s = modified[MAPPER_088]

if MARK_88 not in s:
    start = s.find("template < bool _is154 >")
    end = s.find("typedef Mapper_Namco_34x3<false> Mapper088;")
    if start < 0 or end < 0 or end <= start:
        die("mapper088.hpp: template Namco 34x3 não encontrado")

    new_template = r'''/* AURORA_NAMCO88_154_MESEN_ALIGN_V1
 * Namco 108 family behavior cross-checked against MesenCE.
 */
template < bool _is154 >
class Mapper_Namco_34x3 : public Nes_Mapper, namco_34x3_state_t {
public:
	Mapper_Namco_34x3()
	{
		namco_34x3_state_t *state = this;
		register_state( state, sizeof *state );
	}

	virtual void reset_state()
	{
		enable_sram();
	}

	virtual void apply_mapping()
	{
		set_chr_bank( 0x0000, bank_2k, bank [ 0 ] );
		set_chr_bank( 0x0800, bank_2k, bank [ 1 ] );
		for ( int i = 0; i < 4; i++ )
			set_chr_bank( 0x1000 + ( i << 10 ), bank_1k, bank [ i + 2 ] );

		set_prg_bank( 0x8000, bank_8k, bank [ 6 ] );
		set_prg_bank( 0xA000, bank_8k, bank [ 7 ] );
		set_prg_bank( 0xC000, bank_8k, ~1 );
		set_prg_bank( 0xE000, bank_8k, ~0 );

		if ( _is154 )
			mirror_single( mirr );
	}

	virtual void write( nes_time_t, nes_addr_t addr, int data )
	{
		/* Mapper 154 derives one-screen mirroring from bit 6 of each
		 * write seen by the board, even when the base Namco 108 ignores
		 * that address for bank selection. */
		if ( _is154 )
		{
			mirr = ( data >> 6 ) & 0x01;
			mirror_single( mirr );
		}

		switch ( addr & 0xE001 )
		{
		case 0x8000:
			mode = data & 0x07;
			break;

		case 0x8001:
			switch ( mode )
			{
			case 0: case 1:
				bank [ mode ] = data >> 1;
				set_chr_bank(
					0x0000 + ( mode << 11 ),
					bank_2k, bank [ mode ] );
				break;

			case 2: case 3: case 4: case 5:
				bank [ mode ] = data | 0x40;
				set_chr_bank(
					0x1000 + ( ( mode - 2 ) << 10 ),
					bank_1k, bank [ mode ] );
				break;

			case 6: case 7:
				bank [ mode ] = data;
				set_prg_bank(
					0x8000 + ( ( mode - 6 ) << 13 ),
					bank_8k, bank [ mode ] );
				break;
			}
			break;
		}
	}
};

'''

    s = s[:start] + new_template + s[end:]
    modified[MAPPER_088] = s
    print("[ OK ] NES mappers 88/154: WRAM/mirroring alinhados")
else:
    print("[SKIP] mappers 88/154 já alinhados")


# ---------------------------------------------------------------------
# Add mapper 76 and 95: both are simple Namco 108 derivatives.
# ---------------------------------------------------------------------
mapper76_text = r'''/*
 * AURORA_MAPPER076_NAMCO108_V1
 *
 * Mapper 76 (Namcot 3446 family) for QuickNES.
 * Hardware behavior cross-checked against MesenCE's Namco 108 family.
 * Implementation is written directly against the QuickNES mapper API.
 */

#pragma once

#include "Nes_Mapper.h"

struct mapper076_state_t
{
	uint8_t bank [ 8 ];
	uint8_t mode;
};

BOOST_STATIC_ASSERT( sizeof (mapper076_state_t) == 9 );

class Mapper076 : public Nes_Mapper, mapper076_state_t {
public:
	Mapper076()
	{
		mapper076_state_t *state = this;
		register_state( state, sizeof *state );
	}

	virtual void reset_state()
	{
		enable_sram();
	}

	virtual void apply_mapping()
	{
		for ( int i = 0; i < 4; i++ )
			set_chr_bank( i << 11, bank_2k, bank [ i + 2 ] );

		set_prg_bank( 0x8000, bank_8k, bank [ 6 ] );
		set_prg_bank( 0xA000, bank_8k, bank [ 7 ] );
		set_prg_bank( 0xC000, bank_8k, ~1 );
		set_prg_bank( 0xE000, bank_8k, ~0 );
	}

	virtual void write( nes_time_t, nes_addr_t addr, int data )
	{
		switch ( addr & 0xE001 )
		{
		case 0x8000:
			mode = data & 0x07;
			break;

		case 0x8001:
			bank [ mode ] = data;

			switch ( mode )
			{
			case 2: case 3: case 4: case 5:
				set_chr_bank(
					(mode - 2) << 11,
					bank_2k, bank [ mode ] );
				break;

			case 6: case 7:
				set_prg_bank(
					0x8000 + ( ( mode - 6 ) << 13 ),
					bank_8k, bank [ mode ] );
				break;

			default:
				break;
			}
			break;
		}
	}
};
'''

mapper95_text = r'''/*
 * AURORA_MAPPER095_NAMCO108_V1
 *
 * Mapper 95 (Namcot 3425 family) for QuickNES.
 * Hardware behavior cross-checked against MesenCE's Namco 108 family.
 * Implementation is written directly against the QuickNES mapper API.
 */

#pragma once

#include "Nes_Mapper.h"

struct mapper095_state_t
{
	uint8_t bank [ 8 ];
	uint8_t mode;
};

BOOST_STATIC_ASSERT( sizeof (mapper095_state_t) == 9 );

class Mapper095 : public Nes_Mapper, mapper095_state_t {
public:
	Mapper095()
	{
		mapper095_state_t *state = this;
		register_state( state, sizeof *state );
	}

	virtual void reset_state()
	{
		enable_sram();
	}

	void update_nametables()
	{
		int nt0 = (bank [ 0 ] >> 5) & 1;
		int nt1 = (bank [ 1 ] >> 5) & 1;
		mirror_manual( nt0, nt0, nt1, nt1 );
	}

	virtual void apply_mapping()
	{
		set_chr_bank( 0x0000, bank_2k, bank [ 0 ] >> 1 );
		set_chr_bank( 0x0800, bank_2k, bank [ 1 ] >> 1 );

		for ( int i = 0; i < 4; i++ )
			set_chr_bank( 0x1000 + ( i << 10 ), bank_1k, bank [ i + 2 ] );

		set_prg_bank( 0x8000, bank_8k, bank [ 6 ] );
		set_prg_bank( 0xA000, bank_8k, bank [ 7 ] );
		set_prg_bank( 0xC000, bank_8k, ~1 );
		set_prg_bank( 0xE000, bank_8k, ~0 );

		update_nametables();
	}

	virtual void write( nes_time_t, nes_addr_t addr, int data )
	{
		switch ( addr & 0xE001 )
		{
		case 0x8000:
			mode = data & 0x07;
			break;

		case 0x8001:
			bank [ mode ] = data;

			switch ( mode )
			{
			case 0: case 1:
				set_chr_bank(
					0x0000 + ( mode << 11 ),
					bank_2k, bank [ mode ] >> 1 );
				update_nametables();
				break;

			case 2: case 3: case 4: case 5:
				set_chr_bank(
					0x1000 + ( ( mode - 2 ) << 10 ),
					bank_1k, bank [ mode ] );
				break;

			case 6: case 7:
				set_prg_bank(
					0x8000 + ( ( mode - 6 ) << 13 ),
					bank_8k, bank [ mode ] );
				break;
			}
			break;
		}
	}
};
'''

for p, text, symbol, marker in [
    (MAPPER_076, mapper76_text, "Mapper076", MARK_76),
    (MAPPER_095, mapper95_text, "Mapper095", MARK_95),
]:
    if p.exists():
        existing = p.read_text(encoding="utf-8")
        if symbol not in existing:
            die(
                f"{p.relative_to(ROOT)} já existe mas não contém {symbol}; "
                "não vou sobrescrever"
            )
        modified[p] = existing
        if marker in existing:
            print(f"[SKIP] {p.name} já é a implementação Aurora")
        else:
            print(f"[PASS] {p.name} já existe; será preservado e auditado")
    else:
        new_files[p] = text
        modified[p] = text
        print(f"[ OK ] NES: {p.name} será criado")


# ---------------------------------------------------------------------
# Mapper wiring audit.
# Any mapperNNN.hpp that contains an actual MapperNNN class/typedef/using
# but lacks include/case is wired automatically.
# ---------------------------------------------------------------------
virtual_mapper_files = {}

for p in QN_MAPPERS.glob("mapper*.hpp"):
    virtual_mapper_files[p] = modified.get(p, p.read_text(encoding="utf-8"))

for p, text in new_files.items():
    if p.parent == QN_MAPPERS:
        virtual_mapper_files[p] = text

implemented = []

for p, text in virtual_mapper_files.items():
    m = re.fullmatch(r"mapper(\d+)\.hpp", p.name)
    if not m:
        continue

    digits = m.group(1)
    symbol = "Mapper" + digits

    real_symbol = (
        re.search(r"\bclass\s+" + re.escape(symbol) + r"\b", text) or
        re.search(r"\bstruct\s+" + re.escape(symbol) + r"\b", text) or
        re.search(r"\btypedef\b[^;]*\b" + re.escape(symbol) + r"\s*;", text, re.S) or
        re.search(r"\busing\s+" + re.escape(symbol) + r"\s*=", text)
    )

    if real_symbol:
        implemented.append((int(digits), digits, symbol, p))

implemented.sort()

s = modified[MAPPER_CPP]
wired = []


def insert_mapper_include(text, n, digits):
    wanted = f'#include "mappers/mapper{digits}.hpp"'
    if wanted in text:
        return text, False

    matches = list(re.finditer(
        r'^#include "mappers/mapper(\d+)\.hpp"\s*$',
        text,
        re.M
    ))
    if not matches:
        die("Nes_Mapper.cpp: bloco de includes de mappers não encontrado")

    for m in matches:
        if int(m.group(1)) > n:
            return text[:m.start()] + wanted + "\n" + text[m.start():], True

    m = matches[-1]
    end = m.end()
    return text[:end] + "\n" + wanted + text[end:], True


def insert_mapper_case(text, n, digits, symbol):
    switch_pos = text.find("switch ( mapperCode )")
    if switch_pos < 0:
        die("Nes_Mapper.cpp: switch(mapperCode) não encontrado")

    default_pos = text.find("default: break;", switch_pos)
    if default_pos < 0:
        die("Nes_Mapper.cpp: default do mapper switch não encontrado")

    block = text[switch_pos:default_pos]
    if re.search(r"^\s*case\s+" + str(n) + r"\s*:", block, re.M):
        return text, False

    case_line = f"    case {n:3d}: mapper = new {symbol}(); break;\n"

    cases = list(re.finditer(
        r'^\s*case\s+(\d+)\s*:.*$',
        text[switch_pos:default_pos],
        re.M
    ))

    for m in cases:
        if int(m.group(1)) > n:
            pos = switch_pos + m.start()
            return text[:pos] + case_line + text[pos:], True

    # Preserve indentation of default by inserting at start of its line.
    line_start = text.rfind("\n", switch_pos, default_pos) + 1
    return text[:line_start] + case_line + text[line_start:], True


for n, digits, symbol, p in implemented:
    before = s
    s, did_inc = insert_mapper_include(s, n, digits)
    s, did_case = insert_mapper_case(s, n, digits, symbol)

    if did_inc or did_case:
        wired.append(n)

modified[MAPPER_CPP] = s

if wired:
    print("[ OK ] NES mapper audit: registrados:", ", ".join(map(str, wired)))
else:
    print("[PASS] NES mapper audit: todos os mapperNNN.hpp implementados já estavam registrados")


# ---------------------------------------------------------------------
# SNES forced blank: INIDISP bit 7 == display disabled / forced blank.
# ---------------------------------------------------------------------
s = modified[SNPPU_H]

if MARK_FORCEBLANK not in s:
    pat = (
        r'Bool\s+IsForceBlank\(\)\s+const\s*'
        r'\{\s*return\s+!\(m_Regs\.inidisp\s*&\s*0x80\);\s*\}'
    )

    repl = (
        "/* AURORA_SNES_FORCEBLANK_V1: INIDISP bit 7 means forced blank. */\n"
        "\tBool                    IsForceBlank() const                        "
        "{return (m_Regs.inidisp & 0x80) != 0;}"
    )

    s2, n = re.subn(pat, repl, s, count=1)
    if n == 1:
        s = s2
        modified[SNPPU_H] = s
        print("[ OK ] SNES: IsForceBlank() corrigido")
    elif "(m_Regs.inidisp & 0x80) != 0" in s:
        print("[SKIP] SNES: IsForceBlank() já parece corrigido")
    else:
        die("snppu.h: IsForceBlank() esperado não encontrado")
else:
    print("[SKIP] SNES forced blank já marcado")


# ---------------------------------------------------------------------
# SNES H-flip cache miss: Store4 already generates the cached H-flipped
# row. On an H-flipped miss, load that row instead of reversing it again.
# ---------------------------------------------------------------------
s_h = modified[CHR_CACHE_H]
s_r = modified[RENDER8_CPP]

if "uData4HFlip" in s_h and "SNPPU_CHR_CACHE_HFLIP" in s_h:
    if MARK_HFLIP_MISS not in s_h:
        anchor = "_INLINE void SnesPPUChrCacheInvalidateAll("
        pos = s_h.find(anchor)
        if pos < 0:
            die("snppuchrcache.h: âncora InvalidateAll não encontrada")

        helper = r'''#if SNPPU_CHR_CACHE_HFLIP
/* AURORA_HFLIP_MISS_REUSE_V1
 * Store4() has already produced this mirrored row. The first H-flipped
 * consumer can load it directly instead of doing the byte/mask reversal
 * a second time.
 */
_INLINE void SnesPPUChrCacheLoad4HFlip(
	const SnesPPUChrCacheT *pCache,
	Uint32 uRowAddress,
	Uint64 *pData,
	Uint32 *pOpaque)
{
	Uint32 uAddress = uRowAddress & SNPPU_VRAM_WORD_MASK;
	Uint32 uTile = uAddress >> 4;
	Uint32 uRow = uAddress & 7u;

	*pData = pCache->uData4HFlip[uTile][uRow];
	*pOpaque = pCache->uOpaque4HFlip[uTile][uRow];
}
#endif

'''
        s_h = s_h[:pos] + helper + s_h[pos:]
        modified[CHR_CACHE_H] = s_h

    if MARK_HFLIP_MISS not in s_r:
        pat = (
            r'(SnesPPUChrCacheStore4\(&_SnesPPU_ChrCache,\s*'
            r'uRowAddr,\s*uRowData,\s*uOpaque\);\s*)'
            r'if \(pObj->bHFlip\)\s*'
            r'SnesPPUChrCacheFlipRow\(&uRowData,\s*&uOpaque\);'
        )

        repl = r'''\1/* AURORA_HFLIP_MISS_REUSE_V1 */
						if (pObj->bHFlip)
						{
#if SNPPU_CHR_CACHE_HFLIP
							SnesPPUChrCacheLoad4HFlip(
								&_SnesPPU_ChrCache,
								uRowAddr, &uRowData, &uOpaque);
#else
							SnesPPUChrCacheFlipRow(&uRowData, &uOpaque);
#endif
						}'''

        s2, n = re.subn(pat, repl, s_r, count=1, flags=re.S)
        if n == 1:
            s_r = s2
            modified[RENDER8_CPP] = s_r
            print("[ OK ] SNES: H-flip do primeiro cache miss deixa de ser calculado 2x")
        elif MARK_HFLIP_MISS in s_r:
            print("[SKIP] SNES: H-flip miss já otimizado")
        else:
            print("[WARN] SNES: padrão do OBJ cache mudou; helper criado, renderer não alterado")
else:
    print("[INFO] SNES H-flip cache não está presente; micro-otimização ignorada")


# ---------------------------------------------------------------------
# Sanity: never touch forbidden areas.
# ---------------------------------------------------------------------
if modified.get(SMB2_BG) is not None:
    die("proteção interna: snppubg.cpp entrou indevidamente na lista modificada")

for p, text in modified.items():
    if p == MAKEFILE:
        continue
    for forbidden in ("SNESticle_Aurora/", "SNESticle Aurora/"):
        if forbidden in text:
            die(
                f"proteção de runtime: '{forbidden}' apareceu em "
                f"{p.relative_to(ROOT)}"
            )

required = [
    (modified[CART_H], "submapper_code()", "NES2 submapper accessor"),
    (modified[CART_CPP], MARK_NES2, "NES2 parser"),
    (modified[CORE_CPP], MARK_STROBE, "controller strobe"),
    (modified[MAPPER_206], MARK_206, "mapper206"),
    (modified[SNPPU_H], "(m_Regs.inidisp & 0x80) != 0", "forced blank"),
]

for text, needle, label in required:
    if needle not in text:
        die("sanity: faltando " + label)

if has_trailing_ws(mapper76_text) or has_trailing_ws(mapper95_text):
    die("sanity: whitespace inesperado nos novos mappers")


# ---------------------------------------------------------------------
# Backup.
# ---------------------------------------------------------------------
stamp = time.strftime("%Y%m%d-%H%M%S")
backup_dir = ROOT / ".git" / ("aurora-accuracy-mappers-" + stamp)
backup_dir.mkdir(parents=True, exist_ok=True)

all_targets = set(modified.keys()) | set(new_files.keys())
existed_before = {}

for p in all_targets:
    existed_before[p] = p.exists()
    if p.exists():
        rel = p.relative_to(ROOT)
        dest = backup_dir / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, dest)


def rollback():
    print("[ROLLBACK] restaurando estado anterior ao script...")
    for p in all_targets:
        rel = p.relative_to(ROOT)
        src = backup_dir / rel

        if existed_before.get(p):
            if src.exists():
                p.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, p)
        else:
            if p.exists():
                p.unlink()


# ---------------------------------------------------------------------
# Write.
# ---------------------------------------------------------------------
try:
    for p, text in modified.items():
        old = original.get(p)
        if old is None:
            if not p.exists() or p.read_text(encoding="utf-8") != text:
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(text, encoding="utf-8")
        elif text != old:
            p.write_text(text, encoding="utf-8")

    for p, text in new_files.items():
        if not p.exists():
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text, encoding="utf-8")

    # Untracked files are not covered by git diff --check. Check only
    # files created by this script; pre-existing source may legitimately
    # contain old whitespace that is outside this batch.
    for p in new_files:
        if p.exists() and has_trailing_ws(p.read_text(encoding="utf-8")):
            raise RuntimeError(
                "trailing whitespace em novo arquivo " +
                str(p.relative_to(ROOT))
            )

    qn_targets = [
        str(p.relative_to(QN))
        for p in all_targets
        if QN in p.parents and p.exists()
    ]
    main_targets = [
        str(p.relative_to(ROOT))
        for p in all_targets
        if QN not in p.parents and p.exists()
    ]

    if qn_targets:
        r = run(["git", "diff", "--check", "--"] + qn_targets, cwd=QN)
        if r is None or r.returncode != 0:
            raise RuntimeError(
                "git diff --check no QuickNES falhou:\n" +
                (r.stdout if r else "timeout")
            )

    if main_targets:
        r = run(["git", "diff", "--check", "--"] + main_targets, cwd=ROOT)
        if r is None or r.returncode != 0:
            raise RuntimeError(
                "git diff --check no Aurora falhou:\n" +
                (r.stdout if r else "timeout")
            )

except Exception as e:
    rollback()
    die(str(e))


# ---------------------------------------------------------------------
# Final audit.
# ---------------------------------------------------------------------
print()
print("============================================================")
print(" RESULTADO")
print("============================================================")

print()
print("NES / QuickNES:")
print("  + NES 2.0: mapper de 12 bits + submapper")
print("  + iNES antigo: mapper high nibble tratado conservadoramente")
print("  + iNES PRG count 0 -> 256 bancos")
print("  + controller $4016/$4017 com strobe alto -> A atual")
print("  + mapper 206: WRAM + Namco 108 + submapper 1")
print("  + mapper 88/154: WRAM e mirroring 154 corrigidos")
print("  + mapper 76/95 adicionados quando ausentes")
print("  + auditor registrou mapperNNN.hpp implementado mas esquecido")

print()
print("SNES:")
print("  + INIDISP bit 7 agora é reconhecido como forced blank")
if MARK_HFLIP_MISS in modified[RENDER8_CPP]:
    print("  + OBJ H-flip miss reutiliza o flip já gerado no Store4")
print("  + snppubg.cpp/SMB2 não foi tocado")
print("  + geometria e áudio não foram tocados")

print()
print("Backup:")
print(" ", backup_dir.relative_to(ROOT))

r = run(["git", "-C", str(QN), "rev-parse", "--abbrev-ref", "HEAD"])
branch = r.stdout.strip() if r and r.returncode == 0 else "?"
print()
print("QuickNES branch atual:", branch)
if branch == "HEAD":
    print("  ATENÇÃO: o submodule está detached HEAD.")
    print("  Antes de commitar nele, crie/troque para uma branch.")

print()
print("Revise primeiro:")
print("  git -C src/third_party/quicknes status --short")
print("  git -C src/third_party/quicknes diff")
print("  git diff -- src/snes/ppu/snppu.h src/snes/ppu/snppuchrcache.h src/snes/ppu/snppurender8.cpp")
print()
print("Depois compile/teste:")
print("  ./copy.sh")
print()
print("Testes NES prioritários:")
print("  - mapper 206")
print("  - mapper 206 NES 2.0 submapper 1, se você tiver uma ROM desse board")
print("  - mapper 76 e 95")
print("  - jogos comuns mapper 0/1/2/3/4 para regressão")
print()
print("Testes SNES:")
print("  - Top Gear (velocidade)")
print("  - Super Castlevania IV")
print("  - SMB2 em Super Mario All-Stars (regressão: cortina)")
print()
print("[PASS] patches gravados; git diff --check passou nos arquivos rastreados")
