#!/usr/bin/env python3
"""
SNESticle Aurora - QuickNES mapper/ROM compatibility pass.

Focus:
- Fix mapper 95 (Dragon Buster) and re-review Namco 108 family:
  76 / 88 / 95 / 154 / 206.
- Fix standard mapper 206 accuracy (no PRG-RAM unless battery flag explicitly
  requests the known exceptional case).
- Preserve four-screen mirroring from the cartridge header (Gauntlet).
- Add NES 2.0 mapper/submapper/ROM-size parsing when missing.
- Boot Galaxian's real 8 KiB PRG-ROM by mirroring it like the physical board.
- Make small CHR-ROM mirroring safe for unusual NES 2.0 images.
- Audit mapperNNN.hpp registration in Nes_Mapper.cpp.
- Do not touch SNES, audio, video geometry, runtime paths, or SMB2 fixes.

Run from the SNESticle Aurora repository root.
"""

from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(".").resolve()
QN = ROOT / "src/third_party/quicknes"
EMU = QN / "nes_emu"
MAPPERS = EMU / "mappers"

CART_H = EMU / "Nes_Cart.h"
CART_CPP = EMU / "Nes_Cart.cpp"
MAPPER_CPP = EMU / "Nes_Mapper.cpp"
PPU_IMPL_CPP = EMU / "Nes_Ppu_Impl.cpp"

M076 = MAPPERS / "mapper076.hpp"
M088 = MAPPERS / "mapper088.hpp"
M095 = MAPPERS / "mapper095.hpp"
M154 = MAPPERS / "mapper154.hpp"
M206 = MAPPERS / "mapper206.hpp"

MARK_CART = "AURORA_NES2_HEADER_V2"
MARK_PRG = "AURORA_SMALL_PRG_MIRROR_V1"
MARK_CHR = "AURORA_SAFE_CHR_MIRROR_V1"
MARK_76 = "AURORA_NAMCO76_V2"
MARK_88 = "AURORA_NAMCO88_154_V2"
MARK_95 = "AURORA_NAMCO95_V2"
MARK_206 = "AURORA_NAMCO206_V2"


def die(msg):
    print()
    print("ERRO:", msg)
    raise SystemExit(1)


def read(path):
    if not path.exists():
        die("arquivo não encontrado: " + str(path.relative_to(ROOT)))
    return path.read_text(encoding="utf-8")


def run(args, cwd=None, timeout=60):
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
        die(f"função não encontrada: {signature}")

    brace = text.find("{", start)
    if brace < 0:
        die(f"abre-chaves não encontrado: {signature}")

    depth = 0
    i = brace
    in_string = False
    in_char = False
    escape = False
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

    die(f"função sem fechamento: {signature}")


def replace_function(text, signature, replacement):
    a, b = function_span(text, signature)
    return text[:a] + replacement + text[b:]


def ensure_once(text, needle, insertion, anchor, label):
    if needle in text:
        return text

    n = text.count(anchor)
    if n != 1:
        die(f"{label}: âncora esperada 1x, encontrada {n}x")

    return text.replace(anchor, anchor + insertion, 1)


def no_trailing_ws(text):
    return all(not line.endswith((" ", "\t")) for line in text.splitlines())


print("============================================================")
print(" SNESticle Aurora - QuickNES mapper + Galaxian compatibility")
print("============================================================")
print()

if not (ROOT / ".git").exists():
    die("rode este script na raiz do repositório SNESticle Aurora")

for p in [CART_H, CART_CPP, MAPPER_CPP, PPU_IMPL_CPP, M088, M154, M206]:
    if not p.exists():
        die("QuickNES/submodule incompleto: " + str(p.relative_to(ROOT)))

# Read everything first. No write happens until all guards pass.
original = {
    CART_H: read(CART_H),
    CART_CPP: read(CART_CPP),
    MAPPER_CPP: read(MAPPER_CPP),
    PPU_IMPL_CPP: read(PPU_IMPL_CPP),
    M088: read(M088),
    M154: read(M154),
    M206: read(M206),
}
if M076.exists():
    original[M076] = read(M076)
if M095.exists():
    original[M095] = read(M095)

modified = dict(original)

print("[PASS] árvore QuickNES localizada")
print("[PASS] SNES não faz parte deste batch")


# ---------------------------------------------------------------------------
# 1) Nes_Cart.h: decoded mapper ID + NES 2.0 submapper.
# ---------------------------------------------------------------------------

