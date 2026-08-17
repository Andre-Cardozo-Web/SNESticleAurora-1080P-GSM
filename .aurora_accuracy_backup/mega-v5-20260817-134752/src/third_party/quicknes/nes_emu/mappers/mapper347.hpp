#pragma once
#include "Nes_Mapper.h"
#include <string.h>

/* AURORA_MEGA_V5_MAPPER347_KS7030_V1
 * NES 2.0 mapper 347 / Kaiser KS7030.
 * Split layout follows NESdev; fixed bank numbers cross-checked with FCEUX.
 */
class Mapper347 : public Nes_Mapper
{
    struct state_t { uint8_t reg0, reg1; } s;

    int ram_index(nes_addr_t addr) const
    {
        if (addr >= 0x6000 && addr <= 0x6bff) return addr - 0x6000;
        if (addr >= 0xb800 && addr <= 0xbfff) return 0x1800 + addr - 0xb800;
        if (addr >= 0xcc00 && addr <= 0xcfff) return 0x0c00 + addr - 0xcc00;
        if (addr >= 0xd000 && addr <= 0xd7ff) return 0x1000 + addr - 0xd000;
        return -1;
    }

    int prg_byte(long off) const
    {
        long size = cart().prg_size();
        if (size <= 0) return 0xff;
        off %= size;
        if (off < 0) off += size;
        return cart().prg()[off];
    }

    void update()
    {
        enable_sram(true);
        set_prg_bank(0x6c00, bank_1k, ((s.reg1 & 0x0f) << 2) + 3);
        set_prg_bank(0x7000, bank_4k, (s.reg0 & 7) | 0x10);
        set_prg_bank(0x8000, bank_4k, 0x18);
        set_prg_bank(0x9000, bank_4k, 0x19);
        set_prg_bank(0xa000, bank_4k, 0x1a);
        set_prg_bank(0xb000, bank_2k, 0x36);
        set_prg_bank(0xc000, bank_4k, s.reg1 & 0x0f);
        set_prg_bank(0xd800, bank_2k, 0x3b);
        set_prg_bank(0xe000, bank_8k, 0x1e);
        if (s.reg0 & 8) mirror_horiz(); else mirror_vert();

        intercept_reads(0x6000, 0x2000);
        intercept_writes(0x6000, 0x2000);
        intercept_reads(0xb000, 0x3000);
        intercept_writes(0xb000, 0x3000);
    }

public:
    Mapper347() { register_state(&s, sizeof s); }
    void reset_state() { memset(&s, 0, sizeof s); }
    void apply_mapping() { update(); }

    int read(nes_time_t time, nes_addr_t addr)
    {
        int ri = ram_index(addr);
        if (ri >= 0) return sram_read_byte(0x6000 + ri);
        if (addr >= 0x6c00 && addr <= 0x6fff)
            return prg_byte(((long)(s.reg1 & 0x0f) << 12) | (addr & 0x0fff));
        if (addr >= 0x7000 && addr <= 0x7fff)
            return prg_byte(((long)((s.reg0 & 7) | 0x10) << 12) | (addr & 0x0fff));
        if (addr >= 0xc000 && addr <= 0xcbff)
            return prg_byte(((long)(s.reg1 & 0x0f) << 12) | (addr & 0x0fff));
        return Nes_Mapper::read(time, addr);
    }

    bool write_intercepted(nes_time_t, nes_addr_t addr, int data)
    {
        int ri = ram_index(addr);
        if (ri >= 0)
        {
            sram_write_byte(0x6000 + ri, (uint8_t)data);
            return true;
        }
        if ((addr >= 0x6c00 && addr <= 0x7fff) ||
            (addr >= 0xc000 && addr <= 0xcbff))
            return true;
        return false;
    }

    void write(nes_time_t, nes_addr_t addr, int data)
    {
        if (addr >= 0x8000 && addr <= 0x8fff) s.reg0 = (uint8_t)(data & 0x0f);
        else if (addr >= 0x9000 && addr <= 0x9fff) s.reg1 = (uint8_t)(data & 0x0f);
        else return;
        update();
    }
};
