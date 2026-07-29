/* mainloop_bgm.cpp
 *
 * Trilha sonora de fundo do menu (.mod / .xm) via jar_mod / jar_xm.
 *
 * Arquitetura (igual ao resto do projeto): sem thread.  A cada frame de
 * menu, MainLoopRender chama BgmUpdate(), que gera PCM na EE com o
 * player de tracker e empurra para o audsrv via Aud_Enqueue().  Durante
 * o menu o core do SNES/NES nao roda, entao o BGM e' o unico produtor
 * de audio -- nao briga com o AudMixBuffer do jogo.
 *
 * Descoberta de arquivo: procura todas as faixas .mod/.xm em BGM_PATH
 * (define do Makefile) e em pastas padrao, indexa-as e toca como uma
 * playlist: ao terminar uma faixa avanca para a proxima (e ao sair de uma
 * ROM, via BgmNext, tambem avanca para dar variedade).
 *
 * Players de terceiros:
 *   jar_mod.h  - dominio publico (unlicense), Joshua Reisenauer/mackron
 *   jar_xm.h   - WTFPL, Joshua Reisenauer / Romain Dalmaso
 * As unidades de implementacao ficam em src/third_party/jar/jar_*.c.
 * Aqui incluimos APENAS jar_mod.h (header limpo, so declaracoes); para
 * o XM forward-declaramos as funcoes que usamos, porque a parte publica
 * do jar_xm.h define corpos de funcao (geraria simbolo duplicado se
 * incluido em mais de um .o).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libcdvd.h>

#include "types.h"

extern "C" {
#include "audio.h"
}

/* jar_mod.h: header limpo, da' a struct de contexto e as declaracoes. */
#include "jar_mod.h"

/* jar_xm: tipo opaco + so as funcoes que usamos (ver comentario acima). */
extern "C" {
    struct jar_xm_context_s;
    typedef struct jar_xm_context_s jar_xm_context_t;
    int  jar_xm_create_context_safe(jar_xm_context_t **ctx, const char *moddata,
                                    size_t moddata_length, unsigned int rate);
    void jar_xm_free_context(jar_xm_context_t *ctx);
    void jar_xm_generate_samples(jar_xm_context_t *ctx, float *output,
                                 size_t numsamples);
    void jar_xm_generate_samples_16bit(jar_xm_context_t *ctx, short *output,
                                       size_t numsamples);
    void jar_xm_set_max_loop_count(jar_xm_context_t *ctx, unsigned char loopcnt);
    /* numero de vezes que o modulo ja' deu a volta inteira (loop).  Usado
       para detectar fim de faixa e avancar para a proxima (playlist). */
    unsigned char jar_xm_get_loop_count(jar_xm_context_t *ctx);
    /* liga/desliga interpolacao linear.  Desligada (nearest) e' mais barata
       por sample -- usada no PS2 p/ segurar modulos de muitos canais (32ch)
       em tempo real. */
    void jar_xm_set_linear_interpolation(jar_xm_context_t *ctx, int enable);
}

#include "mainloop_bgm.h"


/* Diagnostico de boot/menu: DLog escreve no EE SIO (visivel no log do
   NetherSX2/PCSX2), definido em modules/sjpcm/sjpcm_rpc.c. */
extern "C" void DLog(const char *fmt, ...);


/* ---- configuracao ---------------------------------------------------- */

/* Taxa de SINTESE do tracker.  A saida do audsrv e' fixa em 48 kHz; o PCM
   gerado a BGM_RATE e' reamostrado (linear) para 48 kHz em BgmUpdate.  O
   custo de CPU da sintese e' ~proporcional ao numero de AMOSTRAS/frame
   (logo, a' BGM_RATE).  Tipicos: 24000 (leve, garante 60fps), 32000
   (meio-termo, padrao), 48000 (nativo, mais pesado).  Sobrescrevivel pelo
   Makefile:  make BGM_RATE=24000 */
#ifndef BGM_RATE
#define BGM_RATE        24000
#endif

/* Teto de frames de SAIDA (48 kHz) por chamada.  Em regime normal so
   geramos ~800; o teto limita o PICO de sintese (nos frames de recarga do
   ring logo apos um bloqueio de disco/decode) a ~(BGM_OUT_CHUNK*BGM_RATE/
   48000) amostras -- com 3072 e BGM_RATE<=48000 fica <=2048@32k, o mesmo
   pico que segurava 60fps, recarregando o ring (~107ms) em ~3 frames. */
#define BGM_OUT_CHUNK   3072

