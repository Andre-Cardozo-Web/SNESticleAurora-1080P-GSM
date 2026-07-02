/* SNESticle - (c) 2009 - Copyleft GNU GPL
   By the autors of Snes9x and of the original SNESticle project
   http://www.dc-swat.ru/home/
*/
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libpad.h>
#include <libmc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "../../main.h"
#include "../../app/app.h"
#include "../../../snes/snes.h"
#include "ps2.h"
#include "iop.h"
#include "ee.h"
#include "pad.h"
#include "gfx.h"
#include "audio.h"
#include "timer.h"
#include "cdfs.h"
#include "mc.h"

extern int Ps2MainLoop(int vsync);

/* BGM (background music) para o browser do emulator.
   Pode vir de:
   - mc0:/mass/SNESticle/bgm/ (memory card, sempre disponivel no boot)
   - cdfs:/BGM (ISO da rom, so' se o boot for por ISO/DVD)

   A musica eh indexada por numero (00.adpcm, 01.adpcm, etc.) com fade-in
   de 1 segundo e loop automatico. */

typedef struct
{
	u32 dma_tag_id;
	u32 dma_data[3]; /* {addr, size, next} */
}	DmaCmdPacket;

typedef struct
{
	u32 play_index;
	u32 play_pos;
	u32 fade_in;
	u32 fade_out;
	u32 loop_counter;
	u32 update_counter;
	SPU_DATA *spu;
}	BgmPlayState;

typedef struct
{
	char name[32];
	u32 size;
	BgmPlayState playback;
}	BgmEntry;

typedef struct
{
	BgmEntry *list;
	u32 list_size;
	u32 list_count;
	char base_path[256];
	int scan_enabled;
}	BgmIndex;

static BgmIndex g_BgmIndex = {0};

/* Musica de fundo eh entregue pelo IOP via DMA.
   Retorna a quantidade de samples que foram queued. */
u32 BgmGetQueuedSamples()
{
	if (!g_BgmIndex.list || !g_BgmIndex.list_count) return 0;

	BgmEntry *entry = &g_BgmIndex.list[g_BgmIndex.list[0].playback.play_index];
	return entry->playback.spu ? entry->playback.spu->pos.cur : 0;
}

/* Limpa o index de musicas. */
static void _ClearIndex(void)
{
	if (g_BgmIndex.list)
	{
		free(g_BgmIndex.list);
		g_BgmIndex.list = NULL;
	}
	g_BgmIndex.list_count = 0;
	g_BgmIndex.list_size = 0;
	g_BgmIndex.base_path[0] = 0;
}

/* Re-escaneia o diretorio de musicas (para trocar de ISO/device). */
void BgmRebuildIndex(const char *base_path)
{
	_ClearIndex();

	if (!base_path || !base_path[0])
	{
		DLog("[bgm] rebuild: sem path\n");
		return;
	}

	strncpy(g_BgmIndex.base_path, base_path, sizeof(g_BgmIndex.base_path) - 1);
	g_BgmIndex.base_path[sizeof(g_BgmIndex.base_path) - 1] = 0;

	DLog("[bgm] rebuild: base_path='%s'\n", base_path);

	_BuildIndex();
}

