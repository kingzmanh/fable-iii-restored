/* Fable III Restored - downloadable content.
 * DLC lives in <game>\DLC\<ContentID hex>\content\ (the installer unpacks Microsoft's original
 * packages there). Every package found is reported as installed and fully licensed, so all
 * premium packs unlock without editing game files or per-user license files.
 *
 * Game-side facts (Fable3.exe, see docs/RE_NOTES.md):
 *  - XLiveContentCreateEnumerator(cItems, pRetrievalInfo{ver=1,..,+0x18 titleId,+0x1c type=2}, pcb, ph)
 *    returns a Win32 error; the game requires cb <= cItems*32.
 *  - per item: XLiveContentGetPath(user, item, path, pcch) then appends "\content",
 *    XLiveContentCreateAccessHandle(user, item, 1, pbuf(28 bytes), 0, &h, NULL),
 *    then reads the 28-byte license from the PBuffer; dword +0x18 = premium content mask. */
#include "xl.h"

#define CONTENT_ITEM_SIZE 32
typedef struct {
    DWORD dwContentAPIVersion;
    DWORD dwTitleID;
    DWORD dwContentType;
    BYTE  abContentID[20];
} content_item;   /* 32 bytes, opaque to the game: it hands it back to our calls */

#define TITLE_ID 0x4D53090A

typedef struct { content_item it; wchar_t dir[MAX_PATH]; } content_pkg;
static content_pkg g_pkgs[32];
static int g_npkgs = -1;

static int hexval(wchar_t c)
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

static void scan_packages(void)
{
    if (g_npkgs >= 0) return;
    g_npkgs = 0;
    wchar_t pat[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%ls\\DLC\\*", xl_root_dir());
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) { xl_log("DLC: no DLC folder"); return; }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || wcslen(fd.cFileName) != 40) continue;
        content_pkg *p = &g_pkgs[g_npkgs];
        int ok = 1;
        for (int i = 0; i < 20 && ok; i++) {
            int hi = hexval(fd.cFileName[i * 2]), lo = hexval(fd.cFileName[i * 2 + 1]);
            if (hi < 0 || lo < 0) ok = 0; else p->it.abContentID[i] = (BYTE)(hi << 4 | lo);
        }
        if (!ok) continue;
        swprintf(p->dir, MAX_PATH, L"%ls\\DLC\\%ls", xl_root_dir(), fd.cFileName);
        wchar_t chk[MAX_PATH];
        swprintf(chk, MAX_PATH, L"%ls\\content", p->dir);
        if (GetFileAttributesW(chk) == INVALID_FILE_ATTRIBUTES) { xl_log("DLC: %ls has no content folder, skipped", fd.cFileName); continue; }
        p->it.dwContentAPIVersion = 1;
        p->it.dwTitleID = TITLE_ID;
        p->it.dwContentType = 2;
        xl_log("DLC: package %ls", fd.cFileName);
        if (++g_npkgs == 32) break;
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

static content_pkg *find_pkg(const void *item)
{
    scan_packages();
    if (!item) return NULL;
    const content_item *ci = item;
    for (int i = 0; i < g_npkgs; i++)
        if (!memcmp(g_pkgs[i].it.abContentID, ci->abContentID, 20)) return &g_pkgs[i];
    return NULL;
}

static DWORD content_enum_fn(xl_handle *h, void *buf, DWORD cb, DWORD *pc)
{
    *pc = 0;
    if (h->pos >= h->count) return ERROR_NO_MORE_FILES;
    DWORD n = 0;
    while (h->pos < h->count && (n + 1) * CONTENT_ITEM_SIZE <= cb) {
        memcpy((BYTE *)buf + n * CONTENT_ITEM_SIZE, &g_pkgs[h->pos].it, CONTENT_ITEM_SIZE);
        n++; h->pos++;
    }
    *pc = n;
    return n ? ERROR_SUCCESS : ERROR_INSUFFICIENT_BUFFER;
}

