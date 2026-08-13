#include <cstddef>
#include <cstdio>
#include <cstring>

#include "types.h"
#include "snppu.h"
#include "snppurender.h"

void _DecodeOBJEX(Uint8 *pObjEx, SnesRenderObjT *pObjs, Int32 nObjs,
                  Uint32 uBaseSize);
void _DecodeOBJ(SnesPPUOBJT *pPPUObj, SnesRenderObjT *pObjs, Int32 nObjs,
                Uint8 *pObjY, Uint8 *pObjSize);
Bool _SnesPPUOBJVisibleX(Uint16 uPosX, Uint8 uWidth);
Bool _SnesPPUOBJTileCountedX(Uint16 uObjectX, Int32 iTileX);

static int g_Failures;

static void Check(const char *pName, int nGot, int nExpected)
{
    if (nGot != nExpected)
    {
        std::printf("FAIL %s: %d != %d\n", pName, nGot, nExpected);
        g_Failures++;
    }
}

static void CheckFill(const char *pName, const Uint8 *pData, int nBytes,
                      Uint8 uExpected)
{
    int i;
    for (i = 0; i < nBytes; i++)
    {
        if (pData[i] != uExpected)
        {
            std::printf("FAIL %s[%d]: %02X != %02X\n", pName, i,
                        (unsigned)pData[i], (unsigned)uExpected);
            g_Failures++;
            return;
        }
    }
}

static void InitRenderTile(SnesRenderObj8T *pObj, Int32 iPosX)
{
    int i;
    std::memset(pObj, 0, sizeof(*pObj));
    pObj->iPosX = (Int16)iPosX;
    pObj->uPri = 3;
    pObj->uPal = 4;
    for (i = 0; i < 8; i++)
        pObj->uData[i] = (Uint8)(0x40 + i);
    pObj->uData[SNPPU_BGPLANE_OPAQUE] = 0xFF;
}

