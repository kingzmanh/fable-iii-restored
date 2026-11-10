/* Fable III Restored - the local profile: sign-in, profile settings (persisted), achievements, stats. */
#include "xl.h"

static int signed_in(DWORD u) { return u == 0; }

DWORD XAPI XUserGetSigninState(DWORD dwUserIndex)
{
    return signed_in(dwUserIndex) ? eXUserSigninState_SignedInToLive : eXUserSigninState_NotSignedIn;
}

DWORD XAPI XUserGetXUID(DWORD dwUserIndex, XUID *pxuid)
{
    if (!signed_in(dwUserIndex)) return ERROR_NO_SUCH_USER;
    if (pxuid) *pxuid = xl_xuid();
    return ERROR_SUCCESS;
}

DWORD XAPI XUserGetSigninInfo(DWORD dwUserIndex, DWORD dwFlags, XUSER_SIGNIN_INFO *pInfo)
{
    (void)dwFlags;
    if (!pInfo) return ERROR_INVALID_PARAMETER;
    memset(pInfo, 0, sizeof *pInfo);
    if (!signed_in(dwUserIndex)) return ERROR_NO_SUCH_USER;
    pInfo->xuid = xl_xuid();
    pInfo->dwInfoFlags = XUSER_INFO_FLAG_LIVE_ENABLED;
    pInfo->UserSigninState = eXUserSigninState_SignedInToLive;
    pInfo->dwSponsorUserIndex = 0xFF;  /* XUSER_INDEX_NONE */
    strncpy(pInfo->szUserName, xl_user_name(), XUSER_NAME_SIZE - 1);
    return ERROR_SUCCESS;
}

DWORD XAPI XUserGetName(DWORD dwUserIndex, LPSTR szUserName, DWORD cchUserName)
{
    if (!signed_in(dwUserIndex)) return ERROR_NO_SUCH_USER;
    if (szUserName && cchUserName) lstrcpynA(szUserName, xl_user_name(), (int)cchUserName);
    return ERROR_SUCCESS;
}

DWORD XAPI XUserCheckPrivilege(DWORD dwUserIndex, DWORD privilege, PBOOL pfResult)
{
    (void)privilege;
    if (pfResult) *pfResult = signed_in(dwUserIndex);
    return signed_in(dwUserIndex) ? ERROR_SUCCESS : ERROR_NO_SUCH_USER;
}

DWORD XAPI XUserAreUsersFriends(DWORD dwUserIndex, const XUID *pXuids, DWORD dwXuidCount, PBOOL pfResult, XOVERLAPPED *pOverlapped)
{
    (void)dwUserIndex; (void)pXuids; (void)dwXuidCount;
    /* everybody who reaches us through our co-op connection is a friend */
    if (pfResult) *pfResult = TRUE;
    return xl_complete(pOverlapped, ERROR_SUCCESS, 0);
}

DWORD XAPI XUserMuteListQuery(DWORD dwUserIndex, XUID xuidRemote, PBOOL pfOnMuteList)
{
    (void)dwUserIndex; (void)xuidRemote;
    if (pfOnMuteList) *pfOnMuteList = FALSE;
    return ERROR_SUCCESS;
}

DWORD XAPI XUserSetContext(DWORD dwUserIndex, DWORD dwContextId, DWORD dwContextValue)
{
    (void)dwUserIndex; (void)dwContextId; (void)dwContextValue;
    return ERROR_SUCCESS;
}

DWORD XAPI XUserSetProperty(DWORD dwUserIndex, DWORD dwPropertyId, DWORD cbValue, const void *pvValue)
{
    (void)dwUserIndex; (void)dwPropertyId; (void)cbValue; (void)pvValue;
    return ERROR_SUCCESS;
}

DWORD XAPI XPresenceInitialize(DWORD cPeersToSubscribe) { (void)cPeersToSubscribe; return ERROR_SUCCESS; }

/* ---- empty enumerators (friends, marketplace offers, storage listings) ---- */
static DWORD enum_empty(xl_handle *h, void *buf, DWORD cb, DWORD *pc) { (void)h; (void)buf; (void)cb; *pc = 0; return ERROR_NO_MORE_FILES; }

HANDLE xl_empty_enum(void)
{
    HANDLE hh = xl_handle_new(1);
    xl_handle_get(hh)->enumerate = enum_empty;
    return hh;
}

