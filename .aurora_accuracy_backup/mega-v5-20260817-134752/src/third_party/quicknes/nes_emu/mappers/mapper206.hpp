/* Copyright notice for this file:
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