/* Teto de frames de SAIDA (48 kHz) sintetizados POR CHAMADA de BgmUpdate.
   Em regime normal so' geramos ~800 (um frame @60fps), mas quando o ring
   drena (ex.: bloqueio de disco ao trocar de faixa) avail pode chegar a
   milhares -- sintetizar tudo de uma vez num unico frame faz um PICO de
   CPU que estoura os 16ms (pior com XM de 32 canais) e causa o engasgo.
   Limitar aqui espalha a recarga por varios frames: > 800 para nao ficar
   pra tras do consumo, mas baixo o bastante pra nao dar pico.  ~1200
   permite ~400 frames/recarga sem estourar o orcamento de CPU. */
#define BGM_MAX_OUT_PER_FRAME 1200

/* Duracao (em frames de menu @60fps) do "respiro" de silencio inserido
   ENTRE faixas, ao trocar.  Cobre o bloqueio de disco do carregamento da
   proxima faixa (o ring toca silencio limpo durante a leitura, em vez de
   underrun/estralo) e da' uma pausa agradavel tipo playlist.  ~12 = 200ms. */
#define BGM_GAP_FRAMES 12

/* Limiar (em frames stereo) abaixo do qual consideramos que a cauda de
   audio do jogo ja' drenou do ring do audsrv.  Ao abrir o menu, o
   _MenuEnable muta o audsrv; esperamos o ring cair abaixo disso antes de
   soltar o tracker, para nao ouvir SNES/NES junto com a trilha. ~5ms. */
#define BGM_DRAIN_THRESH 256

/* Maximo de frames esperando a cauda do jogo drenar antes de soltar o
   tracker.  No boot o audsrv_queued() reporta uma ocupacao inicial
   "fantasma" que nunca drena; sem este timeout a musica so' comecava
   depois de entrar num jogo e voltar.  ~12 frames (~200ms) cobrem a cauda
   real (~107ms) e destravam o caso do boot. */
#define BGM_DRAIN_MAXFRAMES 12

/* O cdfs.irx chama sceCdDiskReady(0) internamente ao abrir a primeira
   pasta. Esse modo e' BLOQUEANTE e foi a causa da Issue #16 quando o
   BGM tentou abrir cdfs:/BGM logo no primeiro frame do menu. A sondagem
   abaixo e' feita aos poucos, com mode=1 (nao bloqueante), antes de
   permitir qualquer opendir no disco.

   - grace: deixa BIOS/mechacon/OPL assentarem depois do SifIopReset;
   - poll: evita um RPC de CDVD em todo frame;
   - stable: exige duas respostas prontas consecutivas;
   - giveup: em ELF sem disco, para de mostrar "Searching" apos ~15 s. */
#define BGM_DISC_GRACE_FRAMES    90
#define BGM_DISC_POLL_FRAMES     15
#define BGM_DISC_STABLE_POLLS     2
#define BGM_DISC_GIVEUP_FRAMES  900

/* O Makefile preserva subpastas da origem ao montar BGM/ na ISO. Escanear
   alguns niveis garante que essas faixas tambem sejam encontradas, sem
   permitir recursao ilimitada em dispositivos arbitrarios. */
#define BGM_SCAN_MAX_DEPTH 4

/* Pastas tentadas, em ordem.  BGM_PATH (se definido pelo Makefile) vem
   primeiro.  A primeira faixa .mod/.xm encontrada e' tocada. */
static const char *s_dirs[] = {
#ifdef BGM_PATH
    BGM_PATH,
#endif
    "mc0:/SNESticle/bgm",
    "mc1:/SNESticle/bgm",
    "mass:/SNESticle/bgm",
    "mass:/bgm",
    "cdfs:/BGM",
};
#define BGM_NUM_DIRS (sizeof(s_dirs) / sizeof(s_dirs[0]))


/* ---- estado ---------------------------------------------------------- */

enum BgmStateE {
    BGM_UNTRIED = 0,   /* ainda nao tentou carregar (lazy load)           */
    BGM_MOD,           /* tocando um .mod                                 */
    BGM_XM,            /* tocando um .xm                                  */
    BGM_FAILED         /* nenhuma faixa / falha -- nao tenta de novo      */
};

static int  s_state    = BGM_UNTRIED;
static int  s_volume   = 100;          /* 0 = off; 1..100 (Video Config)  */
static int  s_rate     = BGM_RATE;     /* taxa de sintese (Hz), Video Config */
static Bool s_volSet   = FALSE;        /* ja' firmamos o volume p/ tocar? */
static int  s_drainWait = 0;           /* frames esperando dreno da cauda */
static int  s_gapFrames = 0;           /* frames de silencio na troca de faixa */

