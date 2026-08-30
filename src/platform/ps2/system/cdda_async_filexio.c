/*
 * AURORA_V4_12_PRIVATE_FILEXIO_CDDA_PCE_TOC2CUE_20260830
 *
 * Dedicated private fileXio RPC client for Sega/Mega CD raw CDDA.
 *
 * Do not replace this with fileXioSetBlockMode(FXIO_NOWAIT): that setting
 * belongs to PS2SDK's global client used by newlib. Aurora needs CDDA and
 * game-required DATA I/O to have independent RPC state.
 */

#define NEWLIB_PORT_AWARE
#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <fileXio.h>
#include <string.h>
#include <sys/fcntl.h>

#ifndef SIF_RPC_M_NOWAIT
#define SIF_RPC_M_NOWAIT 1
#endif

static SifRpcClientData_t s_AuroraCdFxClient __attribute__((aligned(64)));
static unsigned int s_AuroraCdFxRpcBuf[0x1300] __attribute__((aligned(64)));
static rests_pkt s_AuroraCdFxIntrData __attribute__((aligned(64)));

static int s_AuroraCdFxCompletionSema = -1;
static int s_AuroraCdFxBound;
static int s_AuroraCdFxFd = -1;
static char s_AuroraCdFxPath[512];

static volatile int s_AuroraCdFxReadPending;
static volatile int s_AuroraCdFxReadDone;
static void *s_AuroraCdFxReadBuffer;
static int s_AuroraCdFxReadSize;

static void AuroraCdFxReadIntr(void *raw)
{
    rests_pkt *rests = (rests_pkt *)UNCACHED_SEG(raw);

    if (rests->ssize)
        memcpy(rests->sbuf, rests->sbuffer, rests->ssize);
    if (rests->esize)
        memcpy(rests->ebuf, rests->ebuffer, rests->esize);

    s_AuroraCdFxReadDone = 1;
    if (s_AuroraCdFxCompletionSema >= 0)
        iSignalSema(s_AuroraCdFxCompletionSema);
}

static int AuroraCdFxBind(void)
{
    ee_sema_t sema;
    int rv;

    if (s_AuroraCdFxBound && s_AuroraCdFxClient.server)
        return 1;

    memset(&s_AuroraCdFxClient, 0, sizeof(s_AuroraCdFxClient));

    if (s_AuroraCdFxCompletionSema < 0)
    {
        memset(&sema, 0, sizeof(sema));
        sema.init_count = 0;
        sema.max_count = 1;
        s_AuroraCdFxCompletionSema = CreateSema(&sema);
        if (s_AuroraCdFxCompletionSema < 0)
            return 0;
    }

    sceSifInitRpc(0);
    do
    {
        rv = sceSifBindRpc(&s_AuroraCdFxClient, FILEXIO_IRX, 0);
        if (rv < 0)
            return 0;
        if (!s_AuroraCdFxClient.server)
            nopdelay();
    }
    while (!s_AuroraCdFxClient.server);

    s_AuroraCdFxBound = 1;
    return 1;
}

static int AuroraCdFxWaitPendingInternal(int *outBytes)
{
    int rv;

    if (!s_AuroraCdFxReadPending)
    {
        if (outBytes) *outBytes = 0;
        return 1;
    }

    if (!s_AuroraCdFxReadDone)
        WaitSema(s_AuroraCdFxCompletionSema);
    else
        (void)PollSema(s_AuroraCdFxCompletionSema);

    rv = *(volatile int *)UNCACHED_SEG(&s_AuroraCdFxRpcBuf[0]);

    if (s_AuroraCdFxReadBuffer && s_AuroraCdFxReadSize > 0)
        InvalidDCache(s_AuroraCdFxReadBuffer,
                      (unsigned char *)s_AuroraCdFxReadBuffer +
                      s_AuroraCdFxReadSize);

    s_AuroraCdFxReadPending = 0;
    s_AuroraCdFxReadDone = 0;
    s_AuroraCdFxReadBuffer = NULL;
    s_AuroraCdFxReadSize = 0;

    if (outBytes) *outBytes = rv;
    return 1;
}

static int AuroraCdFxCallSync(int command, int packetBytes, int replyBytes)
{
    int rv;

    if (!AuroraCdFxBind())
        return -1;

    (void)AuroraCdFxWaitPendingInternal(NULL);

    rv = sceSifCallRpc(&s_AuroraCdFxClient,
                       command, 0,
                       s_AuroraCdFxRpcBuf, packetBytes,
                       s_AuroraCdFxRpcBuf, replyBytes,
                       NULL, NULL);
    if (rv < 0)
        return rv;

    return *(volatile int *)UNCACHED_SEG(&s_AuroraCdFxRpcBuf[0]);
}

