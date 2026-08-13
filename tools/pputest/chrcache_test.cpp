#include <cstdio>
#include <cstring>

#include "types.h"
#include "snppuchrcache.h"

static int g_Failures;
static SnesPPUChrCacheT g_Cache;

static void Check(const char *pName, Uint64 uGot, Uint64 uExpected)
{
	if (uGot != uExpected)
	{
		std::printf("FAIL %s: %llX != %llX\n", pName,
			(unsigned long long)uGot, (unsigned long long)uExpected);
		g_Failures++;
	}
}

int main()
{
	Uint64 uData = 0;
	Uint32 uOpaque = 0;
	Uint32 nInvalidated;

	std::memset(&g_Cache, 0, sizeof(g_Cache));
	Check("CHR cache bytes", sizeof(g_Cache), 448512);
	Check("2bpp cold miss", SnesPPUChrCacheLookup2(&g_Cache,
		0x1234, FALSE, &uData, &uOpaque), FALSE);

	SnesPPUChrCacheStore2(&g_Cache, 0x1234,
		0x0807060504030201ULL, 0xA5);
	Check("2bpp hit", SnesPPUChrCacheLookup2(&g_Cache,
		0x1234, FALSE, &uData, &uOpaque), TRUE);
	Check("2bpp data", uData, 0x0807060504030201ULL);
	Check("2bpp opaque", uOpaque, 0xA5);
	Check("2bpp hflip hit", SnesPPUChrCacheLookup2(&g_Cache,
		0x1234, TRUE, &uData, &uOpaque), TRUE);
	Check("2bpp hflip data", uData, 0x0102030405060708ULL);
	Check("2bpp hflip opaque", uOpaque, 0xA5);

	/* A mesma linha 4bpp atende BG e OBJ; paleta nao faz parte da entrada. */
	SnesPPUChrCacheStore4(&g_Cache, 0x2345,
		0x0F0E0D0C0B0A0908ULL, 0x96);
	Check("4bpp BG hit", SnesPPUChrCacheLookup4(&g_Cache,
		0x2345, FALSE, &uData, &uOpaque), TRUE);
	Check("4bpp OBJ shared hit", SnesPPUChrCacheLookup4(&g_Cache,
		0x2345, FALSE, &uData, &uOpaque), TRUE);
	Check("4bpp data", uData, 0x0F0E0D0C0B0A0908ULL);
	Check("4bpp hflip hit", SnesPPUChrCacheLookup4(&g_Cache,
		0x2345, TRUE, &uData, &uOpaque), TRUE);
	Check("4bpp hflip data", uData, 0x08090A0B0C0D0E0FULL);
	Check("4bpp hflip opaque", uOpaque, 0x69);

	/* Uma palavra tocada invalida as interpretacoes 2bpp e 4bpp do tile. */
	SnesPPUChrCacheStore2(&g_Cache, 0x3451, 0x11, 1);
	SnesPPUChrCacheStore4(&g_Cache, 0x3451, 0x22, 2);
	SnesPPUChrCacheStore2(&g_Cache, 0x3461, 0x33, 3);
	nInvalidated = SnesPPUChrCacheInvalidateRange(&g_Cache, 0x3457, 1);
	Check("single write invalidated tiles", nInvalidated, 2);
	Check("single write clears 2bpp", SnesPPUChrCacheLookup2(&g_Cache,
		0x3451, FALSE, &uData, &uOpaque), FALSE);
	Check("single write clears 4bpp", SnesPPUChrCacheLookup4(&g_Cache,
		0x3451, FALSE, &uData, &uOpaque), FALSE);
	Check("single write keeps neighbor", SnesPPUChrCacheLookup2(&g_Cache,
		0x3461, FALSE, &uData, &uOpaque), TRUE);

	/* O burst pode cruzar $7FFF->$0000; os dois lados devem ser limpos. */
	SnesPPUChrCacheStore2(&g_Cache, 0x7FFF, 0x44, 4);
	SnesPPUChrCacheStore4(&g_Cache, 0x7FFF, 0x55, 5);
	SnesPPUChrCacheStore2(&g_Cache, 0x0000, 0x66, 6);
	SnesPPUChrCacheStore4(&g_Cache, 0x0000, 0x77, 7);
	SnesPPUChrCacheInvalidateRange(&g_Cache, 0x7FFF, 2);
	Check("wrap clears high 2bpp", SnesPPUChrCacheLookup2(&g_Cache,
		0x7FFF, FALSE, &uData, &uOpaque), FALSE);
	Check("wrap clears high 4bpp", SnesPPUChrCacheLookup4(&g_Cache,
		0x7FFF, FALSE, &uData, &uOpaque), FALSE);
	Check("wrap clears low 2bpp", SnesPPUChrCacheLookup2(&g_Cache,
		0x0000, FALSE, &uData, &uOpaque), FALSE);
	Check("wrap clears low 4bpp", SnesPPUChrCacheLookup4(&g_Cache,
		0x0000, FALSE, &uData, &uOpaque), FALSE);

	SnesPPUChrCacheStore2(&g_Cache, 0x1111, 0x88, 8);
	SnesPPUChrCacheStore4(&g_Cache, 0x2222, 0x99, 9);
	SnesPPUChrCacheInvalidateRange(&g_Cache, 0, 0x8000);
	Check("full clear 2bpp", SnesPPUChrCacheLookup2(&g_Cache,
		0x1111, FALSE, &uData, &uOpaque), FALSE);
	Check("full clear 4bpp", SnesPPUChrCacheLookup4(&g_Cache,
		0x2222, FALSE, &uData, &uOpaque), FALSE);

	std::puts(g_Failures ? "FAIL" : "PASS");
	return g_Failures ? 1 : 0;
}
