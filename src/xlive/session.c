/* Fable III Restored - sessions, invites, title/user storage.
 * Sessions are tracked locally; co-op peers are reached directly (see net.c, coop.c). */
#include "xl.h"

typedef struct {
    DWORD flags, user, maxPublic, maxPrivate;
    XSESSION_INFO info;
    int   started;
    XUID  members[8];
    int   nmembers;
} session_t;

void xl_fill_xnaddr(XNADDR *a);
void xl_remember_xnaddr(const XNADDR *a);
int  xl_coop_search(XSESSION_SEARCHRESULT *out, int max);       /* coop.c */
void xl_coop_hosting(const XSESSION_INFO *info, int open);       /* coop.c */
int  xl_coop_take_invite(XINVITE_INFO *inv);                     /* coop.c */

#define XSESSION_CREATE_HOST 0x00000001

DWORD XAPI XSessionCreate(DWORD dwFlags, DWORD dwUserIndex, DWORD dwMaxPublicSlots, DWORD dwMaxPrivateSlots,
                          ULONGLONG *pqwSessionNonce, XSESSION_INFO *pSessionInfo, XOVERLAPPED *pOverlapped, HANDLE *phEnum)
{
    session_t *s = calloc(1, sizeof *s);
    s->flags = dwFlags; s->user = dwUserIndex; s->maxPublic = dwMaxPublicSlots; s->maxPrivate = dwMaxPrivateSlots;
    if (pqwSessionNonce && !*pqwSessionNonce) XNetRandom((BYTE *)pqwSessionNonce, 8);
    if (dwFlags & XSESSION_CREATE_HOST) {
        XNetCreateKey(&s->info.sessionID, &s->info.keyExchangeKey);
        xl_fill_xnaddr(&s->info.hostAddress);
        if (pSessionInfo) *pSessionInfo = s->info;
        xl_coop_hosting(&s->info, 1);
    } else if (pSessionInfo) {
        s->info = *pSessionInfo;   /* joining: the game passes the host's info */
        xl_remember_xnaddr(&pSessionInfo->hostAddress);
    }
    HANDLE hh = xl_handle_new(3);
    xl_handle_get(hh)->data = s;
    if (phEnum) *phEnum = hh;
    xl_log("XSessionCreate(flags %08lX, user %lu, pub %lu, priv %lu) host %s", dwFlags, dwUserIndex, dwMaxPublicSlots,
           dwMaxPrivateSlots, inet_ntoa(s->info.hostAddress.ina));
    return xl_complete(pOverlapped, ERROR_SUCCESS, 0);
}

static session_t *sess(HANDLE h) { xl_handle *x = xl_handle_get(h); return (x && x->kind == 3) ? x->data : NULL; }

