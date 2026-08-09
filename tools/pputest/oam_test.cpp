#include <cstdio>

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

	std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
	return g_Failures ? 1 : 0;
}