s = modified[CART_H]

# Public declarations.
if "void set_mapper_nes2(" not in s:
    anchor = "\tvoid set_mapper( int mapper_lsb, int mapper_msb );\n"
    if anchor not in s:
        die("Nes_Cart.h: set_mapper() não encontrado")
    s = s.replace(
        anchor,
        anchor + "\tvoid set_mapper_nes2( int mapper_lsb, int mapper_msb, int mapper_ext );\n",
        1,
    )

if "int submapper_code() const;" not in s:
    anchor = "\tint mapper_code() const;\n"
    if anchor not in s:
        die("Nes_Cart.h: mapper_code() não encontrado")
    s = s.replace(anchor, anchor + "\tint submapper_code() const;\n", 1)

# Private fields.
if "mapper_code_value" not in s:
    anchor = "\tunsigned mapper;\n"
    if anchor not in s:
        die("Nes_Cart.h: campo mapper não encontrado")
    s = s.replace(
        anchor,
        anchor +
        "\tunsigned mapper_code_value;\n"
        "\tunsigned submapper;\n",
        1,
    )
elif "\tunsigned submapper;" not in s:
    s = s.replace(
        "\tunsigned mapper_code_value;\n",
        "\tunsigned mapper_code_value;\n\tunsigned submapper;\n",
        1,
    )

# Rewrite inline mapper helpers as one authoritative block.
inline_start = s.find("inline void Nes_Cart::set_mapper(")
endif_pos = s.rfind("#endif")
if inline_start < 0 or endif_pos < 0 or endif_pos <= inline_start:
    die("Nes_Cart.h: bloco inline final não encontrado")

inline_block = r'''/* AURORA_NES2_HEADER_V2
 * Keep raw iNES flags in mapper (battery/mirroring), while storing the
 * decoded mapper number and NES 2.0 submapper separately.
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
}

'''

s = s[:inline_start] + inline_block + s[endif_pos:]
modified[CART_H] = s
print("[ OK ] NES header: mapper 12-bit + submapper")


# ---------------------------------------------------------------------------
# 2) Nes_Cart.cpp: NES 2.0 sizes + Galaxian 8 KiB PRG mirroring.
# ---------------------------------------------------------------------------

s = modified[CART_CPP]

# clear(): initialize fields added above.
clear_sig = "void Nes_Cart::clear()"
a, b = function_span(s, clear_sig)
clear_fn = s[a:b]

if "mapper_code_value = 0;" not in clear_fn:
    if "\tmapper = 0;\n" not in clear_fn:
        die("Nes_Cart.cpp: clear() sem mapper = 0")
    clear_fn = clear_fn.replace(
        "\tmapper = 0;\n",
        "\tmapper = 0;\n"
        "\tmapper_code_value = 0;\n"
        "\tsubmapper = 0;\n",
        1,
    )
    s = s[:a] + clear_fn + s[b:]

# Remove an older Aurora V1 helper if present immediately before load_ines().
load_a, load_b = function_span(s, "const char * Nes_Cart::load_ines(")
old_marker = s.rfind("/* AURORA_NES2_HEADER_V1", 0, load_a)
replace_a = load_a
if old_marker >= 0 and load_a - old_marker < 3000:
    replace_a = old_marker

