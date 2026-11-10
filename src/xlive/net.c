/* Fable III Restored - networking: XSockets / XNet on top of plain Winsock.
 * Each player's XNADDR carries their real IPv4 + game port, so XNADDR <-> IN_ADDR mapping is
 * the identity and co-op runs as direct UDP between the two PCs (LAN, VPN or port-forwarded). */
#include "xl.h"
#include <iphlpapi.h>

#define IPPROTO_VDP 254
static WORD g_game_port;   /* port the game bound (host order) */

/* ---------------- Winsock passthrough ---------------- */
int XAPI XWSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData) { return WSAStartup(wVersionRequested, lpWSAData); }
int XAPI XWSACleanup(void) { return WSACleanup(); }

SOCKET XAPI XSocketCreate(int af, int type, int protocol)
{
    if (protocol == IPPROTO_VDP) protocol = IPPROTO_UDP;  /* VDP = UDP between our two ends */
    SOCKET s = socket(af, type, protocol);
    if (s != INVALID_SOCKET && type == SOCK_DGRAM) {
        BOOL yes = TRUE;
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char *)&yes, sizeof yes);
        /* ignore ICMP port-unreachable resets on UDP */
        DWORD no = 0, ret;
        WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12), &no, sizeof no, NULL, 0, &ret, NULL, NULL);
    }
    xl_log("XSocketCreate(%d,%d,%d) -> %d", af, type, protocol, (int)s);
    return s;
}
int XAPI XSocketClose(SOCKET s) { return closesocket(s); }
int XAPI XSocketShutdown(SOCKET s, int how) { return shutdown(s, how); }
int XAPI XSocketIOCTLSocket(SOCKET s, long cmd, u_long *argp) { return ioctlsocket(s, cmd, argp); }
int XAPI XSocketSetSockOpt(SOCKET s, int level, int optname, const char *optval, int optlen)
{
    /* Xbox-only options (SO_BROADCAST is fine; 0x5801 etc. are title-private) */
    if (level == SOL_SOCKET && (optname == 0x5801 || optname == 0x5802)) return 0;
    return setsockopt(s, level, optname, optval, optlen);
}
int XAPI XSocketGetSockOpt(SOCKET s, int level, int optname, char *optval, int *optlen) { return getsockopt(s, level, optname, optval, optlen); }
int XAPI XSocketGetSockName(SOCKET s, struct sockaddr *name, int *namelen) { return getsockname(s, name, namelen); }
int XAPI XSocketGetPeerName(SOCKET s, struct sockaddr *name, int *namelen) { return getpeername(s, name, namelen); }

int XAPI XSocketBind(SOCKET s, const struct sockaddr *name, int namelen)
{
    struct sockaddr_in a;
    if (name && namelen >= (int)sizeof a && name->sa_family == AF_INET) {
        memcpy(&a, name, sizeof a);
        int cfgport = xl_cfg_int("Network", "Port", 0);
        if (cfgport > 0 && ntohs(a.sin_port) != 0) a.sin_port = htons((u_short)cfgport);
        g_game_port = ntohs(a.sin_port);
        int r = bind(s, (struct sockaddr *)&a, sizeof a);
        xl_log("XSocketBind(%d, port %u) -> %d (%d)", (int)s, g_game_port, r, r ? WSAGetLastError() : 0);
        return r;
    }
    return bind(s, name, namelen);
}
int XAPI XSocketConnect(SOCKET s, const struct sockaddr *name, int namelen) { return connect(s, name, namelen); }
int XAPI XSocketListen(SOCKET s, int backlog) { return listen(s, backlog); }
SOCKET XAPI XSocketAccept(SOCKET s, struct sockaddr *addr, int *addrlen) { return accept(s, addr, addrlen); }
int XAPI XSocketSelect(int n, fd_set *r, fd_set *w, fd_set *e, const struct timeval *t) { return select(n, r, w, e, t); }
BOOL XAPI XWSAGetOverlappedResult(SOCKET s, LPWSAOVERLAPPED ov, LPDWORD cb, BOOL wait, LPDWORD flags) { return WSAGetOverlappedResult(s, ov, cb, wait, flags); }
int XAPI XWSACancelOverlappedIO(HANDLE h) { (void)h; return 0; }
int XAPI XSocketRecv(SOCKET s, char *buf, int len, int flags) { return recv(s, buf, len, flags); }
int XAPI XWSARecv(SOCKET s, LPWSABUF b, DWORD n, LPDWORD cb, LPDWORD fl, LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr) { return WSARecv(s, b, n, cb, fl, ov, cr); }
int XAPI XSocketRecvFrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen) { return recvfrom(s, buf, len, flags, from, fromlen); }
int XAPI XWSARecvFrom(SOCKET s, LPWSABUF b, DWORD n, LPDWORD cb, LPDWORD fl, struct sockaddr *from, LPINT fromlen, LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr)
{
    return WSARecvFrom(s, b, n, cb, fl, from, fromlen, ov, cr);
}
int XAPI XSocketSend(SOCKET s, const char *buf, int len, int flags) { return send(s, buf, len, flags); }
int XAPI XWSASend(SOCKET s, LPWSABUF b, DWORD n, LPDWORD cb, DWORD fl, LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr) { return WSASend(s, b, n, cb, fl, ov, cr); }