DWORD XAPI XFriendsCreateEnumerator(DWORD dwUserIndex, DWORD dwStartingIndex, DWORD dwFriendsToReturn, DWORD *pcbBuffer, HANDLE *ph)
{
    (void)dwUserIndex; (void)dwStartingIndex;
    if (pcbBuffer) *pcbBuffer = dwFriendsToReturn * 0xC4; /* sizeof(XONLINE_FRIEND) */
    if (ph) *ph = xl_empty_enum();
    return ERROR_SUCCESS;
}

DWORD XAPI XMarketplaceCreateOfferEnumerator(DWORD u, DWORD offerType, DWORD cats, DWORD cItem, DWORD *pcb, HANDLE *ph)
{
    (void)u; (void)offerType; (void)cats;
    if (pcb) *pcb = cItem * 0x48;
    if (ph) *ph = xl_empty_enum();
    return ERROR_SUCCESS;
}

DWORD XAPI XMarketplaceCreateOfferEnumeratorByOffering(DWORD u, DWORD cItem, const ULONGLONG *pOffers, WORD cOffers, DWORD *pcb, HANDLE *ph)
{
    (void)u; (void)pOffers; (void)cOffers;
    if (pcb) *pcb = cItem * 0x48;
    if (ph) *ph = xl_empty_enum();
    return ERROR_SUCCESS;
}

/* all content is already installed */
DWORD XAPI XMarketplaceGetDownloadStatus(DWORD u, ULONGLONG offer, DWORD *pdwResult)
{
    (void)u; (void)offer;
    if (pdwResult) *pdwResult = S_OK;
    return ERROR_SUCCESS;
}

/* ---- gamer pictures: none (game falls back to its default picture) ---- */
DWORD XAPI XUserReadGamerPicture(DWORD u, BOOL fSmall, PBYTE pb, DWORD pitch, DWORD height, XOVERLAPPED *ov)
{
    (void)u; (void)fSmall; (void)pb; (void)pitch; (void)height;
    return xl_complete(ov, ERROR_NOT_FOUND, 0);
}
DWORD XAPI XUserReadGamerpictureByKey(const void *key, BOOL fSmall, PBYTE pb, DWORD pitch, DWORD height, XOVERLAPPED *ov)
{
    (void)key; (void)fSmall; (void)pb; (void)pitch; (void)height;
    return xl_complete(ov, ERROR_NOT_FOUND, 0);
}

/* ---------------- profile settings (persisted per profile) ----------------
 * File: <data>\profile_<xuid>.bin = repeated { DWORD id; DWORD len; BYTE data[len] }.
 * GFWL lost the game's own settings (controls/audio/misc) for many players; these persist. */
typedef struct setting { DWORD id; DWORD len; BYTE *data; struct setting *next; } setting;
static setting *g_settings;
static CRITICAL_SECTION g_set_cs;
static int g_set_loaded;

static void settings_path(wchar_t *out)
{
    swprintf(out, MAX_PATH, L"%ls\\profile_%016llX.bin", xl_data_dir(), (unsigned long long)xl_xuid());
}

static void settings_load(void)
{
    if (g_set_loaded) return;
    InitializeCriticalSection(&g_set_cs);
    g_set_loaded = 1;
    wchar_t p[MAX_PATH]; settings_path(p);
    FILE *f = _wfopen(p, L"rb");
    if (!f) return;
    DWORD hd[2];
    while (fread(hd, 8, 1, f) == 1 && hd[1] < 0x100000) {
        setting *s = calloc(1, sizeof *s);
        s->id = hd[0]; s->len = hd[1];
        s->data = malloc(s->len ? s->len : 1);
        if (fread(s->data, 1, s->len, f) != s->len) { free(s->data); free(s); break; }
        s->next = g_settings; g_settings = s;
    }
    fclose(f);
}

static void settings_save(void)
{
    wchar_t p[MAX_PATH], tmp[MAX_PATH];
    settings_path(p);
    swprintf(tmp, MAX_PATH, L"%ls.tmp", p);
    FILE *f = _wfopen(tmp, L"wb");
    if (!f) return;
    for (setting *s = g_settings; s; s = s->next) {
        DWORD hd[2] = { s->id, s->len };
        fwrite(hd, 8, 1, f);
        fwrite(s->data, 1, s->len, f);
    }
    fclose(f);
    MoveFileExW(tmp, p, MOVEFILE_REPLACE_EXISTING);
}

static setting *settings_find(DWORD id)
{
    for (setting *s = g_settings; s; s = s->next) if (s->id == id) return s;
    return NULL;
}