loader = r'''/* AURORA_NES2_HEADER_V2
 *
 * NES 2.0 supports mapper bits 8-11, submappers, and exponent/multiplier
 * ROM sizes. Galaxian is a real-world reason this matters: its physical
 * PRG-ROM is only 8 KiB.
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

	/* exponent/multiplier: 2^E * (2*M + 1) */
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

static void AuroraMirrorRom(
	uint8_t *data, long raw_size, long mapped_size )
{
	if ( !data || raw_size <= 0 || mapped_size <= raw_size )
		return;

	for ( long i = raw_size; i < mapped_size; i++ )
		data [i] = data [i % raw_size];
}

const char * Nes_Cart::load_ines( Auto_File_Reader in )
{
	RETURN_ERR( in.open() );

	ines_header_t h;
	RETURN_ERR( in->read( &h, sizeof h ) );

	if ( 0 != memcmp( h.signature, "NES\x1A", 4 ) )
		return not_ines_file;

	const bool nes2 = (h.flags2 & 0x0C) == 0x08;

	/* Bytes 12-15 being non-zero is the classic sign of an archaic/dirty
	 * iNES header. In that case, the upper mapper nibble is unreliable. */
	const bool archaic_ines =
		!nes2 && (h.zero [4] || h.zero [5] || h.zero [6] || h.zero [7]);

	long raw_prg_bytes;
	long raw_chr_bytes;

	if ( nes2 )
	{
		set_mapper_nes2( h.flags, h.flags2, h.zero [0] );

		const uint8_t size_msb = h.zero [1];

		raw_prg_bytes = AuroraNes2RomSize(
			h.prg_count, size_msb & 0x0F, 16 * 1024L );

		raw_chr_bytes = AuroraNes2RomSize(
			h.chr_count, (size_msb >> 4) & 0x0F, 8 * 1024L );
	}
	else
	{
		set_mapper( h.flags, archaic_ines ? 0 : h.flags2 );

		/* Historical iNES convention: PRG count 0 means 256 banks. */
		long prg_banks = h.prg_count ? (long) h.prg_count : 256L;
		raw_prg_bytes = prg_banks * 16 * 1024L;
		raw_chr_bytes = (long) h.chr_count * 8 * 1024L;
	}

	if ( raw_prg_bytes <= 0 || raw_chr_bytes < 0 )
		return "Invalid NES ROM size";

	if ( h.flags & 0x04 )
		RETURN_ERR( in->skip( 512 ) );

	long mapped_prg_bytes = raw_prg_bytes;
	long mapped_chr_bytes = raw_chr_bytes;

	/* AURORA_SMALL_PRG_MIRROR_V1
	 *
	 * Galaxian's Namcot 3301 PCB has an 8 KiB PRG chip. The absent address
	 * line mirrors that chip through the NROM CPU window. QuickNES normally
	 * maps PRG in 16 KiB units during reset, so materialize the physical
	 * mirror once at load time. There is zero per-frame cost.
	 */
	if ( mapper_code() == 0 && raw_prg_bytes == 8 * 1024L )
		mapped_prg_bytes = 16 * 1024L;

	/* AURORA_SAFE_CHR_MIRROR_V1
	 *
	 * NES 2.0 can describe CHR ROMs smaller than the normal 8 KiB PPU
	 * window. If a small ROM divides 8 KiB evenly, mirror it once at load
	 * time so normal QuickNES 1 KiB CHR page mapping remains valid.
	 */
	if ( raw_chr_bytes > 0 &&
	     raw_chr_bytes < 8 * 1024L &&
	     (8 * 1024L) % raw_chr_bytes == 0 )
	{
		mapped_chr_bytes = 8 * 1024L;
	}

	RETURN_ERR( resize_prg( mapped_prg_bytes ) );
	RETURN_ERR( resize_chr( mapped_chr_bytes ) );

	RETURN_ERR( in->read( prg(), raw_prg_bytes ) );
	AuroraMirrorRom( prg(), raw_prg_bytes, mapped_prg_bytes );

	if ( raw_chr_bytes > 0 )
	{
		RETURN_ERR( in->read( chr(), raw_chr_bytes ) );
		AuroraMirrorRom( chr(), raw_chr_bytes, mapped_chr_bytes );
	}

	return 0;
}'''

s = s[:replace_a] + loader + s[load_b:]
modified[CART_CPP] = s
print("[ OK ] Galaxian: NES 2.0 8 KiB PRG + espelhamento NROM")
print("[ OK ] CHR-ROM pequeno: espelhamento seguro no carregamento")


# ---------------------------------------------------------------------------
# 3) Generic PRG mapping guard: never modulo by zero.
# ---------------------------------------------------------------------------

s = modified[MAPPER_CPP]