/* Le um arquivo .adpcm e decompacta ele na memoria SPU via IOP DMA. */
static Bool _LoadTrack(const char *path, BgmPlayState *playback)
{
	FILE *fp;
	ADPCM_HEADER header;
	u32 read_size;
	void *dma_buf;
	int ret;

	if (!path || !path[0])
	{
		DLog("[bgm] load: arquivo invalido\n");
		return FALSE;
	}

	DLog("[bgm] load file '%s'...", path);
	fp = fopen(path, "rb");
	if (!fp)
	{
		DLog(" ERRO (nao abriu)\n");
		return FALSE;
	}

	/* Le header ADPCM */
	if (fread(&header, sizeof(ADPCM_HEADER), 1, fp) != 1)
	{
		DLog(" ERRO (header)\n");
		fclose(fp);
		return FALSE;
	}

	/* Aloca DMA buffer pro IOP */
	dma_buf = malloc(header.data_size + 64);
	if (!dma_buf)
	{
		DLog(" ERRO (DMA buffer)\n");
		fclose(fp);
		return FALSE;
	}

	/* Le os samples ADPCM */
	read_size = fread(dma_buf, 1, header.data_size, fp);
	fclose(fp);

	if (read_size != header.data_size)
	{
		DLog(" ERRO (read)\n");
		free(dma_buf);
		return FALSE;
	}

	/* Envia pro IOP via RPC */
	ret = Ps2SoundLoadAdpcm(&header, dma_buf, playback);
	free(dma_buf);

	if (ret != 0)
	{
		DLog(" ERRO (IOP RPC)\n");
		return FALSE;
	}

	DLog(" OK (samples=%u, rate=%u)\n", header.num_samples, header.sample_rate);
	return TRUE;
}

/* Procura a proxima musica e a carrega. */
static Bool _PlayNext(void)
{
	BgmEntry *entry;
	char path[512];

	if (!g_BgmIndex.list || !g_BgmIndex.list_count)
		return FALSE;

	entry = &g_BgmIndex.list[g_BgmIndex.list[0].playback.play_index];

	/* Proxima musica */
	entry->playback.play_index = (entry->playback.play_index + 1) % g_BgmIndex.list_count;

	entry = &g_BgmIndex.list[entry->playback.play_index];

	/* Monta o path completo */
	snprintf(path, sizeof(path), "%s/%s", g_BgmIndex.base_path, entry->name);

	DLog("[bgm] play track %u: '%s'\n", entry->playback.play_index, entry->name);

	if (!_LoadTrack(path, &entry->playback))
	{
		DLog("[bgm] ERRO ao carregar track %u\n", entry->playback.play_index);
		return FALSE;
	}

	entry->playback.fade_in = 60; /* 1 segundo em 60fps */
	entry->playback.fade_out = 0;
	entry->playback.loop_counter = 0;
	entry->playback.update_counter = 0;

	return TRUE;
}

/* Compara extensao de arquivo. */
static Bool _HasExt(const char *name, const char *ext)
{
	size_t name_len, ext_len;

	if (!name || !ext)
		return FALSE;

	name_len = strlen(name);
	ext_len = strlen(ext);

	if (name_len < ext_len)
		return FALSE;

	return (strcasecmp(&name[name_len - ext_len], ext) == 0);
}

/* Diretorio de boot (HDD ELF ou ISO).
   Retorna o diretorio relativo pro boot (ex. "cdfs:/cdrom:1/SNES" pro ISO).
   LADO do ELF. */
char *MainGetBootDir();

/* Um caminho aponta pro DRIVE de CD/DVD?  O primeiro acesso a cdfs:/cdrom:
   logo no boot por ISO (num PS2 real) TRAVA: o menu abre e congela na tela
   de selecao de dispositivo, porque o mecha ainda esta identificando o
   disco (pos-SifIopReset + sceCdInit SCECdINoD, que nao espera disco).  A
   varredura automatica da trilha de menu era o unico acesso ao disco nessa
   tela, entao ela pula o disco -- o browser le o cdfs: so' quando o usuario
   entra nele (segundos depois, drive ja' assentado).  BGM de mc0:/mass:
   continua normal. */
static Bool _IsDiscPath(const char *p)
{
	if (!p) return FALSE;
	return (strncmp(p, "cdfs",  4) == 0 ||
	        strncmp(p, "cdrom", 5) == 0) ? TRUE : FALSE;
}