int main()
{
    SnesRenderObjT objs[4];
    SnesPPUOBJT raw[4];
    Uint8 objEx[1];
    Uint8 objY[4];
    Uint8 objHeight[4];

    std::memset(objs, 0, sizeof(objs));
    std::memset(raw, 0, sizeof(raw));

    // Alterna small/large nos quatro objetos (bits de size 1, 3, 5 e 7).
    objEx[0] = 0x88;
    _DecodeOBJEX(objEx, objs, 4, 6);
    Check("mode 6 small width",  objs[0].uWidth, 16);
    Check("mode 6 small height", objs[0].uHeight, 32);
    Check("mode 6 large width",  objs[1].uWidth, 32);
    Check("mode 6 large height", objs[1].uHeight, 64);

    raw[0].uAttrib = 0x80;
    raw[1].uAttrib = 0x80;
    _DecodeOBJ(raw, objs, 4, objY, objHeight);
    Check("rect small vflip xor",  objs[0].uVXOR, 15);
    Check("rect large vflip xor",  objs[1].uVXOR, 31);
    Check("rect small visibility", objHeight[0], 32);
    Check("rect large visibility", objHeight[1], 64);

    std::memset(objs, 0, sizeof(objs));
    objEx[0] = 0x88;
    _DecodeOBJEX(objEx, objs, 4, 7);
    Check("mode 7 small width",  objs[0].uWidth, 16);
    Check("mode 7 small height", objs[0].uHeight, 32);
    Check("mode 7 large width",  objs[1].uWidth, 32);
    Check("mode 7 large height", objs[1].uHeight, 32);

	Check("x 0 visible",       _SnesPPUOBJVisibleX(0, 8), TRUE);
	Check("x 255 visible",     _SnesPPUOBJVisibleX(255, 8), TRUE);
	Check("x 256 counted",     _SnesPPUOBJVisibleX(256, 8), TRUE);
	Check("x -1 visible",      _SnesPPUOBJVisibleX(511, 8), TRUE);
	Check("x -7 visible",      _SnesPPUOBJVisibleX(505, 8), TRUE);
	Check("x -8 hidden",       _SnesPPUOBJVisibleX(504, 8), FALSE);
	Check("x -31 visible",     _SnesPPUOBJVisibleX(481, 32), TRUE);
	Check("x -32 hidden",      _SnesPPUOBJVisibleX(480, 32), FALSE);
	Check("tile x -7 counted", _SnesPPUOBJTileCountedX(505, -7), TRUE);
	Check("tile x -8 skipped", _SnesPPUOBJTileCountedX(504, -8), FALSE);
	Check("tile x 255 counted", _SnesPPUOBJTileCountedX(255, 255), TRUE);
	Check("tile x 256 skipped", _SnesPPUOBJTileCountedX(257, 256), FALSE);
	Check("object x 256 quirk", _SnesPPUOBJTileCountedX(256, -256), TRUE);
	Check("OBSEL name select 0", _SnesPPUOBJNameSelect(0x00), 0x1000);
	Check("OBSEL name select 1", _SnesPPUOBJNameSelect(0x08), 0x2000);
	Check("OBSEL name select 2", _SnesPPUOBJNameSelect(0x10), 0x3000);
	Check("OBSEL name select 3", _SnesPPUOBJNameSelect(0x18), 0x4000);
	Check("OBSEL ignores size/base", _SnesPPUOBJNameSelect(0xE7), 0x1000);
	Check("normal tile column 0", _SnesPPUOBJSourceColumn(0, 32, FALSE), 0);
	Check("normal tile column 3", _SnesPPUOBJSourceColumn(3, 32, FALSE), 3);
	Check("hflip left fetches right", _SnesPPUOBJSourceColumn(0, 32, TRUE), 3);
	Check("hflip right fetches left", _SnesPPUOBJSourceColumn(3, 32, TRUE), 0);

    // Tiles parcialmente fora da tela nao podem tocar os buffers vizinhos.
    // Final Fight 2 mantem OBJ em X negativo durante o gameplay.
    {
        Uint8 guardedLine[256 + 16];
        Uint8 *pLine8 = guardedLine + 8;
        SNMaskT planes[SNPPU_BGPLANE_NUM];
        SNMaskT guardedAddSub[3];
        SnesRenderObj8T obj;

        std::memset(guardedLine, 0xCD, sizeof(guardedLine));
        std::memset(pLine8, 0, 256);
        std::memset(planes, 0, sizeof(planes));
        std::memset(guardedAddSub, 0xA5, sizeof(guardedAddSub));
        std::memset(&guardedAddSub[1], 0, sizeof(SNMaskT));
        InitRenderTile(&obj, -7);

        _SnesPPURenderOBJ8(pLine8, planes, &obj, 1, NULL, NULL,
                           &guardedAddSub[1], 1);
        Check("left clip visible pixel", pLine8[0], 0x47);
        Check("left clip next pixel untouched", pLine8[1], 0x00);
        Check("left clip add/sub", guardedAddSub[1].uMask32[0], 0x00000001);
        CheckFill("left line guard before", guardedLine, 8, 0xCD);
        CheckFill("left line guard after", guardedLine + 264, 8, 0xCD);
        CheckFill("left mask guard before", (const Uint8 *)&guardedAddSub[0],
                  sizeof(SNMaskT), 0xA5);
        CheckFill("left mask guard after", (const Uint8 *)&guardedAddSub[2],
                  sizeof(SNMaskT), 0xA5);

        std::memset(guardedLine, 0xCD, sizeof(guardedLine));
        std::memset(pLine8, 0, 256);
        std::memset(planes, 0, sizeof(planes));
        std::memset(guardedAddSub, 0xA5, sizeof(guardedAddSub));
        std::memset(&guardedAddSub[1], 0, sizeof(SNMaskT));
        InitRenderTile(&obj, 255);

        _SnesPPURenderOBJ8(pLine8, planes, &obj, 1, NULL, NULL,
                           &guardedAddSub[1], 1);
        Check("right clip visible pixel", pLine8[255], 0x40);
        Check("right clip previous pixel untouched", pLine8[254], 0x00);
        Check("right clip add/sub", guardedAddSub[1].uMask32[7],
              (int)0x80000000u);
        CheckFill("right line guard before", guardedLine, 8, 0xCD);
        CheckFill("right line guard after", guardedLine + 264, 8, 0xCD);
        CheckFill("right mask guard before", (const Uint8 *)&guardedAddSub[0],
                  sizeof(SNMaskT), 0xA5);
        CheckFill("right mask guard after", (const Uint8 *)&guardedAddSub[2],
                  sizeof(SNMaskT), 0xA5);

        // Prioridade de BG e janela continuam bloqueando somente o pixel alvo.
        std::memset(pLine8, 0, 256);
        std::memset(planes, 0, sizeof(planes));
        std::memset(&guardedAddSub[1], 0, sizeof(SNMaskT));
        InitRenderTile(&obj, 10);
        obj.uPri = 0;
        planes[SNPPU_BGPLANE_LAYER0].uMask32[0] = 1u << 10;
        _SnesPPURenderOBJ8(pLine8, planes, &obj, 1, NULL, NULL,
                           &guardedAddSub[1], 1);
        Check("BG priority blocks first pixel", pLine8[10], 0x00);
        Check("BG priority leaves next pixel", pLine8[11], 0x41);

		// The fast path must split an unaligned tile across adjacent words
		// without changing pixel order or priority masking.
		std::memset(pLine8, 0, 256);
		std::memset(planes, 0, sizeof(planes));
		std::memset(&guardedAddSub[1], 0, sizeof(SNMaskT));
		InitRenderTile(&obj, 29);
		obj.uPri = 0;
		planes[SNPPU_BGPLANE_LAYER0].uMask32[1] = 1u << 1; // x=33
		_SnesPPURenderOBJ8(pLine8, planes, &obj, 1, NULL, NULL,
		                   &guardedAddSub[1], 1);
		Check("word split first pixel", pLine8[29], 0x40);
		Check("word split last low pixel", pLine8[31], 0x42);
		Check("word split first high pixel", pLine8[32], 0x43);
		Check("word split BG priority", pLine8[33], 0x00);
		Check("word split last pixel", pLine8[36], 0x47);
		Check("word split add/sub low", guardedAddSub[1].uMask32[0],
		      (int)0xE0000000u);
		Check("word split add/sub high", guardedAddSub[1].uMask32[1],
		      0x0000001D);
    }

    std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
    return g_Failures ? 1 : 0;
}
