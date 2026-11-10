/* Fable III Restored - co-op discovery without Microsoft's servers.
 * A host answers small UDP queries (port 27421) with its XSESSION_INFO. A joining player finds
 * hosts on the LAN by broadcast, or a host on the internet/VPN by its IP (Fable3Restored.ini
 * [Coop] JoinIP=, set by the launcher). With JoinIP set we also raise an "invite accepted"
 * notification, so the game joins the host the same way it did through the LIVE Guide. */
#include "xl.h"

#define DISC_PORT 27421
#define MAGIC_Q 0x51334346  /* 'FC3Q' */
#define MAGIC_A 0x41334346  /* 'FC3A' */

typedef struct { DWORD magic; DWORD version; } disc_q;
typedef struct { DWORD magic; DWORD version; XSESSION_INFO info; XUID host; char name[16]; DWORD open; } disc_a;

static volatile LONG g_hosting;
static XSESSION_INFO g_host_info;
static HANDLE g_thread;
static SOCKET g_sock = INVALID_SOCKET;

static DWORD WINAPI responder(LPVOID p)
{
    (void)p;
    char buf[256];
    for (;;) {
        struct sockaddr_in from; int fl = sizeof from;
        int n = recvfrom(g_sock, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
        if (n < 0) { if (WSAGetLastError() == WSAENOTSOCK) break; Sleep(50); continue; }
        if (n < (int)sizeof(disc_q) || ((disc_q *)buf)->magic != MAGIC_Q || !g_hosting) continue;
        disc_a a = { MAGIC_A, 1, g_host_info, xl_xuid(), {0}, 1 };
        strncpy(a.name, xl_user_name(), 15);
        sendto(g_sock, (char *)&a, sizeof a, 0, (struct sockaddr *)&from, fl);
    }
    return 0;
}

void xl_coop_hosting(const XSESSION_INFO *info, int open)
{
    if (open) {
        g_host_info = *info;
        InterlockedExchange(&g_hosting, 1);
        if (!g_thread) {
            WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
            g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            struct sockaddr_in a = { 0 };
            a.sin_family = AF_INET; a.sin_port = htons(DISC_PORT); a.sin_addr.s_addr = INADDR_ANY;
            if (bind(g_sock, (struct sockaddr *)&a, sizeof a) != 0) xl_log("coop: discovery port %d busy (%d)", DISC_PORT, WSAGetLastError());
            g_thread = CreateThread(NULL, 0, responder, NULL, 0, NULL);
        }
        xl_log("coop: hosting, discoverable on udp %d", DISC_PORT);
    } else {
        InterlockedExchange(&g_hosting, 0);
        xl_log("coop: hosting closed");
    }
}

/* query LAN broadcast + configured JoinIP, collect answers */
static int query(disc_a *out, int max, int timeout_ms)
{
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return 0;
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char *)&yes, sizeof yes);
    disc_q q = { MAGIC_Q, 1 };
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET; a.sin_port = htons(DISC_PORT);
    a.sin_addr.s_addr = INADDR_BROADCAST;
    sendto(s, (char *)&q, sizeof q, 0, (struct sockaddr *)&a, sizeof a);
    char ip[64];
    xl_cfg_str("Coop", "JoinIP", "", ip, sizeof ip);
    if (ip[0]) {
        struct addrinfo hints = { 0 }, *res = NULL;
        hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(ip, NULL, &hints, &res) == 0 && res) {
            a.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
            freeaddrinfo(res);
            for (int i = 0; i < 3; i++) sendto(s, (char *)&q, sizeof q, 0, (struct sockaddr *)&a, sizeof a);
        }
    }
    int n = 0;
    DWORD end = GetTickCount() + timeout_ms;
    while (n < max) {
        int left = (int)(end - GetTickCount());
        if (left <= 0) break;
        fd_set rs; FD_ZERO(&rs); FD_SET(s, &rs);
        struct timeval tv = { 0, left * 1000 };
        if (select(0, &rs, NULL, NULL, &tv) <= 0) break;
        disc_a r; struct sockaddr_in from; int fl = sizeof from;
        int got = recvfrom(s, (char *)&r, sizeof r, 0, (struct sockaddr *)&from, &fl);
        if (got != (int)sizeof r || r.magic != MAGIC_A || r.host == xl_xuid()) continue;
        int dup = 0;
        for (int i = 0; i < n; i++) if (out[i].host == r.host) dup = 1;
        if (dup) continue;
        /* the host may report a private address; use the one the answer came from */
        r.info.hostAddress.ina = from.sin_addr;
        r.info.hostAddress.inaOnline = from.sin_addr;
        out[n++] = r;
        xl_log("coop: found host '%s' at %s", r.name, inet_ntoa(from.sin_addr));
    }
    closesocket(s);
    return n;
}

int xl_coop_search(XSESSION_SEARCHRESULT *out, int max)
{
    disc_a found[8];
    int n = query(found, max < 8 ? max : 8, 700);
    for (int i = 0; i < n; i++) {
        memset(&out[i], 0, sizeof out[i]);
        out[i].info = found[i].info;
        out[i].dwOpenPublicSlots = 1;
        out[i].dwOpenPrivateSlots = 1;
        out[i].dwFilledPublicSlots = 1;
    }
    return n;
}

static int g_invite_used;

int xl_coop_take_invite(XINVITE_INFO *inv)
{
    char ip[64];
    xl_cfg_str("Coop", "JoinIP", "", ip, sizeof ip);
    if (!ip[0] || g_invite_used) return 0;
    disc_a found[1];
    if (query(found, 1, 1500) < 1) { xl_log("coop: no host answered at %s", ip); return 0; }
    memset(inv, 0, sizeof *inv);
    inv->dwUserIndex = 0;
    inv->fFromGameInvite = TRUE;
    inv->dwTitleID = 0x4D53090A;
    inv->hostInfo = found[0].info;
    inv->xuidInviter = found[0].host;
    g_invite_used = 1;
    return 1;
}

/* called once the game listens for notifications: JoinIP turns into an accepted invite */
void xl_coop_startup(void)
{
    char ip[64];
    xl_cfg_str("Coop", "JoinIP", "", ip, sizeof ip);
    if (ip[0]) xl_notify_push(XN_LIVE_INVITE_ACCEPTED, 0);
}

/* achievements toast: logged for now */
void xl_achievement_toast(DWORD id) { (void)id; }