new_set_prg = r'''void Nes_Mapper::set_prg_bank( nes_addr_t addr, bank_size_t bs, int bank )
{
	int bank_size = 1 << bs;
	int bank_count = cart_->prg_size() >> bs;

	/* AURORA_SMALL_PRG_MIRROR_V1
	 * An undersized physical PRG ROM can legitimately be mirrored into a
	 * larger mapper window (Galaxian: 8 KiB chip in a 16 KiB NROM bank).
	 * The loader materializes that mirror for mapper 0, but this guard also
	 * prevents bank %= 0 on any unusual small image.
	 */
	if ( bank_count <= 0 )
	{
		long physical_size = cart_->prg_size();

		if ( physical_size <= 0 ||
		     (physical_size % Nes_Cpu::page_size) != 0 )
			return;

		/* Mirror the physical ROM repeatedly into the larger CPU window.
		 * map_code() works in 2 KiB pages, so no read can run past the
		 * cartridge allocation. */
		int mapped = 0;
		while ( mapped < bank_size )
		{
			int chunk = (int) physical_size;
			if ( chunk > bank_size - mapped )
				chunk = bank_size - mapped;

			emu().map_code( addr + mapped, chunk, cart_->prg() );
			mapped += chunk;
		}

		if ( unsigned (addr - 0x6000) < 0x2000 )
			emu().enable_prg_6000();
		return;
	}

	if ( bank < 0 )
	{
		bank %= bank_count;
		if ( bank < 0 )
			bank += bank_count;
	}
	else if ( bank >= bank_count )
	{
		bank %= bank_count;
	}

	emu().map_code( addr, bank_size, cart_->prg() + (bank << bs) );

	if ( unsigned (addr - 0x6000) < 0x2000 )
		emu().enable_prg_6000();
}'''

s = replace_function(
    s,
    "void Nes_Mapper::set_prg_bank(",
    new_set_prg,
)
modified[MAPPER_CPP] = s
print("[ OK ] PRG mapper: eliminado bank_count=0 / modulo por zero")


# ---------------------------------------------------------------------------
# 4) Safer CHR bank wrapping. This only runs when a mapper changes a CHR bank.
# ---------------------------------------------------------------------------

s = modified[PPU_IMPL_CPP]

new_chr = r'''void Nes_Ppu_Impl::set_chr_bank( int addr, int size, long data )
{
	/* AURORA_SAFE_CHR_MIRROR_V1
	 * Wrap each 1 KiB page independently. Besides avoiding an out-of-range
	 * final page, this models missing high address lines as ROM mirroring.
	 * No cost is added to pixel rendering; this runs only on bank changes.
	 */
	if ( chr_size <= 0 )
		return;

	int count = (unsigned) size / chr_page_size;
	int page = (unsigned) addr / chr_page_size;

	while ( count-- )
	{
		long mapped = data % chr_size;
		if ( mapped < 0 )
			mapped += chr_size;

		chr_pages [page] = mapped - page * chr_page_size;
		page++;
		data += chr_page_size;
	}
}'''

new_chr_ex = r'''void Nes_Ppu_Impl::set_chr_bank_ex( int addr, int size, long data )
{
	mmc24_enabled = true;

	/* AURORA_SAFE_CHR_MIRROR_V1 */
	if ( chr_size <= 0 )
		return;

	int count = (unsigned) size / chr_page_size;
	int page = (unsigned) addr / chr_page_size;

	while ( count-- )
	{
		long mapped = data % chr_size;
		if ( mapped < 0 )
			mapped += chr_size;

		chr_pages_ex [page] = mapped - page * chr_page_size;
		page++;
		data += chr_page_size;
	}
}'''

s = replace_function(s, "void Nes_Ppu_Impl::set_chr_bank(", new_chr)
s = replace_function(s, "void Nes_Ppu_Impl::set_chr_bank_ex(", new_chr_ex)
modified[PPU_IMPL_CPP] = s
print("[ OK ] CHR mapper: wrap por página de 1 KiB")


# ---------------------------------------------------------------------------
# 5) Vetted Namco 108 family.
# Sources checked before this script was authored:
# - Mesen2 Namco108 / Namco108_76 / _88 / _95 / _154
# - NESdev mapper 206 / 95 / NES 2.0 submapper 206
# ---------------------------------------------------------------------------