/* Broadcasts (system link) go to the LAN broadcast address on the same port. */
int XAPI XSocketSendTo(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
    return sendto(s, buf, len, flags, to, tolen);
}
int XAPI XWSASendTo(SOCKET s, LPWSABUF b, DWORD n, LPDWORD cb, DWORD fl, const struct sockaddr *to, int tolen, LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr)
{
    return WSASendTo(s, b, n, cb, fl, to, tolen, ov, cr);
}
unsigned long XAPI XSocketInet_Addr(const char *cp) { return inet_addr(cp); }
int XAPI XWSAGetLastError(void) { return WSAGetLastError(); }
u_short XAPI XSocketNTOHS(u_short v) { return ntohs(v); }
u_long  XAPI XSocketNTOHL(u_long v) { return ntohl(v); }

/* ---------------- XNet ---------------- */
static IN_ADDR g_local_ip;

static void pick_local_ip(void)
{
    if (g_local_ip.s_addr) return;
    char cfg[64];
    xl_cfg_str("Network", "LocalIP", "", cfg, sizeof cfg);
    if (cfg[0]) { g_local_ip.s_addr = inet_addr(cfg); return; }
    /* the address Windows would use to reach the internet */
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET; a.sin_port = htons(53); a.sin_addr.s_addr = inet_addr("8.8.8.8");
    if (s != INVALID_SOCKET && connect(s, (struct sockaddr *)&a, sizeof a) == 0) {
        int n = sizeof a;
        if (getsockname(s, (struct sockaddr *)&a, &n) == 0) g_local_ip = a.sin_addr;
    }
    if (s != INVALID_SOCKET) closesocket(s);
    if (!g_local_ip.s_addr) g_local_ip.s_addr = htonl(INADDR_LOOPBACK);
    xl_log("local ip %s", inet_ntoa(g_local_ip));
}

int XAPI XNetStartup(const void *pxnsp) { (void)pxnsp; pick_local_ip(); xl_log("XNetStartup"); return 0; }
int XAPI XNetCleanup(void) { return 0; }

int XAPI XNetRandom(BYTE *pb, UINT cb)
{
    static int seeded;
    if (!seeded) { srand(GetTickCount() ^ GetCurrentProcessId()); seeded = 1; }
    for (UINT i = 0; i < cb; i++) pb[i] = (BYTE)(rand() >> 3);
    return 0;
}
int XAPI XNetCreateKey(XNKID *pxnkid, XNKEY *pxnkey)
{
    XNetRandom((BYTE *)pxnkid, sizeof *pxnkid);
    XNetRandom((BYTE *)pxnkey, sizeof *pxnkey);
    pxnkid->ab[0] &= 0xFE;   /* XNET_XNKID_MASK: online peer-to-peer session */
    pxnkid->ab[0] |= 0x02;
    return 0;
}
int XAPI XNetRegisterKey(const XNKID *pxnkid, const XNKEY *pxnkey) { (void)pxnkid; (void)pxnkey; return 0; }
int XAPI XNetUnregisterKey(const XNKID *pxnkid) { (void)pxnkid; return 0; }

void xl_fill_xnaddr(XNADDR *a)
{
    pick_local_ip();
    memset(a, 0, sizeof *a);
    a->ina = g_local_ip;
    a->inaOnline = g_local_ip;
    a->wPortOnline = htons(g_game_port ? g_game_port : 3074);
    XUID x = xl_xuid();
    memcpy(a->abEnet, &x, 6);   /* stable per-player id, used as machine id */
    memcpy(a->abOnline, &x, 8);
}

DWORD XAPI XNetGetTitleXnAddr(XNADDR *pxna)
{
    if (pxna) xl_fill_xnaddr(pxna);
    return 0x00000002 | 0x00000004 | 0x00000040 | 0x00000080; /* ETHERNET|STATIC|DNS|ONLINE */
}
DWORD XAPI XNetGetEthernetLinkStatus(void) { return 0x01 | 0x02 | 0x08; }
int XAPI XNetSetSystemLinkPort(WORD wPort) { (void)wPort; return 0; }