/* Frequencias de sintese oferecidas no Video Config (Hz).  Mais alta =
   melhor qualidade e mais CPU (48000 pode derrubar o fps).  24000 e' o
   padrao seguro. A saida e' sempre reamostrada para 48 kHz. */
static const int s_rateList[] = { 16000, 22050, 24000, 32000, 38000, 44100, 48000 };
#define BGM_RATE_COUNT ((int)(sizeof(s_rateList) / sizeof(s_rateList[0])))

static jar_mod_context_t  s_mod;       /* contexto do tocador de MOD      */
static jar_xm_context_t  *s_xm  = NULL;/* contexto do tocador de XM       */
static char              *s_xmBuf = NULL; /* buffer do arquivo .xm (vivo) */

/* Indice (cache) de TODAS as faixas .mod/.xm achadas -- escaneado UMA vez
   (sem reler o disco toda hora).  s_trackIdx aponta a faixa atual; e'
   sorteada no boot para dar variedade sem custo de reload (trocar de
   faixa releria do disco, lento no memory card -> traria a travadinha). */
#define BGM_INDEX_MAX 64
typedef struct { char path[256]; int kind; } BgmTrackT; /* kind 1=mod 2=xm */
static BgmTrackT s_index[BGM_INDEX_MAX];
static int       s_indexCount = -1;    /* -1 = ainda nao escaneado */
static int       s_trackIdx   = 0;     /* faixa atual no indice    */

enum BgmDiscScanE {
    BGM_DISC_PENDING = 0,
    BGM_DISC_DONE
};
static int          s_discScanState   = BGM_DISC_PENDING;
static unsigned int s_discScanFrames  = 0;
static int          s_discStablePolls = 0;
static char         s_bootBgm[256];

/* buffers de geracao (estaticos: evitam pressao de pilha na EE) */
static short s_inter[BGM_OUT_CHUNK * 2] __attribute__((aligned(64))); /* 48k L,R,L,R */
static float s_xmf  [BGM_OUT_CHUNK * 2] __attribute__((aligned(64))); /* scratch XM float */
static short s_left [BGM_OUT_CHUNK]     __attribute__((aligned(64)));
static short s_right[BGM_OUT_CHUNK]     __attribute__((aligned(64)));


/* ---- utilitarios ----------------------------------------------------- */

static Bool _HasExt(const char *name, const char *ext)
{
    size_t ln = strlen(name);
    size_t le = strlen(ext);
    size_t i;
    if (ln < le) return FALSE;
    for (i = 0; i < le; i++)
    {
        char a = name[ln - le + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32); /* lower */
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return FALSE;
    }
    return TRUE;
}

/* Escaneia as pastas candidatas UMA vez e indexa todas as faixas
   .mod/.xm achadas (so' os nomes/caminhos -- barato).  Sorteia uma faixa
   inicial (variedade por boot, sem custo de reload). */
/* MainGetBootDir(): pasta de onde o ELF foi carregado (definida em
   main.cpp com linkage C++).  Declarada SEM extern "C" para casar com a
   definicao (igual mainloop_init.cpp); usada para achar a pasta "bgm" AO
   LADO do ELF. */
char *MainGetBootDir();

/* Um caminho aponta pro drive de CD/DVD? */
static Bool _IsDiscPath(const char *p)
{
    if (!p) return FALSE;
    return (strncmp(p, "cdfs",  4) == 0 ||
            strncmp(p, "cdrom", 5) == 0) ? TRUE : FALSE;
}

/* Tipos que o cdfs.irx do PS2SDK reconhece como discos com filesystem. */
static Bool _DiscTypeHasFilesystem(int type)
{
    switch (type)
    {
        case SCECdPSCD:
        case SCECdPSCDDA:
        case SCECdPS2CD:
        case SCECdPS2CDDA:
        case SCECdPS2DVD:
        case SCECdDVDV:
            return TRUE;
        default:
            return FALSE;
    }
}