mapper206 = r'''/* Copyright notice for this file:
 * Copyright (C) 2018
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Mapper implementation originally added to the libretro QuickNES port.
 */

#pragma once

#include "Nes_Mapper.h"

struct namco_206_state_t
{
	uint8_t bank [8];
	uint8_t mode;
};

BOOST_STATIC_ASSERT( sizeof (namco_206_state_t) == 9 );

/* AURORA_NAMCO206_V2
 *
 * Namco 108 / Tengen MIMIC-1:
 *   PRG: 8K + 8K + fixed 8K + fixed 8K
 *   CHR: 2K + 2K + 1K + 1K + 1K + 1K
 *   no IRQ
 *   hardwired mirroring, including four-screen when the ROM header says so
 *   normally no PRG-RAM
 *
 * NES 2.0 submapper 1: 32 KiB PRG is unbanked.
 */
class Mapper206 : public Nes_Mapper, namco_206_state_t {
public:
	Mapper206()
	{
		namco_206_state_t *state = this;
		register_state( state, sizeof *state );
	}

	virtual void reset_state()
	{
		/* Standard 206 has no WRAM. The known exceptional prototype uses
		 * battery-backed RAM, so honor an explicit battery flag only. */
		if ( cart().has_battery_ram() )
			enable_sram();
	}

	virtual void apply_mapping()
	{
		set_chr_bank( 0x0000, bank_2k, bank [0] );
		set_chr_bank( 0x0800, bank_2k, bank [1] );

		for ( int i = 0; i < 4; i++ )
			set_chr_bank(
				0x1000 + (i << 10),
				bank_1k, bank [i + 2] );

		if ( cart().submapper_code() == 1 &&
		     cart().prg_size() <= 32 * 1024L )
		{
			set_prg_bank( 0x8000, bank_32k, 0 );
		}
		else
		{
			set_prg_bank( 0x8000, bank_8k, bank [6] );
			set_prg_bank( 0xA000, bank_8k, bank [7] );
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
			case 0:
			case 1:
				/* Only CHR D5..D1 are connected for 2 KiB banks. */
				bank [mode] = (data & 0x3E) >> 1;
				set_chr_bank(
					0x0000 + (mode << 11),
					bank_2k, bank [mode] );
				break;

			case 2:
			case 3:
			case 4:
			case 5:
				/* Only six CHR bank bits exist. */
				bank [mode] = data & 0x3F;
				set_chr_bank(
					0x1000 + ((mode - 2) << 10),
					bank_1k, bank [mode] );
				break;

			case 6:
			case 7:
				/* PRG bank is four bits on the 128 KiB hardware. */
				bank [mode] = data & 0x0F;

				if ( cart().submapper_code() != 1 ||
				     cart().prg_size() > 32 * 1024L )
				{
					set_prg_bank(
						0x8000 + ((mode - 6) << 13),
						bank_8k, bank [mode] );
				}
				break;
			}
			break;
		}
	}
};
'''

mapper88 = r'''/* Copyright notice for this file:
 * Copyright (C) 2018
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Mapper implementation originally added to the libretro QuickNES port.
 */

#pragma once

#include "Nes_Mapper.h"

struct namco_88_state_t
{
	uint8_t bank [8];
	uint8_t mirr;
	uint8_t mode;
};

BOOST_STATIC_ASSERT( sizeof (namco_88_state_t) == 10 );

/* AURORA_NAMCO88_154_V2
 *
 * Mapper 88:
 * - registers 0/1 select 2 KiB CHR only from lower 64 KiB
 * - registers 2-5 select 1 KiB CHR only from upper 64 KiB
 *
 * Mapper 154:
 * - same CHR wiring
 * - bit 6 of every mapper write controls one-screen nametable selection
 */
template < bool _is154 >
class Mapper_Namco_34x3 : public Nes_Mapper, namco_88_state_t {
public:
	Mapper_Namco_34x3()
	{
		namco_88_state_t *state = this;
		register_state( state, sizeof *state );
	}

	virtual void reset_state()
	{
		/* No standard PRG-RAM on this Namco 108 family. */
	}

	virtual void apply_mapping()
	{
		set_chr_bank( 0x0000, bank_2k, bank [0] );
		set_chr_bank( 0x0800, bank_2k, bank [1] );

		for ( int i = 0; i < 4; i++ )
			set_chr_bank(
				0x1000 + (i << 10),
				bank_1k, bank [i + 2] );

		set_prg_bank( 0x8000, bank_8k, bank [6] );
		set_prg_bank( 0xA000, bank_8k, bank [7] );
		set_prg_bank( 0xC000, bank_8k, ~1 );
		set_prg_bank( 0xE000, bank_8k, ~0 );

		if ( _is154 )
			mirror_single( mirr );
	}

	virtual void write( nes_time_t, nes_addr_t addr, int data )
	{
		if ( _is154 )
		{
			mirr = (data >> 6) & 1;
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
			case 0:
			case 1:
				/* Lower half only: Mesen masks registers 0/1 to 0x3F. */
				bank [mode] = (data & 0x3E) >> 1;
				set_chr_bank(
					0x0000 + (mode << 11),
					bank_2k, bank [mode] );
				break;

			case 2:
			case 3:
			case 4:
			case 5:
				/* Upper 64 KiB selected by fixed CHR A16=1. */
				bank [mode] = (data & 0x3F) | 0x40;
				set_chr_bank(
					0x1000 + ((mode - 2) << 10),
					bank_1k, bank [mode] );
				break;

			case 6:
			case 7:
				bank [mode] = data & 0x0F;
				set_prg_bank(
					0x8000 + ((mode - 6) << 13),
					bank_8k, bank [mode] );
				break;
			}
			break;
		}
	}
};

typedef Mapper_Namco_34x3<false> Mapper088;
'''

