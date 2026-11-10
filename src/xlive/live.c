/* Fable III Restored - XLive lifecycle, notifications, PBuffers, protected data, UI calls. */
#include "xl.h"
#include <mmsystem.h>

/* ---------------- notifications ---------------- */
/* Each listener has its own queue and area mask; its handle is a real event so the game can
 * wait on it. Notifications go to every listener whose areas include the id's area. */
typedef struct { DWORD id; ULONG_PTR param; } note_t;
#define NOTE_MAX 64
#define LISTENER_MAX 16
typedef struct { HANDLE ev; ULONGLONG areas; note_t q[NOTE_MAX]; int n; } listener_t;
static CRITICAL_SECTION g_note_cs;
static listener_t g_ls[LISTENER_MAX];
static int g_nls;
static volatile LONG g_note_init;

static void note_init(void)
{
    if (InterlockedCompareExchange(&g_note_init, 1, 0) == 0) InitializeCriticalSection(&g_note_cs);
}

static ULONGLONG note_area(DWORD id) { return 1ULL << ((id >> 25) & 0x3F); }

static void push_to(listener_t *l, DWORD id, ULONG_PTR param)
{
    if (!(l->areas & note_area(id)) || l->n >= NOTE_MAX) return;
    l->q[l->n].id = id; l->q[l->n].param = param; l->n++;
    SetEvent(l->ev);
}

void xl_notify_push(DWORD id, ULONG_PTR param)
{
    note_init();
    EnterCriticalSection(&g_note_cs);
    for (int i = 0; i < g_nls; i++) push_to(&g_ls[i], id, param);
    LeaveCriticalSection(&g_note_cs);
    xl_log("notify %08lX param %lX", id, (unsigned long)param);
}

HANDLE XAPI XNotifyCreateListener(ULONGLONG qwAreas)
{
    static int coop_started;
    note_init();
    EnterCriticalSection(&g_note_cs);
    if (g_nls >= LISTENER_MAX) { LeaveCriticalSection(&g_note_cs); return NULL; }
    listener_t *l = &g_ls[g_nls++];
    l->ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    l->areas = qwAreas;
    l->n = 0;
    /* initial state every listener sees: profile 0 signed in, LIVE connected */
    push_to(l, XN_SYS_SIGNINCHANGED, 1);
    push_to(l, XN_LIVE_CONNECTIONCHANGED, XONLINE_S_LOGON_CONNECTION_ESTABLISHED);
    LeaveCriticalSection(&g_note_cs);
    xl_log("XNotifyCreateListener(%llX) -> %p", (unsigned long long)qwAreas, l->ev);
    if (!coop_started && (qwAreas & XNOTIFY_LIVE)) {
        extern void xl_coop_startup(void);
        coop_started = 1;
        xl_coop_startup();
    }
    return l->ev;
}

BOOL XAPI XNotifyGetNext(HANDLE hNotification, DWORD dwMsgFilter, PDWORD pdwId, PULONG_PTR pParam)
{
    note_init();
    BOOL got = FALSE;
    EnterCriticalSection(&g_note_cs);
    for (int k = 0; k < g_nls; k++) {
        listener_t *l = &g_ls[k];
        if (l->ev != hNotification) continue;
        for (int i = 0; i < l->n; i++) {
            if (dwMsgFilter && l->q[i].id != dwMsgFilter) continue;
            if (pdwId) *pdwId = l->q[i].id;
            if (pParam) *pParam = l->q[i].param;
            memmove(&l->q[i], &l->q[i + 1], (l->n - i - 1) * sizeof(note_t));
            l->n--;
            got = TRUE;
            break;
        }
        if (!l->n) ResetEvent(l->ev);
        break;
    }
    LeaveCriticalSection(&g_note_cs);
    return got;
}

DWORD XAPI XNotifyPositionUI(DWORD dwPosition) { (void)dwPosition; return ERROR_SUCCESS; }