DWORD XAPI XSessionDelete(HANDLE hSession, XOVERLAPPED *ov)
{
    session_t *s = sess(hSession);
    if (s && (s->flags & XSESSION_CREATE_HOST)) xl_coop_hosting(&s->info, 0);
    xl_log("XSessionDelete");
    return xl_complete(ov, ERROR_SUCCESS, 0);
}
DWORD XAPI XSessionStart(HANDLE hSession, DWORD dwFlags, XOVERLAPPED *ov) { session_t *s = sess(hSession); (void)dwFlags; if (s) s->started = 1; return xl_complete(ov, ERROR_SUCCESS, 0); }
DWORD XAPI XSessionEnd(HANDLE hSession, XOVERLAPPED *ov) { session_t *s = sess(hSession); if (s) s->started = 0; return xl_complete(ov, ERROR_SUCCESS, 0); }
DWORD XAPI XSessionModify(HANDLE hSession, DWORD dwFlags, DWORD maxPub, DWORD maxPriv, XOVERLAPPED *ov)
{
    session_t *s = sess(hSession);
    if (s) { s->flags = (s->flags & XSESSION_CREATE_HOST) | (dwFlags & ~XSESSION_CREATE_HOST); s->maxPublic = maxPub; s->maxPrivate = maxPriv; }
    xl_log("XSessionModify(flags %08lX pub %lu priv %lu)", dwFlags, maxPub, maxPriv);
    return xl_complete(ov, ERROR_SUCCESS, 0);
}
DWORD XAPI XSessionMigrateHost(HANDLE h, DWORD u, XSESSION_INFO *pInfo, XOVERLAPPED *ov)
{
    session_t *s = sess(h); (void)u;
    if (s) { xl_fill_xnaddr(&s->info.hostAddress); if (pInfo) *pInfo = s->info; }
    return xl_complete(ov, ERROR_SUCCESS, 0);
}
DWORD XAPI XSessionJoinLocal(HANDLE h, DWORD n, const DWORD *idx, const BOOL *priv, XOVERLAPPED *ov) { (void)h; (void)n; (void)idx; (void)priv; return xl_complete(ov, ERROR_SUCCESS, 0); }
DWORD XAPI XSessionLeaveLocal(HANDLE h, DWORD n, const DWORD *idx, XOVERLAPPED *ov) { (void)h; (void)n; (void)idx; return xl_complete(ov, ERROR_SUCCESS, 0); }
DWORD XAPI XSessionJoinRemote(HANDLE h, DWORD n, const XUID *x, const BOOL *priv, XOVERLAPPED *ov)
{
    session_t *s = sess(h); (void)priv;
    for (DWORD i = 0; s && i < n && s->nmembers < 8; i++) s->members[s->nmembers++] = x[i];
    xl_log("XSessionJoinRemote(%lu)", n);
    return xl_complete(ov, ERROR_SUCCESS, 0);
}
DWORD XAPI XSessionLeaveRemote(HANDLE h, DWORD n, const XUID *x, XOVERLAPPED *ov) { (void)h; (void)n; (void)x; return xl_complete(ov, ERROR_SUCCESS, 0); }
DWORD XAPI XSessionWriteStats(HANDLE h, XUID x, DWORD n, const void *v, XOVERLAPPED *ov) { (void)h; (void)x; (void)n; (void)v; return xl_complete(ov, ERROR_SUCCESS, 0); }
DWORD XAPI XSessionFlushStats(HANDLE h, XOVERLAPPED *ov) { (void)h; return xl_complete(ov, ERROR_SUCCESS, 0); }
DWORD XAPI XSessionArbitrationRegister(HANDLE h, DWORD f, ULONGLONG nonce, DWORD *pcb, void *res, XOVERLAPPED *ov)
{
    (void)h; (void)f; (void)nonce; (void)res;
    if (pcb && !res) { *pcb = 16; return ERROR_INSUFFICIENT_BUFFER; }
    return xl_complete(ov, ERROR_SUCCESS, 0);
}

/* XSESSION_LOCAL_DETAILS (subset): userIndexHost, gameType, gameMode, flags, maxPub, maxPriv, availPub, availPriv,
 * actualMembers, returnedMembers, eState, qwNonce, sessionInfo, xnkidArbitration, pSessionMembers */
DWORD XAPI XSessionGetDetails(HANDLE h, DWORD *pcb, BYTE *pDetails, XOVERLAPPED *ov)
{
    session_t *s = sess(h);
    DWORD need = 0x70;
    if (!pcb) return ERROR_INVALID_PARAMETER;
    if (!pDetails || *pcb < need) { *pcb = need; return ERROR_INSUFFICIENT_BUFFER; }
    memset(pDetails, 0, need);
    if (s) {
        DWORD *d = (DWORD *)pDetails;
        d[0] = s->user; d[3] = s->flags; d[4] = s->maxPublic; d[5] = s->maxPrivate;
        d[6] = s->maxPublic; d[7] = s->maxPrivate; d[8] = 1 + s->nmembers; d[10] = s->started ? 2 : 1;
        memcpy(pDetails + 0x30, &s->info, sizeof s->info);
    }
    return xl_complete(ov, ERROR_SUCCESS, 0);
}

static DWORD do_search(DWORD dwNumResults, DWORD *pcbResultsBuffer, XSESSION_SEARCHRESULT_HEADER *pResults, XOVERLAPPED *ov)
{
    DWORD need = sizeof(XSESSION_SEARCHRESULT_HEADER) + dwNumResults * sizeof(XSESSION_SEARCHRESULT);
    if (!pcbResultsBuffer) return ERROR_INVALID_PARAMETER;
    if (!pResults || *pcbResultsBuffer < need) { *pcbResultsBuffer = need; return ERROR_INSUFFICIENT_BUFFER; }
    XSESSION_SEARCHRESULT *r = (XSESSION_SEARCHRESULT *)(pResults + 1);
    int n = xl_coop_search(r, (int)dwNumResults);
    pResults->dwSearchResults = (DWORD)n;
    pResults->pResults = r;
    for (int i = 0; i < n; i++) xl_remember_xnaddr(&r[i].info.hostAddress);
    xl_log("XSessionSearch -> %d results", n);
    return xl_complete(ov, ERROR_SUCCESS, 0);
}