int AuroraCdFxOpenSeek(const char *path, long offset)
{
    struct fxio_open_packet *op;
    struct fxio_close_packet *cp;
    struct fxio_lseek_packet *sp;
    int fd;

    if (!path || !*path || strlen(path) >= sizeof(s_AuroraCdFxPath) ||
        offset < 0 || !AuroraCdFxBind())
        return 0;

    (void)AuroraCdFxWaitPendingInternal(NULL);

    if (s_AuroraCdFxFd < 0 || strcmp(s_AuroraCdFxPath, path))
    {
        if (s_AuroraCdFxFd >= 0)
        {
            cp = (struct fxio_close_packet *)s_AuroraCdFxRpcBuf;
            cp->fd = s_AuroraCdFxFd;
            (void)AuroraCdFxCallSync(
                FILEXIO_CLOSE, sizeof(*cp), sizeof(int));
            s_AuroraCdFxFd = -1;
            s_AuroraCdFxPath[0] = 0;
        }

        op = (struct fxio_open_packet *)s_AuroraCdFxRpcBuf;
        memset(op, 0, sizeof(*op));
        strncpy(op->pathname, path, sizeof(op->pathname) - 1);
        /* AURORA_V4_14_FILEXIO_OPENFLAG_PCE_STORED_PREGAPS_20260830
         * FILEXIO_OPEN consumes IOP/iomanX flags, not EE/newlib POSIX flags.
         * IOP O_RDONLY is 0x0001; EE O_RDONLY is normally 0. */
        op->flags = 0x0001;
        op->mode = 0;

        fd = AuroraCdFxCallSync(
            FILEXIO_OPEN, sizeof(*op), sizeof(int));
        if (fd < 0)
            return 0;

        s_AuroraCdFxFd = fd;
        strncpy(s_AuroraCdFxPath, path,
                sizeof(s_AuroraCdFxPath) - 1);
        s_AuroraCdFxPath[sizeof(s_AuroraCdFxPath) - 1] = 0;
    }

    sp = (struct fxio_lseek_packet *)s_AuroraCdFxRpcBuf;
    sp->fd = s_AuroraCdFxFd;
    sp->offset = (u32)offset;
    /* FILEXIO_LSEEK uses the standard SEEK_SET ABI value (0). */
    sp->whence = 0;

    return AuroraCdFxCallSync(
        FILEXIO_LSEEK, sizeof(*sp), sizeof(int)) >= 0;
}

int AuroraCdFxStartRead(void *buffer, int bytes)
{
    struct fxio_read_packet *rp;
    int rv;

    if (!buffer || bytes <= 0 || ((unsigned int)buffer & 63u) ||
        (bytes & 63) || s_AuroraCdFxFd < 0 ||
        s_AuroraCdFxReadPending || !AuroraCdFxBind())
        return 0;

    while (PollSema(s_AuroraCdFxCompletionSema) >= 0)
        ;

    rp = (struct fxio_read_packet *)s_AuroraCdFxRpcBuf;
    rp->fd = s_AuroraCdFxFd;
    rp->buffer = buffer;
    rp->size = bytes;
    rp->intrData = &s_AuroraCdFxIntrData;

    sceSifWriteBackDCache(buffer, bytes);
    sceSifWriteBackDCache(&s_AuroraCdFxIntrData,
                          sizeof(s_AuroraCdFxIntrData));

    s_AuroraCdFxReadPending = 1;
    s_AuroraCdFxReadDone = 0;
    s_AuroraCdFxReadBuffer = buffer;
    s_AuroraCdFxReadSize = bytes;

    rv = sceSifCallRpc(&s_AuroraCdFxClient,
                       FILEXIO_READ, SIF_RPC_M_NOWAIT,
                       s_AuroraCdFxRpcBuf, sizeof(*rp),
                       s_AuroraCdFxRpcBuf, sizeof(int),
                       AuroraCdFxReadIntr,
                       &s_AuroraCdFxIntrData);
    if (rv < 0)
    {
        s_AuroraCdFxReadPending = 0;
        s_AuroraCdFxReadDone = 0;
        s_AuroraCdFxReadBuffer = NULL;
        s_AuroraCdFxReadSize = 0;
        return 0;
    }

    return 1;
}

int AuroraCdFxPollRead(int *outBytes)
{
    int rv;

    if (!s_AuroraCdFxReadPending)
        return 0;
    if (!s_AuroraCdFxReadDone)
        return 0;

    (void)PollSema(s_AuroraCdFxCompletionSema);
    rv = *(volatile int *)UNCACHED_SEG(&s_AuroraCdFxRpcBuf[0]);

    if (s_AuroraCdFxReadBuffer && s_AuroraCdFxReadSize > 0)
        InvalidDCache(s_AuroraCdFxReadBuffer,
                      (unsigned char *)s_AuroraCdFxReadBuffer +
                      s_AuroraCdFxReadSize);

    s_AuroraCdFxReadPending = 0;
    s_AuroraCdFxReadDone = 0;
    s_AuroraCdFxReadBuffer = NULL;
    s_AuroraCdFxReadSize = 0;

    if (outBytes) *outBytes = rv;
    return 1;
}

int AuroraCdFxWaitRead(int *outBytes)
{
    return AuroraCdFxWaitPendingInternal(outBytes);
}

void AuroraCdFxClose(void)
{
    struct fxio_close_packet *cp;

    (void)AuroraCdFxWaitPendingInternal(NULL);

    if (s_AuroraCdFxFd >= 0)
    {
        cp = (struct fxio_close_packet *)s_AuroraCdFxRpcBuf;
        cp->fd = s_AuroraCdFxFd;
        (void)AuroraCdFxCallSync(
            FILEXIO_CLOSE, sizeof(*cp), sizeof(int));
    }

    s_AuroraCdFxFd = -1;
    s_AuroraCdFxPath[0] = 0;
}

/* AURORA_V4_12_PRIVATE_FILEXIO_CDDA_PCE_TOC2CUE_20260830 */

/* AURORA_V4_12_1_RESUME_COMPILEFIX_20260830 */

/* AURORA_V4_14_FILEXIO_OPENFLAG_PCE_STORED_PREGAPS_20260830 */

/* AURORA_V4_14_1_RESUME_COMPILEFIX_20260830 */