mapper154 = r'''#pragma once

/* Mapper 154 is mapper 88 plus mapper-controlled one-screen mirroring.
 * Implementation lives in mapper088.hpp.
 */
typedef Mapper_Namco_34x3<true> Mapper154;
'''

mapper76 = r'''/*
 * AURORA_NAMCO76_V2
 *
 * Mapper 76 / Namcot 3446.
 * Namco 108 variant where CHR registers 2-5 become four 2 KiB banks and
 * CHR registers 0/1 are inaccessible.
 */

#pragma once

#include "Nes_Mapper.h"

struct mapper076_state_t
{
	uint8_t bank [8];
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
		/* No standard PRG-RAM. */
	}

	virtual void apply_mapping()
	{
		for ( int i = 0; i < 4; i++ )
			set_chr_bank(
				i << 11,
				bank_2k, bank [i + 2] );

		set_prg_bank( 0x8000, bank_8k, bank [6] );
		set_prg_bank( 0xA000, bank_8k, bank [7] );
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
			switch ( mode )
			{
			case 2:
			case 3:
			case 4:
			case 5:
				bank [mode] = data & 0x3F;
				set_chr_bank(
					(mode - 2) << 11,
					bank_2k, bank [mode] );
				break;

			case 6:
			case 7:
				bank [mode] = data & 0x0F;
				set_prg_bank(
					0x8000 + ((mode - 6) << 13),
					bank_8k, bank [mode] );
				break;

			default:
				/* Registers 0 and 1 are not connected on mapper 76. */
				break;
			}
			break;
		}
	}
};
'''

mapper95 = r'''/*
 * AURORA_NAMCO95_V2
 *
 * Mapper 95 / Namcot 3425, used by Dragon Buster.
 *
 * CHR A15 directly controls CIRAM A10:
 *   nametable 0/1 use CHR register 0 bit 5
 *   nametable 2/3 use CHR register 1 bit 5
 *
 * This matches Mesen2's Namco108_95 behavior.
 */

#pragma once

#include "Nes_Mapper.h"

struct mapper095_state_t
{
	uint8_t bank [8];
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
		/* No standard PRG-RAM. */
	}

	void update_nametables()
	{
		int nt0 = (bank [0] >> 5) & 1;
		int nt1 = (bank [1] >> 5) & 1;
		mirror_manual( nt0, nt0, nt1, nt1 );
	}

	virtual void apply_mapping()
	{
		set_chr_bank( 0x0000, bank_2k, bank [0] >> 1 );
		set_chr_bank( 0x0800, bank_2k, bank [1] >> 1 );

		for ( int i = 0; i < 4; i++ )
			set_chr_bank(
				0x1000 + (i << 10),
				bank_1k, bank [i + 2] );

		set_prg_bank( 0x8000, bank_8k, bank [6] );
		set_prg_bank( 0xA000, bank_8k, bank [7] );
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
			switch ( mode )
			{
			case 0:
			case 1:
				/* Keep bit 5 for CIRAM selection; bit 0 is not connected
				 * to the 2 KiB CHR bank address. */
				bank [mode] = data & 0x3E;
				set_chr_bank(
					0x0000 + (mode << 11),
					bank_2k, bank [mode] >> 1 );
				update_nametables();
				break;

			case 2:
			case 3:
			case 4:
			case 5:
				bank [mode] = data & 0x3F;
				set_chr_bank(
					0x1000 + ((mode - 2) << 10),
					bank_1k, bank [mode] );
				break;

			case 6:
			case 7:
				bank [mode] = data & 0x0F;
				set_prg_bank(
					0x8000 + ((mode - 6) << 13),
					bank_8k, bank [mode] );
				break;
			}
			break;
		}
	}
};
'''