static void _BuildBootBgm(void)
{
    const char *bd = MainGetBootDir();
    int n = 0;

    s_bootBgm[0] = 0;
    if (!bd || !bd[0]) return;

    while (bd[n] && n < (int)sizeof(s_bootBgm) - 6)
    {
        s_bootBgm[n] = (bd[n] == '\\') ? '/' : bd[n];
        n++;
    }
    s_bootBgm[n] = 0;

    /* argv[0] de um ELF na ISO normalmente vem como cdrom0:/...; toda a
       pilha moderna deste projeto usa cdfs:. Converta antes de guardar o
       caminho ao lado do ELF. */
    if (strncmp(s_bootBgm, "cdrom", 5) == 0)
    {
        char converted[sizeof(s_bootBgm)];
        const char *colon = strchr(s_bootBgm, ':');
        const char *tail = colon ? colon + 1 : "";
        snprintf(converted, sizeof(converted), "cdfs:%s", tail);
        strncpy(s_bootBgm, converted, sizeof(s_bootBgm) - 1);
        s_bootBgm[sizeof(s_bootBgm) - 1] = 0;
        n = (int)strlen(s_bootBgm);
    }

    if (n > 0 && s_bootBgm[n - 1] != '/') s_bootBgm[n++] = '/';
    s_bootBgm[n] = 0;
    strncat(s_bootBgm, "bgm",
            sizeof(s_bootBgm) - strlen(s_bootBgm) - 1);
}

static Bool _IndexHasPath(const char *path)
{
    int i;

    for (i = 0; i < s_indexCount; i++)
    {
        const char *a = s_index[i].path;
        const char *b = path;

        if (_IsDiscPath(a) && _IsDiscPath(b))
        {
            while (*a && *b)
            {
                char ca = (*a == '\\') ? '/' : *a;
                char cb = (*b == '\\') ? '/' : *b;
                if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
                if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
                if (ca != cb) break;
                a++;
                b++;
            }
            if (!*a && !*b) return TRUE;
        }
        else if (strcmp(a, b) == 0)
            return TRUE;
    }
    return FALSE;
}

static void _IndexAdd(const char *path, int kind)
{
    size_t len;

    if (!path || !path[0] || !kind ||
        s_indexCount >= BGM_INDEX_MAX || _IndexHasPath(path))
        return;

    len = strlen(path);
    if (len >= sizeof(s_index[s_indexCount].path))
    {
        DLog("[bgm] skip overlong path (%u byte(s))", (unsigned int)len);
        return;
    }
    memcpy(s_index[s_indexCount].path, path, len + 1);
    s_index[s_indexCount].kind = kind;
    s_indexCount++;
}

static void _IndexRemove(int index)
{
    int i;

    if (index < 0 || index >= s_indexCount) return;
    for (i = index; i + 1 < s_indexCount; i++)
        s_index[i] = s_index[i + 1];
    s_indexCount--;

    if (s_indexCount <= 0)
        s_trackIdx = 0;
    else if (s_trackIdx >= s_indexCount)
        s_trackIdx = 0;
}

/* Retorna 1 se a pasta abriu. A recursao e' limitada e so faz stat()
   quando d_type nao informa se a entrada e' diretorio (caso do cdfs). */
static int _ScanDir(const char *scanDir, int depth)
{
    DIR *pDir;
    struct dirent *pEnt;

    DLog("[bgm] scan opendir('%s')...", scanDir);
    pDir = opendir(scanDir);
    DLog("[bgm] scan opendir('%s') -> %p", scanDir, (void *)pDir);
    if (!pDir) return 0;

    while ((pEnt = readdir(pDir)) != NULL &&
           s_indexCount < BGM_INDEX_MAX)
    {
        char child[256];
        const char *sep;
        int kind = 0;
        int written;

        if (!strcmp(pEnt->d_name, ".") || !strcmp(pEnt->d_name, ".."))
            continue;

        sep = (scanDir[0] &&
               (scanDir[strlen(scanDir) - 1] == '/' ||
                scanDir[strlen(scanDir) - 1] == '\\')) ? "" : "/";
        written = snprintf(child, sizeof(child), "%s%s%s",
                           scanDir, sep, pEnt->d_name);
        if (written < 0 || written >= (int)sizeof(child))
            continue;

        if (_HasExt(pEnt->d_name, ".mod")) kind = 1;
        else if (_HasExt(pEnt->d_name, ".xm")) kind = 2;
        if (kind)
        {
            _IndexAdd(child, kind);
            continue;
        }

        if (depth < BGM_SCAN_MAX_DEPTH)
        {
            Bool isDir = FALSE;
            Bool typeKnown = FALSE;
#ifdef DT_DIR
            if (pEnt->d_type == DT_DIR)
            {
                isDir = TRUE;
                typeKnown = TRUE;
            }
            else if (pEnt->d_type == DT_REG)
            {
                typeKnown = TRUE;
            }
#endif
            if (!typeKnown)
            {
                struct stat st;
                if (stat(child, &st) == 0)
                    isDir = S_ISDIR(st.st_mode) ? TRUE : FALSE;
            }
            if (isDir)
                _ScanDir(child, depth + 1);
        }
    }
    closedir(pDir);
    return 1;
}