DWORD XAPI XSessionSearch(DWORD dwProcedureIndex, DWORD dwUserIndex, DWORD dwNumResults, WORD wNumProperties, WORD wNumContexts,
                          void *pSearchProperties, void *pSearchContexts, DWORD *pcbResultsBuffer,
                          XSESSION_SEARCHRESULT_HEADER *pSearchResults, XOVERLAPPED *pOverlapped)
{
    (void)dwProcedureIndex; (void)dwUserIndex; (void)wNumProperties; (void)wNumContexts; (void)pSearchProperties; (void)pSearchContexts;
    return do_search(dwNumResults, pcbResultsBuffer, pSearchResults, pOverlapped);
}
DWORD XAPI XSessionSearchEx(DWORD dwProcedureIndex, DWORD dwUserIndex, DWORD dwNumResults, DWORD dwNumUsers, WORD wNumProperties,
                            WORD wNumContexts, void *pSearchProperties, void *pSearchContexts, DWORD *pcbResultsBuffer,
                            XSESSION_SEARCHRESULT_HEADER *pSearchResults, XOVERLAPPED *pOverlapped)
{
    (void)dwProcedureIndex; (void)dwUserIndex; (void)dwNumUsers; (void)wNumProperties; (void)wNumContexts; (void)pSearchProperties; (void)pSearchContexts;
    return do_search(dwNumResults, pcbResultsBuffer, pSearchResults, pOverlapped);
}
DWORD XAPI XSessionSearchByID(XNKID sessionID, DWORD dwUserIndex, DWORD *pcbResultsBuffer, XSESSION_SEARCHRESULT_HEADER *pSearchResults, XOVERLAPPED *pOverlapped)
{
    (void)sessionID; (void)dwUserIndex;
    return do_search(1, pcbResultsBuffer, pSearchResults, pOverlapped);
}

/* ---------------- invites ---------------- */
DWORD XAPI XInviteGetAcceptedInfo(DWORD dwUserIndex, XINVITE_INFO *pInfo)
{
    (void)dwUserIndex;
    if (!pInfo) return ERROR_INVALID_PARAMETER;
    if (xl_coop_take_invite(pInfo)) { xl_remember_xnaddr(&pInfo->hostInfo.hostAddress); xl_log("invite accepted -> %s", inet_ntoa(pInfo->hostInfo.hostAddress.ina)); return ERROR_SUCCESS; }
    return ERROR_NOT_FOUND;
}
DWORD XAPI XInviteSend(DWORD u, DWORD n, const XUID *x, LPCWSTR t, XOVERLAPPED *ov) { (void)u; (void)n; (void)x; (void)t; return xl_complete(ov, ERROR_SUCCESS, 0); }

/* ---------------- LIVE storage: nothing online; files simply don't exist ---------------- */
#define XONLINE_E_STORAGE_FILE_NOT_FOUND 0x80151903
DWORD XAPI XStorageBuildServerPath(DWORD u, DWORD fac, const void *pv, DWORD cb, LPCWSTR name, WCHAR *out, DWORD *pcch)
{
    (void)u; (void)fac; (void)pv; (void)cb;
    WCHAR tmp[260];
    swprintf(tmp, 260, L"local://fable3/%ls", name ? name : L"");
    DWORD need = (DWORD)wcslen(tmp) + 1;
    if (!pcch) return ERROR_INVALID_PARAMETER;
    if (!out || *pcch < need) { *pcch = need; return ERROR_INSUFFICIENT_BUFFER; }
    wcscpy(out, tmp); *pcch = need;
    return ERROR_SUCCESS;
}
DWORD XAPI XStorageBuildServerPathByXuid(XUID x, DWORD fac, const void *pv, DWORD cb, LPCWSTR name, WCHAR *out, DWORD *pcch)
{
    (void)x;
    return XStorageBuildServerPath(0, fac, pv, cb, name, out, pcch);
}
DWORD XAPI XStorageDownloadToMemory(DWORD u, LPCWSTR path, DWORD cb, BYTE *buf, DWORD cbRes, void *res, XOVERLAPPED *ov)
{
    (void)u; (void)path; (void)cb; (void)buf; (void)cbRes; (void)res;
    return xl_complete(ov, XONLINE_E_STORAGE_FILE_NOT_FOUND, 0);
}
DWORD XAPI XStorageUploadFromMemory(DWORD u, LPCWSTR path, DWORD cb, const BYTE *buf, XOVERLAPPED *ov)
{
    (void)u; (void)path; (void)cb; (void)buf;
    return xl_complete(ov, ERROR_SUCCESS, 0);
}
DWORD XAPI XStorageEnumerate(DWORD u, LPCWSTR path, DWORD start, DWORD max, DWORD cbRes, BYTE *res, XOVERLAPPED *ov)
{
    (void)u; (void)path; (void)start; (void)max;
    if (res && cbRes >= 12) memset(res, 0, 12);  /* XSTORAGE_ENUMERATE_RESULTS: 0 items */
    return xl_complete(ov, ERROR_SUCCESS, 0);
}