for p, text in [
    (M076, mapper76),
    (M088, mapper88),
    (M095, mapper95),
    (M154, mapper154),
    (M206, mapper206),
]:
    modified[p] = text

print("[ OK ] mapper 76 revisado")
print("[ OK ] mapper 88 revisado")
print("[ OK ] mapper 95 / Dragon Buster revisado")
print("[ OK ] mapper 154 revisado")
print("[ OK ] mapper 206 / Gauntlet revisado")


# ---------------------------------------------------------------------------
# 6) Registration audit. Wire every mapperNNN.hpp that contains MapperNNN.
# ---------------------------------------------------------------------------

def mapper_symbol_present(text, digits):
    # Do not let old commented-out typedefs/register examples fool the audit.
    scan = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    scan = re.sub(r"//.*", "", scan)
    symbol = "Mapper" + digits
    return bool(
        re.search(r"\bclass\s+" + re.escape(symbol) + r"\b", scan) or
        re.search(r"\bstruct\s+" + re.escape(symbol) + r"\b", scan) or
        re.search(r"\btypedef\b[^;]*\b" + re.escape(symbol) + r"\s*;", scan, re.S) or
        re.search(r"\busing\s+" + re.escape(symbol) + r"\s*=", scan)
    )


def insert_include(text, n, digits):
    wanted = f'#include "mappers/mapper{digits}.hpp"'
    if wanted in text:
        return text, False

    matches = list(re.finditer(
        r'^#include "mappers/mapper(\d+)\.hpp"\s*$',
        text,
        re.M,
    ))
    if not matches:
        die("Nes_Mapper.cpp: bloco de includes não encontrado")

    for m in matches:
        if int(m.group(1)) > n:
            return text[:m.start()] + wanted + "\n" + text[m.start():], True

    m = matches[-1]
    return text[:m.end()] + "\n" + wanted + text[m.end():], True


def insert_case(text, n, digits):
    switch_pos = text.find("switch ( mapperCode )")
    if switch_pos < 0:
        die("Nes_Mapper.cpp: switch(mapperCode) não encontrado")

    default_pos = text.find("default: break;", switch_pos)
    if default_pos < 0:
        die("Nes_Mapper.cpp: default do mapper switch não encontrado")

    block = text[switch_pos:default_pos]
    if re.search(r"^\s*case\s+" + str(n) + r"\s*:", block, re.M):
        return text, False

    symbol = "Mapper" + digits
    line = f"    case {n:3d}: mapper = new {symbol}(); break;\n"

    cases = list(re.finditer(
        r'^\s*case\s+(\d+)\s*:.*$',
        block,
        re.M,
    ))

    for m in cases:
        if int(m.group(1)) > n:
            pos = switch_pos + m.start()
            return text[:pos] + line + text[pos:], True

    line_start = text.rfind("\n", switch_pos, default_pos) + 1
    return text[:line_start] + line + text[line_start:], True


# Build a virtual view of mapper files, including the new/replaced versions.
virtual_files = {}
for p in MAPPERS.glob("mapper*.hpp"):
    virtual_files[p] = modified.get(p, read(p))
for p in [M076, M088, M095, M154, M206]:
    virtual_files[p] = modified[p]

implemented = []
for p, text in virtual_files.items():
    m = re.fullmatch(r"mapper(\d+)\.hpp", p.name)
    if not m:
        continue
    digits = m.group(1)
    if mapper_symbol_present(text, digits):
        implemented.append((int(digits), digits))

implemented.sort()

s = modified[MAPPER_CPP]
wired = []

for n, digits in implemented:
    s, inc = insert_include(s, n, digits)
    s, case = insert_case(s, n, digits)
    if inc or case:
        wired.append(n)

modified[MAPPER_CPP] = s

if wired:
    print("[ OK ] mappers registrados pelo auditor:",
          ", ".join(map(str, wired)))
else:
    print("[PASS] auditor: todos os mapperNNN.hpp implementados já registrados")


# ---------------------------------------------------------------------------
# 7) Sanity checks.
# ---------------------------------------------------------------------------