/* bytes of payload a setting needs beyond the XUSER_PROFILE_SETTING itself */
static DWORD setting_extra(DWORD id)
{
    DWORD t = XPROFILE_SETTING_TYPE(id);
    if (t == XUSER_DATA_TYPE_UNICODE || t == XUSER_DATA_TYPE_BINARY) return (XPROFILE_SETTING_SIZE(id) + 7) & ~7u;
    return 0;
}

static DWORD read_settings(DWORD dwUserIndex, XUID xuid, DWORD dwNumSettingIds, const DWORD *pdwSettingIds,
                           DWORD *pcbResults, XUSER_READ_PROFILE_SETTING_RESULT *pResults, int byXuid)
{
    settings_load();
    DWORD need = sizeof(XUSER_READ_PROFILE_SETTING_RESULT) + dwNumSettingIds * sizeof(XUSER_PROFILE_SETTING);
    for (DWORD i = 0; i < dwNumSettingIds; i++) need += setting_extra(pdwSettingIds[i]);
    if (!pcbResults) return ERROR_INVALID_PARAMETER;
    if (!pResults || *pcbResults < need) { *pcbResults = need; return ERROR_INSUFFICIENT_BUFFER; }
    memset(pResults, 0, need);
    XUSER_PROFILE_SETTING *ps = (XUSER_PROFILE_SETTING *)(pResults + 1);
    BYTE *extra = (BYTE *)(ps + dwNumSettingIds);
    pResults->dwSettingsLen = dwNumSettingIds;
    pResults->pSettings = ps;
    EnterCriticalSection(&g_set_cs);
    for (DWORD i = 0; i < dwNumSettingIds; i++) {
        DWORD id = pdwSettingIds[i];
        XUSER_PROFILE_SETTING *o = &ps[i];
        o->dwSettingId = id;
        if (byXuid) o->user.xuid = xuid; else o->user.dwUserIndex = dwUserIndex;
        o->data.type = (BYTE)XPROFILE_SETTING_TYPE(id);
        setting *s = settings_find(id);
        DWORD t = XPROFILE_SETTING_TYPE(id);
        o->source = s ? XSOURCE_TITLE : XSOURCE_DEFAULT;
        if (t == XUSER_DATA_TYPE_BINARY) {
            o->data.binary.pbData = extra;
            o->data.binary.cbData = 0;
            if (s) { DWORD n = min(s->len, XPROFILE_SETTING_SIZE(id)); memcpy(extra, s->data, n); o->data.binary.cbData = n; }
            else o->source = XSOURCE_NO_VALUE;
            extra += setting_extra(id);
        } else if (t == XUSER_DATA_TYPE_UNICODE) {
            o->data.string.pwszData = (LPWSTR)extra;
            o->data.string.cbData = 2;
            if (s) { DWORD n = min(s->len, XPROFILE_SETTING_SIZE(id)); memcpy(extra, s->data, n); o->data.string.cbData = n; }
            extra += setting_extra(id);
        } else if (s) {
            memcpy(&o->data.i64Data, s->data, min(s->len, 8u));
        } else {
            /* sensible LIVE defaults for the common system settings */
            switch (XPROFILE_SETTING_ID(id)) {
            case 0x0002: o->data.nData = 0; break;   /* gamer type */
            case 0x0003: o->data.nData = 0; break;   /* yaxis inversion off */
            case 0x0018: o->data.nData = 1; break;   /* controller vibration on */
            default: o->data.i64Data = 0; break;
            }
        }
    }
    LeaveCriticalSection(&g_set_cs);
    *pcbResults = need;
    return ERROR_SUCCESS;
}

DWORD XAPI XUserReadProfileSettings(DWORD dwTitleId, DWORD dwUserIndex, DWORD dwNumSettingIds, const DWORD *pdwSettingIds,
                                    DWORD *pcbResults, XUSER_READ_PROFILE_SETTING_RESULT *pResults, XOVERLAPPED *pOverlapped)
{
    (void)dwTitleId;
    DWORD r = read_settings(dwUserIndex, 0, dwNumSettingIds, pdwSettingIds, pcbResults, pResults, 0);
    xl_log("XUserReadProfileSettings(user %lu, %lu ids) -> %lu (cb %lu)", dwUserIndex, dwNumSettingIds, r, pcbResults ? *pcbResults : 0);
    if (r == ERROR_INSUFFICIENT_BUFFER) return r;   /* size query is always synchronous */
    return xl_complete(pOverlapped, r, 0);
}

