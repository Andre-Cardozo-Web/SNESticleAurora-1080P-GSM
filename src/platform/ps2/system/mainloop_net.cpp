#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "types.h"
#include "console.h"
#include "file.h"
#include "mainloop_debug.h"
#include "mainloop_iop.h"
#include "mainloop_load.h"
#include "mainloop_net.h"
#include "mainloop_shared.h"
#include "mainloop_ui.h"
#include "mainloop.h"
#include "embedded_irx.h"

extern "C" {
#include <ps2ips.h>
extern "C" int ps2ipc_ps2ip_setconfig(const t_ip_info *ip_info);
extern "C" int ps2ipc_ps2ip_getconfig(char *netif_name, t_ip_info *ip_info);

static char s_netdiag[48] = "?";

extern "C" const char *AuroraNetGetConfigDiag(void)
{
    return s_netdiag;
}



#include "netplay_ee.h"
}

/* MAINLOOP_NETPORT lives in mainloop_shared.h (included above). */

int _MainLoopNetworkEvent(Uint32 Type, Uint32 Parm1, void *Parm2)
{
    NetPlayRPCStatusT status;
	switch (Type)
	{
		case 1:
            printf("Connecting to %08X\n", Parm1);
            NetPlayClientConnect(Parm1, MAINLOOP_NETPORT);
			break;
		case 2:
            NetPlayGetStatus(&status);
            if (status.eServerStatus == NETPLAY_STATUS_IDLE)
            {
               NetPlayServerStart(MAINLOOP_NETPORT, Parm1);
               NetPlayClientConnect(0x0100007F, MAINLOOP_NETPORT);
           }
           else
           NetPlayServerStop();
			break;
		case 3:
            NetPlayGetStatus(&status);
            if (status.eClientStatus == NETPLAY_STATUS_IDLE)
            {
				return 1;
            } else
            {
                NetPlayClientDisconnect();
				return 0;
            }
			break;
	}

	return 0;
}

void *_MainLoopNetCallback(NetPlayCallbackE eCallback, char *data, int size)
{
    switch (eCallback)
    {
        case NETPLAY_CALLBACK_NONE:
            break;

        case NETPLAY_CALLBACK_CONNECTED:
            printf("NetClientEE: Connected\n");
            break;

        case NETPLAY_CALLBACK_DISCONNECTED:
            printf("NetClientEE: Disconnected\n");
            break;

        case NETPLAY_CALLBACK_LOADGAME:
            {
                Bool result = FALSE;

                printf("NetClientEE: Loading the netgame %s\n", data);
                if (size > 0)
                {
                    //  load here (no-sram)
					result = _MainLoopExecuteFile(data, FALSE);
                }

                if (!result)
                {
                    NetPlayClientSendLoadAck(NETPLAY_LOADACK_ERROR);
                }  else
                {
                    NetPlayClientSendLoadAck(NETPLAY_LOADACK_OK);
                }
            }
            break;

        case NETPLAY_CALLBACK_UNLOADGAME:
            printf("NetClientEE: Unloading the netgame\n");
            _MainLoopUnloadRom();
            break;

        case NETPLAY_CALLBACK_STARTGAME:
            printf("NetClientEE: Starting the netgame\n");
            _MenuEnable(FALSE);
            break;

        default:
            printf("NetClientEE: Callback %d\n", eCallback);
            break;

    }
	return NULL;
}

char *_MainLoop_NetConfigPaths[]=
{
    /* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827
     * Keep Aurora-local config first, but also understand the standard uLE
     * SYS-CONF location.  Local media matter for an SMB-launched ELF because
     * the SMB device itself is not available until IP configuration succeeds. */
    (char *)"mc0:/SNESticle/",
    (char *)"mc1:/SNESticle/",
    (char *)"mass0:/SNESticle/",
    (char *)"mass1:/SNESticle/",
    (char *)"mass:/SNESticle/",
    _MainLoop_BootDir,
    (char *)"mc0:/SYS-CONF/",
    (char *)"mc1:/SYS-CONF/",
    NULL
};




/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827: parse the standard PS2 IPCONFIG.DAT line:
 *     PS2_IP NETMASK GATEWAY
 * The original SNESticle function was only a stub returning FALSE, so every
 * caller silently fell back to DHCP.  That cannot work on a plain crossover
 * PC<->PS2 link unless the PC is also running a DHCP server. */
