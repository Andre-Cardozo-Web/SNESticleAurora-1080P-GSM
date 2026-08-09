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

    std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
    return g_Failures ? 1 : 0;
}