/* ---------------- lifecycle ---------------- */
HRESULT XAPI XLiveInitialize(void *pXii) { (void)pXii; xl_log("XLiveInitialize"); return S_OK; }
HRESULT XAPI XLiveInitializeEx(void *pXii, DWORD dwVersion) { (void)pXii; xl_log("XLiveInitializeEx(ver %08lX)", dwVersion); return S_OK; }
void xl_test_exit(void); void xl_test_on_device(void *); void xl_test_frame(void);
void    XAPI XLiveUninitialize(void) { xl_log("XLiveUninitialize"); xl_test_exit(); }
HRESULT XAPI XLiveOnCreateDevice(void *pDevice, void *pPresentParams) { (void)pPresentParams; xl_log("XLiveOnCreateDevice"); xl_test_on_device(pDevice); return S_OK; }
HRESULT XAPI XLiveOnDestroyDevice(void) { return S_OK; }
HRESULT XAPI XLiveOnResetDevice(void *pPresentParams) { (void)pPresentParams; return S_OK; }
/* Frame limiter ([Display] FPSLimit, default 60, 0 = off). The game calls XLiveRender right
 * before every Present, so waiting here paces the frames. The cloth/hair physics jitter at
 * very high frame rates. Sleep for most of the wait (1 ms timer resolution), spin the rest. */
static void frame_limit(void)
{
    static int limit = -1;
    static LARGE_INTEGER freq, next;
    if (limit < 0) {
        limit = xl_cfg_int("Display", "FPSLimit", 60);
        QueryPerformanceFrequency(&freq);
        timeBeginPeriod(1);
        xl_log("frame limit: %d fps", limit);
    }
    if (limit <= 0) return;
    LONGLONG step = freq.QuadPart / limit;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!next.QuadPart || now.QuadPart - next.QuadPart > step * 4) next.QuadPart = now.QuadPart;  /* start / after a long stall */
    LONGLONG left = next.QuadPart - now.QuadPart;
    if (left > 0) {
        LONGLONG ms = left * 1000 / freq.QuadPart;
        if (ms > 2) Sleep((DWORD)(ms - 2));
        do QueryPerformanceCounter(&now); while (now.QuadPart < next.QuadPart);
    }
    next.QuadPart += step;
}

/* frame counter: fps is logged every 10 s (Fable3Restored.log) */
HRESULT XAPI XLiveRender(void)
{
    static DWORD t0, frames;
    frame_limit();
    DWORD now = GetTickCount();
    if (!t0) t0 = now;
    frames++;
    if (now - t0 >= 10000) { xl_log("fps %.1f", frames * 1000.0 / (now - t0)); t0 = now; frames = 0; }
    xl_test_frame();
    return S_OK;
}
HRESULT XAPI XLiveInput(void *pXli) { (void)pXli; return S_FALSE; }
BOOL    XAPI XLivePreTranslateMessage(const MSG *pMsg) { (void)pMsg; return FALSE; }
HRESULT XAPI XLiveSetDebugLevel(DWORD dwNewLevel, DWORD *pdwOldLevel) { (void)dwNewLevel; if (pdwOldLevel) *pdwOldLevel = 0; return S_OK; }
HRESULT XAPI XLiveGetUpdateInformation(void *p) { (void)p; return S_FALSE; }  /* no update available */
HRESULT XAPI XLiveUpdateSystem(LPCWSTR p) { (void)p; return S_FALSE; }
DWORD   XAPI XOnlineStartup(void) { xl_log("XOnlineStartup"); return ERROR_SUCCESS; }
DWORD   XAPI XOnlineCleanup(void) { return ERROR_SUCCESS; }
DWORD   XAPI XOnlineGetNatType(void) { return 1; /* XONLINE_NAT_OPEN */ }

/* ---------------- PBuffers ---------------- */
typedef struct { DWORD magic; DWORD size; BYTE data[1]; } pbuf_t;
#define PB_MAGIC 0x46554250

HRESULT XAPI XLivePBufferAllocate(DWORD size, void **pp)
{
    if (!pp) return E_POINTER;
    pbuf_t *b = calloc(1, sizeof(pbuf_t) + size);
    if (!b) return E_OUTOFMEMORY;
    b->magic = PB_MAGIC; b->size = size;
    *pp = b;
    return S_OK;
}
HRESULT XAPI XLivePBufferFree(void *p) { pbuf_t *b = p; if (b && b->magic == PB_MAGIC) { b->magic = 0; free(b); } return S_OK; }

static pbuf_t *pb(void *p) { pbuf_t *b = p; return (b && b->magic == PB_MAGIC) ? b : NULL; }