int XAPI XNetXnAddrToInAddr(const XNADDR *pxna, const XNKID *pxnkid, IN_ADDR *pina)
{
    (void)pxnkid;
    if (!pxna || !pina) return WSAEINVAL;
    *pina = pxna->inaOnline.s_addr ? pxna->inaOnline : pxna->ina;
    return 0;
}
int XAPI XNetServerToInAddr(const IN_ADDR ina, DWORD dwServiceId, IN_ADDR *pina) { (void)dwServiceId; if (pina) *pina = ina; return 0; }

/* reverse lookup table of addresses we have seen in sessions */
static XNADDR g_known[16];
static int g_nknown;
void xl_remember_xnaddr(const XNADDR *a)
{
    for (int i = 0; i < g_nknown; i++) if (g_known[i].ina.s_addr == a->ina.s_addr) { g_known[i] = *a; return; }
    if (g_nknown < 16) g_known[g_nknown++] = *a;
}

int XAPI XNetInAddrToXnAddr(const IN_ADDR ina, XNADDR *pxna, XNKID *pxnkid)
{
    if (pxnkid) memset(pxnkid, 0, sizeof *pxnkid);
    if (!pxna) return WSAEINVAL;
    for (int i = 0; i < g_nknown; i++) if (g_known[i].ina.s_addr == ina.s_addr) { *pxna = g_known[i]; return 0; }
    memset(pxna, 0, sizeof *pxna);
    pxna->ina = ina; pxna->inaOnline = ina;
    pxna->wPortOnline = htons(g_game_port ? g_game_port : 3074);
    memcpy(pxna->abEnet, &ina, 4);
    return 0;
}
int XAPI XNetUnregisterInAddr(const IN_ADDR ina) { (void)ina; return 0; }
int XAPI XNetXnAddrToMachineId(const XNADDR *pxna, ULONGLONG *pqwMachineId)
{
    if (!pxna || !pqwMachineId) return WSAEINVAL;
    ULONGLONG id = 0;
    memcpy(&id, pxna->abEnet, 6);
    if (!id) id = pxna->ina.s_addr;
    *pqwMachineId = 0xFA00000000000000ULL | id;
    return 0;
}
int XAPI XNetConnect(const IN_ADDR ina) { (void)ina; return 0; }
DWORD XAPI XNetGetConnectStatus(const IN_ADDR ina) { (void)ina; return 2; /* XNET_CONNECT_STATUS_CONNECTED */ }

/* QoS: no probing service; report generous numbers so co-op is allowed */
typedef struct { BYTE bFlags, bReserved; WORD cProbesXmit, cProbesRecv; WORD cbData; BYTE *pbData; WORD wRttMinInMsecs, wRttMedInMsecs; DWORD dwUpBitsPerSec, dwDnBitsPerSec; } XNQOSINFO;
typedef struct { UINT cxnqos; UINT cxnqosPending; XNQOSINFO axnqosinfo[1]; } XNQOS;

int XAPI XNetQosListen(const XNKID *pxnkid, const BYTE *pb, UINT cb, DWORD dwBitsPerSec, DWORD dwFlags)
{
    (void)pxnkid; (void)pb; (void)cb; (void)dwBitsPerSec; (void)dwFlags;
    return 0;
}
int XAPI XNetQosLookup(UINT cxna, const XNADDR *apxna[], const XNKID *apxnkid[], const XNKEY *apxnkey[], UINT cina,
                       const IN_ADDR aina[], const DWORD adwServiceId[], UINT cProbes, DWORD dwBitsPerSec, DWORD dwFlags,
                       HANDLE hEvent, XNQOS **ppxnqos)
{
    (void)apxna; (void)apxnkid; (void)apxnkey; (void)aina; (void)adwServiceId; (void)cProbes; (void)dwBitsPerSec; (void)dwFlags;
    UINT n = cxna + cina;
    XNQOS *q = calloc(1, sizeof(XNQOS) + n * sizeof(XNQOSINFO));
    q->cxnqos = n;
    for (UINT i = 0; i < n; i++) {
        q->axnqosinfo[i].bFlags = 0x01 | 0x02; /* COMPLETE | TARGETCONTACTED */
        q->axnqosinfo[i].cProbesXmit = q->axnqosinfo[i].cProbesRecv = 4;
        q->axnqosinfo[i].wRttMinInMsecs = q->axnqosinfo[i].wRttMedInMsecs = 30;
        q->axnqosinfo[i].dwUpBitsPerSec = q->axnqosinfo[i].dwDnBitsPerSec = 10000000;
    }
    if (ppxnqos) *ppxnqos = q;
    if (hEvent) SetEvent(hEvent);
    return 0;
}
int XAPI XNetQosRelease(XNQOS *pxnqos) { free(pxnqos); return 0; }