DWORD XAPI XUserReadProfileSettingsByXuid(DWORD dwTitleId, DWORD dwUserIndexRequester, DWORD dwNumFor, const XUID *pxuidFor,
                                          DWORD dwNumSettingIds, const DWORD *pdwSettingIds, DWORD *pcbResults,
                                          XUSER_READ_PROFILE_SETTING_RESULT *pResults, XOVERLAPPED *pOverlapped)
{
    (void)dwTitleId;
    XUID x = (dwNumFor && pxuidFor) ? pxuidFor[0] : xl_xuid();
    DWORD r = read_settings(dwUserIndexRequester, x, dwNumSettingIds, pdwSettingIds, pcbResults, pResults, 1);
    if (r == ERROR_INSUFFICIENT_BUFFER) return r;
    return xl_complete(pOverlapped, r, 0);
}

DWORD XAPI XUserWriteProfileSettings(DWORD dwUserIndex, DWORD dwNumSettings, const XUSER_PROFILE_SETTING *pSettings, XOVERLAPPED *pOverlapped)
{
    settings_load();
    EnterCriticalSection(&g_set_cs);
    for (DWORD i = 0; i < dwNumSettings; i++) {
        const XUSER_PROFILE_SETTING *in = &pSettings[i];
        DWORD id = in->dwSettingId, t = XPROFILE_SETTING_TYPE(id);
        const void *src; DWORD n;
        if (t == XUSER_DATA_TYPE_BINARY) { src = in->data.binary.pbData; n = in->data.binary.cbData; }
        else if (t == XUSER_DATA_TYPE_UNICODE) { src = in->data.string.pwszData; n = in->data.string.cbData; }
        else { src = &in->data.i64Data; n = 8; }
        if (!src) n = 0;
        setting *s = settings_find(id);
        if (!s) { s = calloc(1, sizeof *s); s->id = id; s->next = g_settings; g_settings = s; }
        free(s->data);
        s->data = malloc(n ? n : 1);
        if (n) memcpy(s->data, src, n);
        s->len = n;
    }
    settings_save();
    LeaveCriticalSection(&g_set_cs);
    xl_log("XUserWriteProfileSettings(user %lu, %lu settings) saved", dwUserIndex, dwNumSettings);
    return xl_complete(pOverlapped, ERROR_SUCCESS, 0);
}

/* ---------------- achievements (kept locally) ---------------- */
static void ach_path(wchar_t *out)
{
    swprintf(out, MAX_PATH, L"%ls\\achievements_%016llX.txt", xl_data_dir(), (unsigned long long)xl_xuid());
}

static int ach_load(DWORD *ids, FILETIME *when, int max)
{
    wchar_t p[MAX_PATH]; ach_path(p);
    FILE *f = _wfopen(p, L"r");
    if (!f) return 0;
    int n = 0; unsigned long id; unsigned long long t;
    while (n < max && fscanf(f, "%lu %llu", &id, &t) == 2) {
        ids[n] = id; when[n].dwLowDateTime = (DWORD)t; when[n].dwHighDateTime = (DWORD)(t >> 32); n++;
    }
    fclose(f);
    return n;
}

void xl_achievement_toast(DWORD id);  /* overlay.c */

DWORD XAPI XUserWriteAchievements(DWORD dwNumAchievements, const XUSER_ACHIEVEMENT *pAchievements, XOVERLAPPED *pOverlapped)
{
    DWORD ids[256]; FILETIME when[256];
    int n = ach_load(ids, when, 256);
    wchar_t p[MAX_PATH]; ach_path(p);
    FILE *f = _wfopen(p, L"a");
    for (DWORD i = 0; i < dwNumAchievements; i++) {
        DWORD id = pAchievements[i].dwAchievementId;
        int have = 0;
        for (int k = 0; k < n; k++) if (ids[k] == id) have = 1;
        if (have) continue;
        FILETIME ft; GetSystemTimeAsFileTime(&ft);
        if (f) fprintf(f, "%lu %llu\n", id, ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime);
        if (n < 256) { ids[n] = id; when[n] = ft; n++; }
        xl_log("achievement unlocked: %lu", id);
        xl_achievement_toast(id);
    }
    if (f) fclose(f);
    return xl_complete(pOverlapped, ERROR_SUCCESS, 0);
}

/* XACHIEVEMENT_DETAILS: id, label, desc, unachieved, imageId, cred, ftAchieved, flags (36 bytes) */
typedef struct {
    DWORD dwId; LPWSTR pwszLabel; LPWSTR pwszDescription; LPWSTR pwszUnachieved;
    DWORD dwImageId; DWORD dwCred; FILETIME ftAchieved; DWORD dwFlags;
} XACHIEVEMENT_DETAILS;
#define XACHIEVEMENT_DETAILS_ACHIEVED_ONLINE 0x00010000
#define XACHIEVEMENT_DETAILS_ACHIEVED        0x00020000