static void _BuildIndex(void)
{
    size_t d;

    s_indexCount = 0;
    s_discScanState = BGM_DISC_PENDING;
    s_discScanFrames = 0;
    s_discStablePolls = 0;
    _BuildBootBgm();

    /* Primeiro escaneia somente caminhos que nunca tocam o CD/DVD. */
    if (s_bootBgm[0] && !_IsDiscPath(s_bootBgm))
        _ScanDir(s_bootBgm, 0);

    for (d = 0; d < BGM_NUM_DIRS && s_indexCount < BGM_INDEX_MAX; d++)
        if (!_IsDiscPath(s_dirs[d]))
            _ScanDir(s_dirs[d], 0);

    /* faixa inicial pseudo-aleatoria (clock varia conforme o tempo de
       boot); se nao houver entropia, cai no indice 0 -- sem problema. */
    if (s_indexCount > 0)
    {
        unsigned int seed = (unsigned int)clock();
        s_trackIdx = (int)(seed % (unsigned int)s_indexCount);
    }
    DLog("[bgm] local index built: %d track(s), disc pending", s_indexCount);
}

/* Um passo curto por frame. Nenhum loop de espera e nenhum acesso a cdfs:
   acontece antes de o mechacon responder "pronto" duas vezes. O opendir
   posterior ainda e' sincrono, mas nessa altura o cdfs.irx nao entra no
   sceCdDiskReady(0) enquanto o drive esta detectando. */
static void _DiscScanStep(void)
{
    int type;
    int ready;
    int before;
    size_t d;
    DIR *root;

    if (s_discScanState == BGM_DISC_DONE) return;

    s_discScanFrames++;
    if (s_discScanFrames < BGM_DISC_GRACE_FRAMES) return;
    if (((s_discScanFrames - BGM_DISC_GRACE_FRAMES) %
         BGM_DISC_POLL_FRAMES) != 0) return;

    type = sceCdGetDiskType();
    if (!_DiscTypeHasFilesystem(type))
    {
        s_discStablePolls = 0;
        if (s_discScanFrames >= BGM_DISC_GIVEUP_FRAMES)
        {
            s_discScanState = BGM_DISC_DONE;
            DLog("[bgm] disc scan timeout (type=%d)", type);
        }
        return;
    }

    /* mode=1: consulta e volta; nunca espera o drive terminar de girar. */
    ready = sceCdDiskReady(1);
    if (ready != SCECdComplete)
    {
        s_discStablePolls = 0;
        return;
    }

    if (++s_discStablePolls < BGM_DISC_STABLE_POLLS) return;

    /* Confirma que o device cdfs: realmente responde. Se o RPC ainda nao
       estiver pronto, mantem PENDING e tenta novamente em outro frame. */
    root = opendir("cdfs:/");
    if (!root)
    {
        DLog("[bgm] disc ready, but cdfs root is not mounted yet");
        s_discStablePolls = 0;
        return;
    }
    closedir(root);

    before = s_indexCount;
    if (s_bootBgm[0] && _IsDiscPath(s_bootBgm))
        _ScanDir(s_bootBgm, 0);

    for (d = 0; d < BGM_NUM_DIRS && s_indexCount < BGM_INDEX_MAX; d++)
        if (_IsDiscPath(s_dirs[d]))
            _ScanDir(s_dirs[d], 0);

    s_discScanState = BGM_DISC_DONE;
    if (before == 0 && s_indexCount > 0)
    {
        unsigned int seed = (unsigned int)clock();
        s_trackIdx = (int)(seed % (unsigned int)s_indexCount);
        if (s_state == BGM_FAILED)
            s_state = BGM_UNTRIED;
    }
    DLog("[bgm] disc index complete: +%d, total=%d",
         s_indexCount - before, s_indexCount);
}

/* Le um arquivo inteiro para um buffer malloc'd.  Retorna NULL em erro. */
static char *_LoadFileAlloc(const char *path, long *outLen)
{
    FILE *f;
    long  len;
    char *buf;
    size_t rd;

    f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }

    buf = (char *)malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }

    rd = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if ((long)rd != len) { free(buf); return NULL; }

    *outLen = len;
    return buf;
}


/* ---- carga (lazy) ---------------------------------------------------- */

