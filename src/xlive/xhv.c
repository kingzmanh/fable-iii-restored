/* Fable III Restored - a silent XHV voice engine.
 * Fable3.exe never checks XHVCreateEngine's result and calls the engine unconditionally
 * (RegisterLocalTalker at vtable +0x20 at boot), so returning failure crashes it.
 * Methods are stdcall with `this` first; argument sizes were taken from the game's calls:
 *   +0x0c StartLocalProcessingModes(user, modes, n)   +0x14 StartRemoteProcessingModes(xuid, modes, n)
 *   +0x20 RegisterLocalTalker(user)  +0x24 UnregisterLocalTalker(user)
 *   +0x28 RegisterRemoteTalker(xuid, 0, 0, 0)  +0x2c UnregisterRemoteTalker(xuid)
 *   +0x34 IsHeadsetPresent(user)  +0x40 GetDataReadyFlags()
 *   +0x44 GetLocalChatData(user, pb, pcb, pcPackets)  +0x48 SetPlaybackPriority(xuid, user, prio)
 *   +0x4c SubmitIncomingChatData(xuid, pb, pcb) */
#include "xl.h"

typedef struct xhv { const void *const *vt; LONG ref; } xhv;

static ULONG   __stdcall e_AddRef(xhv *t) { return (ULONG)InterlockedIncrement(&t->ref); }
static ULONG   __stdcall e_Release(xhv *t) { return (ULONG)InterlockedDecrement(&t->ref); }   /* static object */
static HRESULT __stdcall e_Lock(xhv *t, DWORD type) { (void)t; (void)type; return S_OK; }
static HRESULT __stdcall e_StartLocal(xhv *t, DWORD u, const DWORD *m, DWORD n) { (void)t; (void)u; (void)m; (void)n; return S_OK; }
static HRESULT __stdcall e_StopLocal(xhv *t, DWORD u, const DWORD *m, DWORD n) { (void)t; (void)u; (void)m; (void)n; return S_OK; }
static HRESULT __stdcall e_StartRemote(xhv *t, XUID x, const DWORD *m, DWORD n) { (void)t; (void)x; (void)m; (void)n; return S_OK; }
static HRESULT __stdcall e_StopRemote(xhv *t, XUID x, const DWORD *m, DWORD n) { (void)t; (void)x; (void)m; (void)n; return S_OK; }
static HRESULT __stdcall e_SetMaxDecode(xhv *t, DWORD n) { (void)t; (void)n; return S_OK; }
static HRESULT __stdcall e_RegLocal(xhv *t, DWORD u) { (void)t; (void)u; return S_OK; }
static HRESULT __stdcall e_UnregLocal(xhv *t, DWORD u) { (void)t; (void)u; return S_OK; }
static HRESULT __stdcall e_RegRemote(xhv *t, XUID x, void *a, void *b, void *c) { (void)t; (void)x; (void)a; (void)b; (void)c; return S_OK; }
static HRESULT __stdcall e_UnregRemote(xhv *t, XUID x) { (void)t; (void)x; return S_OK; }
static HRESULT __stdcall e_GetRemoteTalkers(xhv *t, DWORD *pn, XUID *px) { (void)t; (void)px; if (pn) *pn = 0; return S_OK; }
static BOOL    __stdcall e_IsHeadset(xhv *t, DWORD u) { (void)t; (void)u; return FALSE; }
static BOOL    __stdcall e_IsLocalTalking(xhv *t, DWORD u) { (void)t; (void)u; return FALSE; }
static BOOL    __stdcall e_IsRemoteTalking(xhv *t, XUID x) { (void)t; (void)x; return FALSE; }
static DWORD   __stdcall e_GetDataReady(xhv *t) { (void)t; return 0; }
static HRESULT __stdcall e_GetLocalChat(xhv *t, DWORD u, BYTE *pb, DWORD *pcb, DWORD *pcp)
{
    (void)t; (void)u; (void)pb;
    if (pcb) *pcb = 0;
    if (pcp) *pcp = 0;
    return E_PENDING;
}
static HRESULT __stdcall e_SetPlaybackPriority(xhv *t, XUID x, DWORD u, DWORD p) { (void)t; (void)x; (void)u; (void)p; return S_OK; }
static HRESULT __stdcall e_SubmitIncoming(xhv *t, XUID x, const BYTE *pb, DWORD *pcb) { (void)t; (void)x; (void)pb; (void)pcb; return S_OK; }

static const void *const g_vt[] = {
    e_AddRef, e_Release, e_Lock,                          /* 0x00 0x04 0x08 */
    e_StartLocal, e_StopLocal, e_StartRemote, e_StopRemote,/* 0x0c 0x10 0x14 0x18 */
    e_SetMaxDecode,                                        /* 0x1c */
    e_RegLocal, e_UnregLocal, e_RegRemote, e_UnregRemote,  /* 0x20 0x24 0x28 0x2c */
    e_GetRemoteTalkers, e_IsHeadset, e_IsLocalTalking,     /* 0x30 0x34 0x38 */
    e_IsRemoteTalking, e_GetDataReady, e_GetLocalChat,     /* 0x3c 0x40 0x44 */
    e_SetPlaybackPriority, e_SubmitIncoming,               /* 0x48 0x4c */
};
static xhv g_engine = { g_vt, 1 };

HRESULT XAPI XHVCreateEngine(void *pParams, HANDLE *phWorkerThread, void **ppEngine)
{
    (void)pParams;
    if (phWorkerThread) *phWorkerThread = NULL;
    if (!ppEngine) return E_POINTER;
    *ppEngine = &g_engine;
    xl_log("XHVCreateEngine -> silent engine");
    return S_OK;
}