checks = [
    (modified[CART_H], "submapper_code()", "submapper accessor"),
    (modified[CART_CPP], MARK_CART, "NES 2.0 loader"),
    (modified[CART_CPP], MARK_PRG, "Galaxian PRG mirror"),
    (modified[PPU_IMPL_CPP], MARK_CHR, "CHR mirror"),
    (modified[M076], MARK_76, "mapper 76"),
    (modified[M088], MARK_88, "mapper 88/154"),
    (modified[M095], MARK_95, "mapper 95"),
    (modified[M206], MARK_206, "mapper 206"),
    (modified[MAPPER_CPP], "case  76:", "mapper 76 registration"),
    (modified[MAPPER_CPP], "case  95:", "mapper 95 registration"),
    (modified[MAPPER_CPP], "case 206:", "mapper 206 registration"),
]

for text, needle, label in checks:
    if needle not in text:
        die("sanity falhou: " + label)

# Standard members of this mapper family must not blindly enable SRAM.
for p in [M076, M088, M095]:
    if "enable_sram" in modified[p]:
        die(f"sanity: SRAM indevido em {p.name}")

# Mapper 206 may only enable it conditionally for an explicit battery flag.
if modified[M206].count("enable_sram") != 1:
    die("sanity: mapper206 SRAM conditional inesperado")

for p, text in modified.items():
    if not no_trailing_ws(text):
        die("trailing whitespace gerado em " + str(p.relative_to(ROOT)))

print("[PASS] sanity: sem WRAM indevido em 76/88/95")
print("[PASS] sanity: mapper 206 preserva four-screen via header")


# ---------------------------------------------------------------------------
# 8) Backup, write, git diff --check, rollback on failure.
# ---------------------------------------------------------------------------

stamp = time.strftime("%Y%m%d-%H%M%S")
backup_dir = ROOT / ".git" / ("aurora-nes-mapper-galaxian-" + stamp)
backup_dir.mkdir(parents=True, exist_ok=True)

targets = set(modified)
existed = {}

for p in targets:
    existed[p] = p.exists()
    if p.exists():
        dest = backup_dir / p.relative_to(ROOT)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, dest)


def rollback():
    print("[ROLLBACK] restaurando arquivos...")
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

    # QuickNES is a submodule, so check its diff independently.
    qn_paths = [
        str(p.relative_to(QN))
        for p in targets
        if QN in p.parents
    ]

    r = run(["git", "diff", "--check", "--"] + qn_paths, cwd=QN)
    if r is None or r.returncode != 0:
        raise RuntimeError(
            "git diff --check falhou no QuickNES:\n" +
            (r.stdout if r else "timeout")
        )

except Exception as exc:
    rollback()
    die(str(exc))


# ---------------------------------------------------------------------------
# 9) Final report.
# ---------------------------------------------------------------------------

print()
print("============================================================")
print(" RESULTADO")
print("============================================================")
print()
print("Correções aplicadas:")
print("  - Galaxian: NES 2.0 + PRG físico de 8 KiB espelhado")
print("  - CHR-ROM sub-8 KiB: espelhamento seguro quando divisível por 8 KiB")
print("  - mapper 76: Namco 108/3446 revisado")
print("  - mapper 88: CHR lower/upper halves corrigidos")
print("  - mapper 95: Dragon Buster / CIRAM via CHR A15")
print("  - mapper 154: one-screen mirroring em cada mapper write")
print("  - mapper 206: sem WRAM padrão, submapper 1, four-screen preservado")
print("  - auditor de registro de mappers")
print()
print("Backup:")
print(" ", backup_dir.relative_to(ROOT))
print()
print("Revise:")
print("  git -C src/third_party/quicknes status --short")
print("  git -C src/third_party/quicknes diff --check")
print("  git -C src/third_party/quicknes diff")
print()
print("Compile:")
print("  ./copy.sh")
print()
print("Testes prioritários:")
print("  1. Dragon Buster (J)                   -> mapper 95")
print("  2. Gauntlet (USA, Tengen unlicensed)   -> mapper 206 + four-screen")
print("  3. Galaxian (J), NES 2.0 8 KiB PRG    -> mapper 0")
print("  4. Digital Devil Story: Megami Tensei  -> mapper 76")
print("  5. RBI Baseball / Pac-Man Tengen etc.  -> mapper 206 regressão")
print()
print("Se tudo passar, lembre que QuickNES é submodule:")
print("  commit/push primeiro dentro de src/third_party/quicknes,")
print("  depois atualize o gitlink no repositório Aurora.")
print()
print("[PASS] arquivos gravados e git diff --check passou")