static void _TryLoad(void)
{
    if (s_indexCount < 0) _BuildIndex();
    if (s_indexCount <= 0)
    {
        s_state = BGM_FAILED;
        return;
    }

    /* Uma faixa truncada ou incompatível nao deve derrubar a playlist
       inteira. Tenta somente uma entrada por frame: se ela falhar, remove
       do indice e deixa a seguinte para o proximo BgmUpdate, evitando um
       congelamento longo quando ha varios arquivos ruins. */
    {
        const char *path;
        int kind;

        if (s_trackIdx < 0 || s_trackIdx >= s_indexCount) s_trackIdx = 0;
        path = s_index[s_trackIdx].path;
        kind = s_index[s_trackIdx].kind;
        DLog("[bgm] load track[%d] kind=%d '%s'",
             s_trackIdx, kind, path);

        if (kind == 1) /* MOD */
        {
            jar_mod_init(&s_mod);             /* defaults: 48000/16/stereo */
            jar_mod_setcfg(&s_mod, s_rate, 16, 1, 1, 1);
            if (jar_mod_load_file(&s_mod, path) != 0)
            {
                s_state = BGM_MOD;            /* jar_mod faz loop sozinho */
                return;
            }
            jar_mod_unload(&s_mod);
        }
        else /* XM */
        {
            long len = 0;
            s_xm = NULL;
            s_xmBuf = _LoadFileAlloc(path, &len);
            if (s_xmBuf &&
                jar_xm_create_context_safe(&s_xm, s_xmBuf, (size_t)len,
                                           s_rate) == 0)
            {
                jar_xm_set_max_loop_count(s_xm, 0); /* loop infinito */
                /* Interpolacao linear LIGADA (default): som mais limpo.
                   Se algum modulo de muitos canais engasgar, baixe a
                   taxa no Video Config. */
                s_state = BGM_XM;
                return;
            }
            if (s_xm) { jar_xm_free_context(s_xm); s_xm = NULL; }
            if (s_xmBuf) { free(s_xmBuf); s_xmBuf = NULL; }
        }

        DLog("[bgm] rejected unreadable/invalid track '%s'", path);
        _IndexRemove(s_trackIdx);
    }

    s_state = (s_indexCount > 0) ? BGM_UNTRIED : BGM_FAILED;
}


/* ---- API ------------------------------------------------------------- */

/* Libera APENAS o decoder e o buffer do arquivo, deixando o estado pronto
   para o proximo BgmUpdate recarregar (s_state = UNTRIED).  NAO mexe na
   logica de volume/dreno da sessao de menu (s_volSet/s_drainWait) -- assim
   o auto-advance (trocar de faixa no meio do menu) toca a proxima na hora,
   sem re-esperar o dreno da cauda do jogo. */
static void _BgmFreeDecoder(void)
{
    if (s_state == BGM_MOD) jar_mod_unload(&s_mod);
    if (s_state == BGM_XM && s_xm) { jar_xm_free_context(s_xm); s_xm = NULL; }
    if (s_xmBuf) { free(s_xmBuf); s_xmBuf = NULL; }
    s_state  = BGM_UNTRIED;
}

/* Libera o decoder e o buffer do arquivo E re-arma a logica de
   volume/dreno.  Chamado quando a trilha e' desligada (BgmSetVolume(0)) ou
   ao abrir o menu (BgmNext): nesses casos queremos esperar a cauda de
   audio do jogo drenar antes de soltar o tracker de novo. */
static void _BgmFree(void)
{
    _BgmFreeDecoder();
    s_volSet = FALSE;
    s_drainWait = 0;
    s_gapFrames = 0;
}

/* Avanca para a proxima faixa do indice (sequencial, circular) e libera o
   decoder atual SEM re-armar o dreno -- usado pelo auto-advance quando a
   faixa atual termina uma passada inteira.  Retorna TRUE se trocou; com
   0/1 faixa nao ha "outra": retorna FALSE (o chamador deixa a faixa unica
   seguir em loop normal, sem reload nem hitch). */
static Bool _BgmAdvance(void)
{
    if (s_indexCount <= 1) return FALSE;
    s_trackIdx = (s_trackIdx + 1) % s_indexCount;
    _BgmFreeDecoder();   /* mantem s_volSet: proxima faixa toca na hora */
    return TRUE;
}

void BgmStop(void)
{
    /* Para de alimentar SEM liberar o decoder: a faixa fica carregada,
       entao reabrir o menu e' instantaneo (sem reler do disco -> sem a
       travadinha).  So' re-arma a logica de volume/dreno para a proxima
       entrada no menu (esperar a cauda de audio do jogo drenar antes de
       soltar o tracker).  A liberacao real acontece em BgmSetVolume(0). */
    s_volSet = FALSE;
    s_drainWait = 0;
    s_gapFrames = 0;
}