HRESULT XAPI XLivePBufferGetByteArray(void *p, DWORD off, BYTE *dst, DWORD n)
{
    pbuf_t *b = pb(p);
    if (!b || !dst || off + n > b->size) return E_INVALIDARG;
    memcpy(dst, b->data + off, n);
    return S_OK;
}
HRESULT XAPI XLivePBufferSetByteArray(void *p, DWORD off, const BYTE *src, DWORD n)
{
    pbuf_t *b = pb(p);
    if (!b || !src || off + n > b->size) return E_INVALIDARG;
    memcpy(b->data + off, src, n);
    return S_OK;
}
HRESULT XAPI XLivePBufferGetByte(void *p, DWORD off, BYTE *v) { return XLivePBufferGetByteArray(p, off, v, 1); }
HRESULT XAPI XLivePBufferSetByte(void *p, DWORD off, BYTE v) { return XLivePBufferSetByteArray(p, off, &v, 1); }
HRESULT XAPI XLivePBufferGetDWORD(void *p, DWORD off, DWORD *v) { return XLivePBufferGetByteArray(p, off, (BYTE *)v, 4); }
HRESULT XAPI XLivePBufferSetDWORD(void *p, DWORD off, DWORD v) { return XLivePBufferSetByteArray(p, off, (BYTE *)&v, 4); }

/* writes into a PBuffer from other modules (content.c) */
BOOL xl_pbuffer_write(void *p, DWORD off, const void *src, DWORD n)
{
    return SUCCEEDED(XLivePBufferSetByteArray(p, off, src, n));
}

/* ---------------- protected data ----------------
 * Real GFWL encrypts with a per-profile key, which is why saves broke when profiles or PCs
 * changed. Ours stores a small tagged header + the plain bytes, so data stays portable. */
#define PD_MAGIC 0x50523346  /* 'F3RP' */
typedef struct { DWORD magic; DWORD size; } pd_hdr;

DWORD XAPI XLiveCreateProtectedDataContext(void *pInfo, HANDLE *phContext)
{
    (void)pInfo;
    if (!phContext) return ERROR_INVALID_PARAMETER;
    *phContext = xl_handle_new(5);
    return ERROR_SUCCESS;
}
DWORD XAPI XLiveQueryProtectedDataInformation(HANDLE h, DWORD *pInfo)
{
    (void)h;
    if (pInfo) { pInfo[1] = 0; } /* { cbSize, dwFlags } - caller-owned cbSize */
    return ERROR_SUCCESS;
}
DWORD XAPI XLiveCloseProtectedDataContext(HANDLE h) { return XCloseHandle(h); }

DWORD XAPI XLiveProtectData(BYTE *pIn, DWORD cbIn, BYTE *pOut, DWORD *pcbOut, HANDLE h)
{
    (void)h;
    if (!pcbOut) return ERROR_INVALID_PARAMETER;
    DWORD need = cbIn + sizeof(pd_hdr);
    if (!pOut || *pcbOut < need) { *pcbOut = need; return ERROR_INSUFFICIENT_BUFFER; }
    pd_hdr hd = { PD_MAGIC, cbIn };
    memcpy(pOut, &hd, sizeof hd);
    if (cbIn) memcpy(pOut + sizeof hd, pIn, cbIn);
    *pcbOut = need;
    return ERROR_SUCCESS;
}

DWORD XAPI XLiveUnprotectData(BYTE *pIn, DWORD cbIn, BYTE *pOut, DWORD *pcbOut, HANDLE h)
{
    (void)h;
    if (!pcbOut || !pIn) return ERROR_INVALID_PARAMETER;
    pd_hdr hd;
    if (cbIn < sizeof hd) return ERROR_INVALID_DATA;
    memcpy(&hd, pIn, sizeof hd);
    if (hd.magic != PD_MAGIC || hd.size > cbIn - sizeof hd) {
        xl_log("XLiveUnprotectData: data from real GFWL (%lu bytes) cannot be read", cbIn);
        return ERROR_INVALID_DATA;
    }
    if (!pOut || *pcbOut < hd.size) { *pcbOut = hd.size; return ERROR_INSUFFICIENT_BUFFER; }
    memcpy(pOut, pIn + sizeof hd, hd.size);
    *pcbOut = hd.size;
    return ERROR_SUCCESS;
}