static void _BuildIndex(void)
{
	size_t d;
	char scanDir[256];
	DIR *pDir;
	struct dirent *dirent;
	BgmEntry *new_list;
	u32 new_count;

	if (!g_BgmIndex.base_path[0])
	{
		DLog("[bgm] _BuildIndex: sem base_path\n");
		return;
	}

	new_list = NULL;
	new_count = 0;

	for (d = 0; d < 2; d++)
	{
		if (d == 0)
			snprintf(scanDir, sizeof(scanDir), "%s", g_BgmIndex.base_path);
		else
			snprintf(scanDir, sizeof(scanDir), "%s/SFX", g_BgmIndex.base_path);

		if (!scanDir || !scanDir[0]) continue;

		/* Nunca toca o drive de CD/DVD automaticamente na tela de menu
		   (trava o boot por ISO no PS2 real -- ver _IsDiscPath). */
		if (_IsDiscPath(scanDir))
		{
			DLog("[bgm] scan skip disc path '%s'", scanDir);
			continue;
		}

		DLog("[bgm] scan opendir('%s')...", scanDir);
		pDir = opendir(scanDir);
		DLog("[bgm] scan opendir('%s') -> %p", scanDir, (void *)pDir);

		if (!pDir) continue;

		while ((dirent = readdir(pDir)) != NULL)
		{
			if (_HasExt(dirent->d_name, ".adpcm"))
			{
				new_list = (BgmEntry*)realloc(new_list, (new_count + 1) * sizeof(BgmEntry));
				if (!new_list) break;

				memset(&new_list[new_count], 0, sizeof(BgmEntry));
				strncpy(new_list[new_count].name, dirent->d_name, sizeof(new_list[new_count].name) - 1);

				DLog("[bgm] found track %u: '%s'", new_count, dirent->d_name);
				new_count++;
			}
		}

		closedir(pDir);
	}

	/* Ordena alfabeticamente */
	if (new_list && new_count > 1)
	{
		qsort(new_list, new_count, sizeof(BgmEntry), 
		      (int (*)(const void *, const void *))_SortEntries);
	}

	if (g_BgmIndex.list)
		free(g_BgmIndex.list);

	g_BgmIndex.list = new_list;
	g_BgmIndex.list_count = new_count;

	if (new_count)
		DLog("[bgm] index built: %u tracks\n", new_count);
	else
		DLog("[bgm] index: nenhuma musica encontrada\n");
}

static int _SortEntries(const BgmEntry *a, const BgmEntry *b)
{
	return strcasecmp(a->name, b->name);
}

/* Update por-frame da musica de fundo. */
void BgmUpdate(void)
{
	BgmEntry *entry;

	if (!g_BgmIndex.list || !g_BgmIndex.list_count)
		return;

	entry = &g_BgmIndex.list[g_BgmIndex.list[0].playback.play_index];

	/* Proxima musica se a atual terminou */
	if (entry->playback.spu && entry->playback.spu->finished)
	{
		_PlayNext();
	}

	/* Fade in */
	if (entry->playback.fade_in)
	{
		entry->playback.fade_in--;
		if (entry->playback.spu)
		{
			entry->playback.spu->vol = (60 - entry->playback.fade_in) * 255 / 60;
		}
	}

	/* Fade out */
	if (entry->playback.fade_out)
	{
		entry->playback.fade_out--;
		if (entry->playback.spu)
		{
			entry->playback.spu->vol = entry->playback.fade_out * 255 / 60;
		}
	}
}

/* Para a musica com fade out. */
void BgmStop(void)
{
	BgmEntry *entry;

	if (!g_BgmIndex.list || !g_BgmIndex.list_count)
		return;

	entry = &g_BgmIndex.list[g_BgmIndex.list[0].playback.play_index];
	entry->playback.fade_out = 60;
}

/* Inicia o playback da primeira musica do index. */
void BgmStart(void)
{
	if (!g_BgmIndex.list || !g_BgmIndex.list_count)
		return;

	DLog("[bgm] start\n");
	_PlayNext();
}

/* Inicializa o sistema de BGM. */
void BgmInit(void)
{
	memset(&g_BgmIndex, 0, sizeof(BgmIndex));
	DLog("[bgm] initialized\n");
}

/* Termina o sistema de BGM. */
void BgmShutdown(void)
{
	_ClearIndex();
	DLog("[bgm] shutdown\n");
}