void BgmNext(void)
{
    /* Avanca para a proxima faixa do indice e libera o decoder atual, de
       modo que o proximo BgmUpdate carregue a nova faixa.  Chamado ao
       ABRIR o menu (sair do jogo) para dar variedade.  Releria do disco
       (pode dar um hitch breve no memory card), por isso so' troca quando
       ha 2+ faixas; com 0/1 faixa nao faz nada (sem reload, sem hitch). */
    if (s_indexCount < 0) _BuildIndex();
    if (s_indexCount <= 1) return;

    s_trackIdx = (s_trackIdx + 1) % s_indexCount;

    if (s_state == BGM_MOD || s_state == BGM_XM) _BgmFree();
}

void BgmSetVolume(int vol)
{
    if (vol < 0)   vol = 0;
    if (vol > 100) vol = 100;

    if (vol == 0)
    {
        /* OFF: libera o decoder/buffer (nao consome RAM) e silencia o que
           ainda estiver no ring. */
        _BgmFree();
        if (Aud_IsInitialized()) Aud_Setvol(0);
    }
    else if (s_volSet && Aud_IsInitialized())
    {
        /* ja' tocando: ajusta o volume ao vivo */
        Aud_Setvol((unsigned int)(vol * 0x3FFF / 100));
    }

    s_volume = vol;
}

int BgmGetVolume(void)
{
    return s_volume;
}

int BgmTrackCount(void)
{
    /* O indice local nasce imediatamente; o pedaço cdfs e' acrescentado
       depois, sem bloquear o primeiro frame do menu. */
    if (s_indexCount < 0) _BuildIndex();
    return s_indexCount;
}

int BgmIsSearching(void)
{
    if (s_indexCount < 0) _BuildIndex();
    return s_discScanState == BGM_DISC_PENDING ? 1 : 0;
}

int BgmGetRate(void)
{
    return s_rate;
}

void BgmSetRate(int hz)
{
    if (hz < 8000)  hz = 8000;
    if (hz > 48000) hz = 48000;
    if (hz == s_rate) return;
    s_rate = hz;
    /* recarrega o decoder na nova taxa: _BgmFree zera o estado e o proximo
       BgmUpdate recarrega em s_rate. */
    if (s_state == BGM_MOD || s_state == BGM_XM) _BgmFree();
}

void BgmCycleRate(int dir)
{
    int i, idx = 3; /* fallback ~32000 */
    for (i = 0; i < BGM_RATE_COUNT; i++)
        if (s_rateList[i] == s_rate) { idx = i; break; }
    idx += (dir < 0) ? -1 : 1;
    if (idx < 0)               idx = BGM_RATE_COUNT - 1;
    if (idx >= BGM_RATE_COUNT)  idx = 0;
    BgmSetRate(s_rateList[idx]);
}