static Bool _MainLoopParseIPv4(const char *text, struct in_addr *out)
{
    unsigned int a, b, c, d;
    char tail;

    if (!text || !out ||
        sscanf(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 ||
        a > 255 || b > 255 || c > 255 || d > 255)
        return FALSE;

    /* Same byte layout as PS2SDK IP4_ADDR on the little-endian EE. */
    out->s_addr = ((Uint32)d << 24) | ((Uint32)c << 16) |
                  ((Uint32)b << 8) | (Uint32)a;
    return TRUE;
}

static FILE *_MainLoopOpenNetConfig(const char *path, char *resolved, size_t resolvedSize)
{
    FILE *fp;
    const char *slash;

    if (!path || !path[0] || !resolved || resolvedSize == 0)
        return NULL;

    snprintf(resolved, resolvedSize, "%s", path);
    fp = fopen(resolved, "rb");
    if (fp)
        return fp;

    /* Aurora historically asks for lowercase ipconfig.dat, while the common
       PS2/uLaunchELF filename is IPCONFIG.DAT. Try that sibling as fallback. */
    slash = strrchr(path, '/');
    if (slash && strcmp(slash + 1, "ipconfig.dat") == 0)
    {
        size_t prefix = (size_t)(slash - path + 1);
        static const char upperName[] = "IPCONFIG.DAT";
        if (prefix + sizeof(upperName) <= resolvedSize)
        {
            memcpy(resolved, path, prefix);
            memcpy(resolved + prefix, upperName, sizeof(upperName));
            fp = fopen(resolved, "rb");
            if (fp)
                return fp;
        }
    }

    resolved[0] = 0;
    return NULL;
}

static Bool _MainLoopLoadNetConfig(t_ip_info *pConfig, const char *pConfigPath)
{
    FILE *fp;
    char resolved[1024];
    char line[256];
    char ip[32], mask[32], gateway[32], extra[2];

    if (!pConfig || !pConfigPath)
        return FALSE;

    fp = _MainLoopOpenNetConfig(pConfigPath, resolved, sizeof(resolved));
    if (!fp)
        return FALSE;

    while (fgets(line, sizeof(line), fp))
    {
        char *p = line;
        char *comment;
        int fields;

        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            ++p;
        if (!*p || *p == '#' || *p == ';')
            continue;

        comment = strpbrk(p, "#;");
        if (comment)
            *comment = 0;

        fields = sscanf(p, "%31s %31s %31s %1s", ip, mask, gateway, extra);
        if (fields != 3 ||
            !_MainLoopParseIPv4(ip, &pConfig->ipaddr) ||
            !_MainLoopParseIPv4(mask, &pConfig->netmask) ||
            !_MainLoopParseIPv4(gateway, &pConfig->gw))
        {
            fclose(fp);
            printf("netconfigload: malformed %s\n", resolved);
            return FALSE;
        }

        pConfig->dhcp_enabled = 0;
        pConfig->dhcp_status = DHCP_STATE_OFF;
        fclose(fp);
        printf("netconfigload: static %s (%s %s %s)\n",
               resolved, ip, mask, gateway);
        return TRUE;
    }

    fclose(fp);
    printf("netconfigload: empty %s\n", resolved);
    return FALSE;
}


Bool _MainLoopConfigureNetwork(char **ppSearchPaths, char *pConfigFileName)
{
    t_ip_info config;
    t_ip_info verify;
    int setResult;
    int getResult;

    (void)ppSearchPaths;
    (void)pConfigFileName;

    memset(&config, 0, sizeof(config));
    strcpy(config.netif_name, "sm0");

    config.dhcp_enabled = 0;
    config.dhcp_status = DHCP_STATE_OFF;
    IP4_ADDR((ip4_addr_t *)&config.ipaddr, 192, 168, 0, 10);
    IP4_ADDR((ip4_addr_t *)&config.netmask, 255, 255, 255, 0);
    IP4_ADDR((ip4_addr_t *)&config.gw, 192, 168, 0, 1);

    /*
     * Bypass libcglue here so we can distinguish an EE socket-op problem
     * from an IOP ps2ip/sm0 problem.
     */
    setResult = ps2ipc_ps2ip_setconfig(&config);

    memset(&verify, 0, sizeof(verify));
    getResult = ps2ipc_ps2ip_getconfig((char *)"sm0", &verify);

    {
        const unsigned char *b = (const unsigned char *)&verify.ipaddr.s_addr;
        snprintf(s_netdiag, sizeof(s_netdiag),
                 "%X/%X/%.3s/%02X%02X%02X%02X",
                 setResult, getResult, verify.netif_name,
                 b[0], b[1], b[2], b[3]);
    }

    printf("SMB direct RPC: set=%d get=%d if='%s' ip=%08X mask=%08X gw=%08X dhcp=%u\n",
           setResult, getResult, verify.netif_name,
           verify.ipaddr.s_addr,
           verify.netmask.s_addr,
           verify.gw.s_addr,
           verify.dhcp_enabled);
if (setResult < 0 || getResult < 0)
        return FALSE;

    /*
     * ps2ips GETCONFIG itself returns success when the RPC completed.
     * Validate the actual contents to prove sm0 exists and accepted the IP.
     */
    if (strcmp(verify.netif_name, "sm0") != 0 ||
        verify.ipaddr.s_addr != config.ipaddr.s_addr ||
        verify.netmask.s_addr != config.netmask.s_addr ||
        verify.gw.s_addr != config.gw.s_addr)
        return FALSE;

    return TRUE;
}


/* Modern netman + ps2ip + lwIP bring-up, mirroring
 * hugorsgarcia/PS2SNESticle/SNESticle/Source/ps2/mainloop.cpp::
 * _MainLoopInitNetwork.
 *
 * Sequence:
 *   1. SifExecModuleBuffer ps2dev9 / netman, NetManInit, smap \
 *      via NetIfLoadEmbeddedIrx (src/platform/ps2/system/      | network IRX
 *      embedded_irx.cpp).                                      | stack
 *   2. SifExecModuleBuffer ps2ip -- happens inside step 1.
 *   3. ip4_addr_set_zero on IP/NM/GW so ps2ipInit() starts up
 *      with a no-IP netif we can re-configure later via
 *      ps2ip_setconfig() (which _MainLoopConfigureNetwork does
 *      from `ipconfig.dat` or a hard-coded DHCP default).
 *
 * The `ppSearchPaths` argument used to pass host: / cdrom: /
 * mc0: hints to IOPLoadModule when the IRXs lived on disk; with
 * the bin2c'd images embedded in the ELF those hints are no
 * longer needed.  We keep the argument so callers don't have to
 * change, but otherwise ignore it.
 *
 * Returns TRUE when the whole stack came up.  On failure (no
 * Network Adapter, dev9 not present, ...) returns FALSE and the
 * caller (mainloop_iop.cpp::_MainLoopLoadModules) skips the
 * netplay init that depends on the IP stack being live.
 */
static int s_network_init_result = 1; /* 1=not attempted, 0=ready, -1=failed */

Bool _MainLoopInitNetwork(Char **ppSearchPaths)
{
    int ret;

    (void)ppSearchPaths;

    if (s_network_init_result != 1)
        return s_network_init_result == 0 ? TRUE : FALSE;

    ret = NetIfLoadEmbeddedIrx();
    if (ret < 0)
    {
        printf("_MainLoopInitNetwork: IOP network stack failed (%d)\n", ret);
        s_network_init_result = -1;
        return FALSE;
    }

    /* AURORA_SMB_IOP_STACK_V3_20260827 */
    ret = ps2ip_init();
    if (ret < 0)
    {
        printf("_MainLoopInitNetwork: ps2ip_init failed (%d)\n", ret);
        s_network_init_result = -1;
        return FALSE;
    }

    s_network_init_result = 0;
    return TRUE;
}

/* Wait only after the user explicitly opens a network feature. Network is
 * never touched during boot. A finite timeout avoids reproducing the old
 * black-screen hang when no cable or DHCP server is present. */
Bool _MainLoopWaitForNetwork(Int32 timeoutMs)
{
    t_ip_info config;
    Int32 elapsed = 0;

    if (timeoutMs < 0)
        timeoutMs = 0;

    while (elapsed <= timeoutMs)
    {
        memset(&config, 0, sizeof(config));
        if (ps2ipc_ps2ip_getconfig((char *)"sm0", &config) > 0 &&
            config.ipaddr.s_addr != 0 &&
            (!config.dhcp_enabled ||
             config.dhcp_status == DHCP_STATE_BOUND ||
             config.dhcp_status == DHCP_STATE_OFF))
        {
            printf("Network ready: ip=%08X dhcp=%u\n",
                   config.ipaddr.s_addr, config.dhcp_status);
            return TRUE;
        }

        if (elapsed == timeoutMs)
            break;
        usleep(100000);
        elapsed += 100;
        if (elapsed > timeoutMs)
            elapsed = timeoutMs;
    }

    printf("Network timeout after %d ms\n", timeoutMs);
    return FALSE;
}
