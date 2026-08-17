#pragma once
#include "Nes_Mapper.h"
#include <string.h>

/* AURORA_MEGA_V5_MAPPER103_V1
 * Mapper 103 / FDS Conversion. Semantics follow Mesen/NESdev mapper 103.
 */
class Mapper103 : public Nes_Mapper
{
    struct state_t { uint8_t bank, ex_reg, ram_enabled; } s;

    void update()
    {
        set_prg_bank(0x8000, bank_16k, 0);
        set_prg_bank(0xc000, bank_16k, last_bank);
        set_chr_ram_bank(0x0000, bank_8k, 0);
        enable_sram(true);
        if (s.ex_reg && !s.ram_enabled)
            set_prg_bank(0x6000, bank_4k, s.bank & 0x0f);
        intercept_reads(0x6000, 0x2000);
        intercept_writes(0x6000, 0x2000);
    }

public:
    Mapper103() { register_state(&s, sizeof s); }
    void reset_state() { memset(&s, 0, sizeof s); }
    void apply_mapping() { update(); }

    int read(nes_time_t time, nes_addr_t addr)
    {
        if (addr >= 0x6000 && addr <= 0x7fff)
        {
            if (addr < 0x7000 && s.ex_reg && !s.ram_enabled)
            {
                long size = cart().prg_size();
                long off = ((long)(s.bank & 0x0f) << 12) | (addr & 0x0fff);
                return size > 0 ? cart().prg()[off % size] : 0xff;
            }
            return sram_read_byte(addr);
        }
        return Nes_Mapper::read(time, addr);
    }

    bool write_intercepted(nes_time_t, nes_addr_t addr, int data)
    {
        if (addr >= 0x6000 && addr <= 0x7fff)
        {
            sram_write_byte(addr, (uint8_t)data);
            return true;
        }
        return false;
    }

    void write(nes_time_t, nes_addr_t addr, int data)
    {
        if ((addr & 0xf800) == 0x8000) s.ex_reg = (uint8_t)((data & 0x10) != 0);
        else if ((addr & 0xf000) == 0x9000) s.bank = (uint8_t)(data & 0x0f);
        else if ((addr & 0xf800) == 0xb800) s.ram_enabled = (uint8_t)((data & 0x10) != 0);
        update();
    }
};