void BgmUpdate(void)
{
    int avail, n, synthN, j;

    static Bool s_logged = FALSE;
    if (!s_logged) { DLog("[bgm] BgmUpdate first call: vol=%d", s_volume); s_logged = TRUE; }

    if (s_volume <= 0)         return;   /* OFF: nem toca o drive */
    if (s_indexCount < 0)      _BuildIndex();
    _DiscScanStep();
    if (!Aud_IsInitialized())  return;

    /* Respiro entre faixas: apos detectar o fim e avancar (decoder ja'
       liberado), tocamos alguns frames de SILENCIO antes de carregar a
       proxima.  Enche o ring de zeros -- quando o _TryLoad bloquear a EE
       lendo o arquivo do disco, o audsrv toca silencio limpo (sem
       underrun/estralo) em vez de repetir lixo do ring.  Da' tambem uma
       pausa curta tipo playlist entre as musicas. */
    if (s_gapFrames > 0)
    {
        int g = Aud_Available();
        s_gapFrames--;
        if (g > 0)
        {
            if (g > BGM_OUT_CHUNK) g = BGM_OUT_CHUNK;
            memset(s_left,  0, (size_t)g * sizeof(s_left[0]));
            memset(s_right, 0, (size_t)g * sizeof(s_right[0]));
            Aud_Enqueue(s_left, s_right, g, 0);
        }
        return;
    }

    if (s_state == BGM_UNTRIED) _TryLoad();
    if (s_state != BGM_MOD && s_state != BGM_XM) return; /* FAILED/nada */

    /* Espera a cauda de audio do jogo (mutada pelo _MenuEnable ao abrir o
       menu) drenar do ring ANTES de soltar o tracker: evita ouvir SNES/NES
       junto com a trilha, e da' um inicio limpo (nao instantaneo).  So'
       enquanto ainda nao firmamos o volume desta sessao de menu. */
    if (!s_volSet)
    {
        /* Espera a cauda do jogo drenar, mas com TIMEOUT: no boot o
           audsrv_queued() reporta uma ocupacao inicial "fantasma" que
           nunca drena -- sem o timeout a musica so' comecava depois de
           entrar num jogo e voltar.  O timeout cobre a cauda real (~107ms)
           e destrava o caso do boot. */
        if (Aud_Buffered() > BGM_DRAIN_THRESH && s_drainWait < BGM_DRAIN_MAXFRAMES)
        {
            s_drainWait++;
            return;
        }
        s_drainWait = 0;
        Aud_Setvol((unsigned int)(s_volume * 0x3FFF / 100)); /* volume do menu */
        s_volSet = TRUE;
    }

    /* frames de SAIDA (48 kHz) que cabem no ring do audsrv agora */
    avail = Aud_Available();
    if (avail <= 0) return;
    n = avail;
    if (n > BGM_MAX_OUT_PER_FRAME) n = BGM_MAX_OUT_PER_FRAME; /* anti-pico (bug stutter) */
    if (n > BGM_OUT_CHUNK - 2) n = BGM_OUT_CHUNK - 2;
    if (n < 1) return;

    /* frames a sintetizar na taxa s_rate p/ render n frames @48k (+guarda) */
    synthN = (int)(((unsigned int)n * (unsigned int)s_rate) / 48000u) + 2;
    if (synthN > BGM_OUT_CHUNK) synthN = BGM_OUT_CHUNK;
    if (synthN < 2) synthN = 2;

    /* gera synthN frames interleaved (L,R,...) a s_rate em s_inter */
    if (s_state == BGM_MOD)
    {
        jar_mod_fillbuffer(&s_mod, s_inter, (unsigned long)synthN, NULL);
    }
    else
    {
        /* float -> int16 na mao: evita o malloc/free por chamada que
           jar_xm_generate_samples_16bit faz internamente. */
        int k;
        jar_xm_generate_samples(s_xm, s_xmf, (size_t)synthN);
        for (k = 0; k < synthN * 2; k++)
        {
            float f = s_xmf[k] * 32767.0f;
            if (f >  32767.0f) f =  32767.0f;
            if (f < -32768.0f) f = -32768.0f;
            s_inter[k] = (short)f;
        }
    }

    /* Auto-advance (playlist): detecta o fim da faixa (o player completou
       uma passada e voltou ao inicio) ANTES de enfileirar.  O buffer recem
       sintetizado ja' pode conter o RECOMECO do loop -- enfileira-lo daria
       os "estralos"/fragmentos.  Entao, se terminou, DESCARTA este buffer
       e avanca: o ponto de loop e' o fim musical da faixa, cortar ali e' o
       certo.  A cauda ja' no ring do audsrv cobre a troca enquanto a nova
       faixa carrega.  Com 0/1 faixa _BgmAdvance retorna FALSE: caimos no
       enqueue normal e a faixa unica segue em loop (sem descartar -> sem
       silenciar). */
    {
        Bool ended = FALSE;
        if      (s_state == BGM_MOD)        ended = (s_mod.loopcount > 0);
        else if (s_state == BGM_XM && s_xm) ended = (jar_xm_get_loop_count(s_xm) > 0);
        if (ended && _BgmAdvance()) { s_gapFrames = BGM_GAP_FRAMES; return; }  /* respiro + carrega a proxima */
    }

    /* reamostra s_rate -> 48 kHz (linear, ponto fixo 16.16) e
       desinterleava para L/R (Aud_Enqueue reinterleava). */
    {
        unsigned int step = (unsigned int)(((unsigned int)s_rate << 16) / 48000u);
        unsigned int pos  = 0;
        for (j = 0; j < n; j++)
        {
            unsigned int i  = pos >> 16;
            unsigned int fr = pos & 0xFFFF;
            unsigned int i1 = i + 1;
            int l0, l1, r0, r1;

            if (i1 >= (unsigned int)synthN) i1 = (unsigned int)(synthN - 1);

            l0 = s_inter[i * 2 + 0]; l1 = s_inter[i1 * 2 + 0];
            r0 = s_inter[i * 2 + 1]; r1 = s_inter[i1 * 2 + 1];
            s_left [j] = (short)(l0 + (int)(((long long)(l1 - l0) * fr) >> 16));
            s_right[j] = (short)(r0 + (int)(((long long)(r1 - r0) * fr) >> 16));
            pos += step;
        }
    }

    /* garante volume audivel: o menu de pausa muta o audsrv (Aud_Setvol(0))
       para matar o rabo de audio do jogo; o volume cheio ja' foi firmado
       acima, apos a cauda do jogo drenar. */

    Aud_Enqueue(s_left, s_right, n, 0); /* wait=0: best-effort, nao trava */
}
