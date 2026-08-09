#include <cstdio>
#include <cstring>

#include "types.h"
#include "snppu.h"

class TestRender : public ISnesPPURender
{
public:
	TestRender()
	{
		m_UpdateFlags = 0;
		m_pPPU = NULL;
	}

	void BeginRender(CRenderSurface *) {}
	void EndRender() {}
	void RenderLine(Int32) {}
};

static int g_Failures;

static void Check(const char *pName, int nGot, int nExpected)
{
	if (nGot != nExpected)
	{
		std::printf("FAIL %s: %d != %d\n", pName, nGot, nExpected);
		g_Failures++;
	}
}

int main()
{
	SnesPPU ppu;
	TestRender render;
	Uint8 *pOAM;

	ppu.SetPPURender(&render);
	render.SetPPU(&ppu);
	ppu.Reset();
	pOAM = (Uint8 *)ppu.GetOAM();

	// OAMADDL must not discard the high-table bit selected by OAMADDH.
	ppu.Write8(0x2103, 0x01);
	ppu.Write8(0x2102, 0x12);
	Check("OAMADDL preserves bit 9", ppu.GetRegs()->oamaddr.w, 0x224);

	// Low OAM commits an even/odd pair only when the odd byte arrives.
	ppu.Write8(0x2103, 0x00);
	ppu.Write8(0x2102, 0x00);
	ppu.WriteOAMDATA(0x12);
	Check("low OAM even byte stays latched", pOAM[0], 0x00);
	Check("low OAM increments after even", ppu.GetRegs()->oamaddr.w, 0x01);
	ppu.WriteOAMDATA(0x34);
	Check("low OAM commits latched byte", pOAM[0], 0x12);
	Check("low OAM commits odd byte", pOAM[1], 0x34);

	ppu.Write8(0x2102, 0x00);
	ppu.WriteOAMDATA(0xAA);
	ppu.Write8(0x2102, 0x01);
	Check("address reset discards unpaired byte", pOAM[0], 0x12);

	// High OAM writes immediately and mirrors every 32 logical bytes.
	ppu.Write8(0x2102, 0x00);
	ppu.Write8(0x2103, 0x01);
	ppu.WriteOAMDATA(0xA5);
	Check("high OAM direct write", ppu.GetOAM()->ObjEx[0], 0xA5);
	ppu.Write8(0x2102, 0x10);
	ppu.WriteOAMDATA(0x5A);
	Check("high OAM mirror", ppu.GetOAM()->ObjEx[0], 0x5A);
	ppu.Write8(0x2102, 0x10);
	Check("high OAM mirrored read", ppu.ReadOAMDATA(), 0x5A);

	// Priority rotation follows the current byte address as the port advances.
	ppu.Write8(0x2102, 0x00);
	ppu.Write8(0x2103, 0x80);
	ppu.WriteOAMDATA(0x01);
	ppu.WriteOAMDATA(0x02);
	ppu.WriteOAMDATA(0x03);
	ppu.WriteOAMDATA(0x04);
	Check("priority address advances", ppu.GetRegs()->oamaddr.w, 0x8004);
	Check("priority first object advances", ppu.GetRegs()->oampri.w, 1);
	ppu.Write8(0x2103, 0x00);
	Check("priority rotation disabled", ppu.GetRegs()->oampri.w, 0);

	// The optimized full-frame OAM DMA must match byte-port semantics.
	{
		SnesPPU reference;
		TestRender referenceRender;
		Uint8 data[sizeof(SnesOAMT)];
		int i;

		reference.SetPPURender(&referenceRender);
		referenceRender.SetPPU(&reference);
		reference.Reset();
		ppu.Reset();
		for (i = 0; i < (int)sizeof(data); i++)
			data[i] = (Uint8)(i * 37 + 11);

		for (i = 0; i < (int)sizeof(data); i++)
			reference.WriteOAMDATA(data[i]);
		ppu.WriteOAMBlock(data, sizeof(data));

		Check("OAM DMA data",
		      std::memcmp(ppu.GetOAM(), reference.GetOAM(), sizeof(SnesOAMT)), 0);
		Check("OAM DMA address", ppu.GetRegs()->oamaddr.w,
		      reference.GetRegs()->oamaddr.w);
		Check("OAM DMA priority", ppu.GetRegs()->oampri.w,
		      reference.GetRegs()->oampri.w);
	}

	// Common mode-1 VRAM DMA writes consecutive low/high byte pairs.
	{
		const Uint8 data[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
		ppu.Reset();
		ppu.Write8(0x2115, 0x80); // increment by one after $2119
		ppu.Write8(0x2116, 0x34);
		ppu.Write8(0x2117, 0x12);
		ppu.WriteVMDATABlock(data, sizeof(data));
		Check("VRAM block word 0", ppu.GetVramPtr(0x1234)[0], 0x2211);
		Check("VRAM block word 1", ppu.GetVramPtr(0x1235)[0], 0x4433);
		Check("VRAM block word 2", ppu.GetVramPtr(0x1236)[0], 0x6655);
		Check("VRAM block address", ppu.GetRegs()->vmaddr.w, 0x1237);
		Check("VRAM block read latch", ppu.GetRegs()->vmreadlatch.w, 0x1236);
	}

	// Physical VRAM wraps every $8000 words while VMADDR remains 16-bit.
	{
		const Uint8 data[] = {0xAA, 0xBB, 0xCC, 0xDD};
		ppu.Reset();
		ppu.Write8(0x2115, 0x80);
		ppu.Write8(0x2116, 0xFF);
		ppu.Write8(0x2117, 0x7F);
		ppu.WriteVMDATABlock(data, sizeof(data));
		Check("VRAM block last word", ppu.GetVramPtr(0x7FFF)[0], 0xBBAA);
		Check("VRAM block wrapped word", ppu.GetVramPtr(0x0000)[0], 0xDDCC);
		Check("VRAM wrapped address", ppu.GetRegs()->vmaddr.w, 0x8001);
	}

	// An odd byte count ends on the low port without a high-port increment.
	{
		const Uint8 data[] = {0xAA, 0xBB, 0xCC};
		ppu.Reset();
		ppu.Write8(0x2115, 0x80);
		ppu.Write8(0x2116, 0x20);
		ppu.Write8(0x2117, 0x00);
		ppu.WriteVMDATABlock(data, sizeof(data));
		Check("VRAM odd pair", ppu.GetVramPtr(0x20)[0], 0xBBAA);
		Check("VRAM odd low byte", ppu.GetVramPtr(0x21)[0], 0x00CC);
		Check("VRAM odd address", ppu.GetRegs()->vmaddr.w, 0x21);
	}

	// Non-linear increment modes retain the exact per-port fallback path.
	{
		const Uint8 data[] = {0x12, 0x34};
		ppu.Reset();
		ppu.Write8(0x2115, 0x00); // increment after $2118
		ppu.Write8(0x2116, 0x30);
		ppu.Write8(0x2117, 0x00);
		ppu.WriteVMDATABlock(data, sizeof(data));
		Check("VRAM low-increment low", ppu.GetVramPtr(0x30)[0], 0x0012);
		Check("VRAM low-increment high", ppu.GetVramPtr(0x31)[0], 0x3400);
		Check("VRAM low-increment address", ppu.GetRegs()->vmaddr.w, 0x31);
		Check("VRAM low-increment latch", ppu.GetRegs()->vmreadlatch.w, 0x31);
	}

	std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
	return g_Failures ? 1 : 0;
}