/* ---------------- Guide UI (no overlay; answer like the user picked the default) ---------------- */
DWORD XAPI XShowGuideUI(DWORD u) { (void)u; return ERROR_SUCCESS; }
DWORD XAPI XShowSigninUI(DWORD cPanes, DWORD dwFlags)
{
    (void)cPanes; (void)dwFlags;
    xl_notify_push(XN_SYS_UI, 1);
    xl_notify_push(XN_SYS_SIGNINCHANGED, 1);
    xl_notify_push(XN_SYS_UI, 0);
    return ERROR_SUCCESS;
}
DWORD XAPI XShowFriendsUI(DWORD u) { (void)u; return ERROR_SUCCESS; }
DWORD XAPI XShowPlayersUI(DWORD u, const XUID *p, DWORD n) { (void)u; (void)p; (void)n; return ERROR_SUCCESS; }
DWORD XAPI XShowGamerCardUI(DWORD u, XUID x) { (void)u; (void)x; return ERROR_SUCCESS; }
DWORD XAPI XShowGameInviteUI(DWORD u, const XUID *p, DWORD n, LPCWSTR t) { (void)u; (void)p; (void)n; (void)t; return ERROR_SUCCESS; }
DWORD XAPI XShowMessageComposeUI(DWORD u, const XUID *p, DWORD n, LPCWSTR t) { (void)u; (void)p; (void)n; (void)t; return ERROR_SUCCESS; }
DWORD XAPI XShowFriendRequestUI(DWORD u, XUID x) { (void)u; (void)x; return ERROR_SUCCESS; }
DWORD XAPI XShowMarketplaceUI(DWORD u, DWORD ep, ULONGLONG offer, DWORD cats) { (void)u; (void)ep; (void)offer; (void)cats; return ERROR_SUCCESS; }

DWORD XAPI XShowMessageBoxUI(DWORD dwUserIndex, LPCWSTR wszTitle, LPCWSTR wszText, DWORD cButtons,
                             LPCWSTR *pwszButtons, DWORD dwFocusButton, DWORD dwFlags, DWORD *pResult, XOVERLAPPED *pOverlapped)
{
    (void)dwUserIndex; (void)cButtons; (void)pwszButtons; (void)dwFlags;
    xl_log("XShowMessageBoxUI '%ls' / '%ls' -> button %lu", wszTitle ? wszTitle : L"", wszText ? wszText : L"", dwFocusButton);
    if (pResult) *pResult = dwFocusButton;
    return xl_complete(pOverlapped, ERROR_SUCCESS, 0);
}

DWORD XAPI XShowKeyboardUI(DWORD dwUserIndex, DWORD dwFlags, LPCWSTR wseDefaultText, LPCWSTR wszTitleText,
                           LPCWSTR wszDescriptionText, LPWSTR wszResultText, DWORD cchResultText, XOVERLAPPED *pOverlapped)
{
    (void)dwUserIndex; (void)dwFlags; (void)wszTitleText; (void)wszDescriptionText;
    if (wszResultText && cchResultText) {
        lstrcpynW(wszResultText, wseDefaultText ? wseDefaultText : L"", (int)cchResultText);
    }
    return xl_complete(pOverlapped, ERROR_SUCCESS, 0);
}

/* STRING_VERIFY_RESPONSE { WORD wNumStrings; HRESULT *pStringResult; } */
DWORD XAPI XStringVerify(DWORD dwFlags, const CHAR *szLocale, DWORD dwNumStrings, const void *pStringData,
                         DWORD cbResults, void *pResults, XOVERLAPPED *pOverlapped)
{
    (void)dwFlags; (void)szLocale; (void)pStringData;
    DWORD need = 8 + dwNumStrings * sizeof(HRESULT);
    if (!pResults || cbResults < need) return xl_complete(pOverlapped, ERROR_INSUFFICIENT_BUFFER, 0);
    BYTE *r = pResults;
    *(WORD *)r = (WORD)dwNumStrings;
    HRESULT *res = (HRESULT *)(r + 8);
    *(HRESULT **)(r + 4) = res;
    for (DWORD i = 0; i < dwNumStrings; i++) res[i] = S_OK;
    return xl_complete(pOverlapped, ERROR_SUCCESS, 0);
}