typedef struct { DWORD ids[256]; FILETIME when[256]; } ach_enum;

static DWORD ach_enum_fn(xl_handle *h, void *buf, DWORD cb, DWORD *pc)
{
    ach_enum *e = h->data;
    DWORD per = sizeof(XACHIEVEMENT_DETAILS) + 2;  /* + one shared empty string */
    DWORD fit = cb / per, n = 0;
    *pc = 0;
    if (h->pos >= h->count) return ERROR_NO_MORE_FILES;
    XACHIEVEMENT_DETAILS *d = buf;
    DWORD left = min(fit, h->count - h->pos);
    WCHAR *empty = (WCHAR *)((BYTE *)buf + left * sizeof(XACHIEVEMENT_DETAILS));
    *empty = 0;
    for (; n < left; n++, h->pos++) {
        memset(&d[n], 0, sizeof d[n]);
        d[n].dwId = e->ids[h->pos];
        d[n].pwszLabel = d[n].pwszDescription = d[n].pwszUnachieved = empty;
        d[n].ftAchieved = e->when[h->pos];
        d[n].dwFlags = XACHIEVEMENT_DETAILS_ACHIEVED | XACHIEVEMENT_DETAILS_ACHIEVED_ONLINE;
    }
    *pc = n;
    return ERROR_SUCCESS;
}

static void ach_destroy(xl_handle *h) { free(h->data); }

DWORD XAPI XUserCreateAchievementEnumerator(DWORD dwTitleId, DWORD dwUserIndex, XUID xuid, DWORD dwDetailFlags,
                                            DWORD dwStartingIndex, DWORD cItem, DWORD *pcbBuffer, HANDLE *phEnum)
{
    (void)dwTitleId; (void)dwUserIndex; (void)xuid; (void)dwDetailFlags;
    if (pcbBuffer) *pcbBuffer = cItem * (sizeof(XACHIEVEMENT_DETAILS) + 2) + 2;
    if (!phEnum) return ERROR_INVALID_PARAMETER;
    HANDLE hh = xl_handle_new(1);
    xl_handle *h = xl_handle_get(hh);
    ach_enum *e = calloc(1, sizeof *e);
    int n = ach_load(e->ids, e->when, 256);
    h->data = e;
    h->count = (DWORD)n;
    h->pos = min(dwStartingIndex, (DWORD)n);
    h->enumerate = ach_enum_fn;
    h->destroy = ach_destroy;
    *phEnum = hh;
    return ERROR_SUCCESS;
}

/* ---------------- stats / leaderboards: offline, empty views ---------------- */
typedef struct { DWORD dwViewId; DWORD dwNumColumnIds; WORD rgwColumnIds[64]; } XUSER_STATS_SPEC;
typedef struct { DWORD dwViewId; DWORD dwTotalViewRows; DWORD dwNumRows; void *pRows; } XUSER_STATS_VIEW;
typedef struct { DWORD dwNumViews; XUSER_STATS_VIEW *pViews; } XUSER_STATS_READ_RESULTS;

DWORD XAPI XUserReadStats(DWORD dwTitleId, DWORD dwNumXuids, const XUID *pXuids, DWORD dwNumStatsSpecs,
                          const XUSER_STATS_SPEC *pSpecs, DWORD *pcbResults, XUSER_STATS_READ_RESULTS *pResults, XOVERLAPPED *pOverlapped)
{
    (void)dwTitleId; (void)dwNumXuids; (void)pXuids;
    DWORD need = sizeof(XUSER_STATS_READ_RESULTS) + dwNumStatsSpecs * sizeof(XUSER_STATS_VIEW);
    if (!pcbResults) return ERROR_INVALID_PARAMETER;
    if (!pResults || *pcbResults < need) { *pcbResults = need; return ERROR_INSUFFICIENT_BUFFER; }
    XUSER_STATS_VIEW *v = (XUSER_STATS_VIEW *)(pResults + 1);
    pResults->dwNumViews = dwNumStatsSpecs;
    pResults->pViews = v;
    for (DWORD i = 0; i < dwNumStatsSpecs; i++) { v[i].dwViewId = pSpecs ? pSpecs[i].dwViewId : 0; v[i].dwTotalViewRows = 0; v[i].dwNumRows = 0; v[i].pRows = NULL; }
    return xl_complete(pOverlapped, ERROR_SUCCESS, 0);
}