DWORD XAPI XLiveContentCreateEnumerator(DWORD cItems, const DWORD *pRetrievalInfo, DWORD *pcbBuffer, HANDLE *phContent)
{
    scan_packages();
    DWORD type = pRetrievalInfo ? pRetrievalInfo[7] : 2;
    xl_log("XLiveContentCreateEnumerator(cItems %lu, type %lu) -> %d packages", cItems, type, g_npkgs);
    if (pcbBuffer) *pcbBuffer = cItems * CONTENT_ITEM_SIZE;
    if (!phContent) return ERROR_INVALID_PARAMETER;
    HANDLE hh = xl_handle_new(1);
    xl_handle *h = xl_handle_get(hh);
    h->enumerate = content_enum_fn;
    h->count = (type == 2) ? (DWORD)g_npkgs : 0;
    *phContent = hh;
    return ERROR_SUCCESS;
}

HRESULT XAPI XLiveContentGetPath(DWORD dwUserIndex, const void *pContentInfo, LPWSTR wszPath, DWORD *pcchPath)
{
    (void)dwUserIndex;
    content_pkg *p = find_pkg(pContentInfo);
    if (!p || !pcchPath) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    DWORD need = (DWORD)wcslen(p->dir) + 1;
    if (!wszPath || *pcchPath < need) { *pcchPath = need; return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER); }
    wcscpy(wszPath, p->dir);
    *pcchPath = need;
    xl_log("XLiveContentGetPath -> %ls", wszPath);
    return S_OK;
}

HRESULT XAPI XLiveContentGetDisplayName(DWORD dwUserIndex, const void *pContentInfo, LPWSTR wszDisplayName, DWORD *pcchDisplayName)
{
    (void)dwUserIndex;
    content_pkg *p = find_pkg(pContentInfo);
    if (!p || !pcchDisplayName) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    /* name = the <name> of the package's collection info if present, else the content id */
    wchar_t name[128];
    const wchar_t *id = wcsrchr(p->dir, L'\\') + 1;
    lstrcpynW(name, id, 128);
    DWORD need = (DWORD)wcslen(name) + 1;
    if (!wszDisplayName || *pcchDisplayName < need) { *pcchDisplayName = need; return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER); }
    wcscpy(wszDisplayName, name);
    *pcchDisplayName = need;
    return S_OK;
}

BOOL xl_pbuffer_write(void *p, DWORD off, const void *src, DWORD n);

HRESULT XAPI XLiveContentCreateAccessHandle(DWORD dwUserIndex, const void *pContentInfo, DWORD dwLicenseInfoVersion,
                                            void *pLicenseInfo, DWORD dwOffset, HANDLE *phAccess, XOVERLAPPED *pOverlapped)
{
    (void)dwUserIndex;
    content_pkg *p = find_pkg(pContentInfo);
    if (!p) { xl_log("XLiveContentCreateAccessHandle: unknown package"); return HRESULT_FROM_WIN32(ERROR_NOT_FOUND); }
    if (pLicenseInfo) {
        /* 28-byte license: everything licensed to this profile, premium mask = all bits */
        DWORD lic[7] = { dwLicenseInfoVersion, 0, 0, 0, 0, 0, 0xFFFFFFFF };
        XUID x = xl_xuid();
        memcpy(&lic[1], &x, 8);
        xl_pbuffer_write(pLicenseInfo, dwOffset, lic, sizeof lic);
    }
    if (phAccess) *phAccess = xl_handle_new(4);
    xl_log("XLiveContentCreateAccessHandle(%ls) -> licensed", wcsrchr(p->dir, L'\\') + 1);
    if (pOverlapped) { xl_complete(pOverlapped, ERROR_SUCCESS, 0); return HRESULT_FROM_WIN32(ERROR_IO_PENDING); }
    return S_OK;
}
