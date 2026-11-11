/* Fable III Restored - core: init, logging, config, profile identity, overlapped, handles. */
#include "xl.h"
#include <shlobj.h>
#include <stdarg.h>

static CRITICAL_SECTION g_log_cs;
static FILE   *g_log;
static wchar_t g_root[MAX_PATH];
static wchar_t g_data[MAX_PATH];
static wchar_t g_ini[MAX_PATH];
static XUID    g_xuid;
static char    g_name[XUSER_NAME_SIZE];
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

/* Fable3.exe is relocatable; convert runtime addresses to file VAs (image base 0x400000) */
DWORD xl_va(DWORD a)
{
    static DWORD base, end;
    if (!base) {
        HMODULE m = GetModuleHandleW(NULL);
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)m + ((IMAGE_DOS_HEADER *)m)->e_lfanew);
        base = (DWORD)(ULONG_PTR)m; end = base + nt->OptionalHeader.SizeOfImage;
    }
    return (a >= base && a < end) ? a - base + 0x400000 : a;
}

const wchar_t *xl_root_dir(void) { return g_root; }
const wchar_t *xl_data_dir(void) { return g_data; }
XUID xl_xuid(void) { return g_xuid; }
const char *xl_user_name(void) { return g_name; }

void xl_log(const char *fmt, ...)
{
    if (!g_log) return;
    va_list ap;
    SYSTEMTIME st;
    GetLocalTime(&st);
    EnterCriticalSection(&g_log_cs);
    fprintf(g_log, "%02d:%02d:%02d.%03d [%5lu] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentThreadId());
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_log_cs);
}

int xl_cfg_int(const char *section, const char *key, int def)
{
    wchar_t ws[64], wk[64];
    MultiByteToWideChar(CP_UTF8, 0, section, -1, ws, 64);
    MultiByteToWideChar(CP_UTF8, 0, key, -1, wk, 64);
    return (int)GetPrivateProfileIntW(ws, wk, def, g_ini);
}

void xl_cfg_str(const char *section, const char *key, const char *def, char *out, int cch)
{
    wchar_t ws[64], wk[64], wd[256], wo[256];
    MultiByteToWideChar(CP_UTF8, 0, section, -1, ws, 64);
    MultiByteToWideChar(CP_UTF8, 0, key, -1, wk, 64);
    MultiByteToWideChar(CP_UTF8, 0, def, -1, wd, 256);
    GetPrivateProfileStringW(ws, wk, wd, wo, 256, g_ini);
    WideCharToMultiByte(CP_UTF8, 0, wo, -1, out, cch, NULL, NULL);
    out[cch - 1] = 0;
}

#include "../common/identity.h"

static XUID make_xuid(void)
{
    char cfg[32];
    xl_cfg_str("Profile", "XUID", "", cfg, sizeof cfg);
    if (cfg[0]) {
        XUID x = _strtoui64(cfg, NULL, 16);
        if (x) return x;
    }
    return f3r_default_xuid();
}

static BOOL CALLBACK init_cb(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_log_cs);
    GetModuleFileNameW(NULL, g_root, MAX_PATH);
    wchar_t *s = wcsrchr(g_root, L'\\');
    if (s) *s = 0;
    if (GetEnvironmentVariableW(L"F3R_INI", g_ini, MAX_PATH) == 0)   /* tests use their own settings file */
        swprintf(g_ini, MAX_PATH, L"%ls\\Fable3Restored.ini", g_root);

    wchar_t *appdata = NULL;
    if (SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, NULL, &appdata) == S_OK) {
        swprintf(g_data, MAX_PATH, L"%ls\\Fable III Restored", appdata);
        CoTaskMemFree(appdata);
    } else {
        swprintf(g_data, MAX_PATH, L"%ls\\Restored", g_root);
    }
    CreateDirectoryW(g_data, NULL);

    if (GetPrivateProfileIntW(L"Debug", L"Log", 1, g_ini)) {
        wchar_t lp[MAX_PATH];
        swprintf(lp, MAX_PATH, L"%ls\\Fable3Restored.log", g_root);
        g_log = _wfopen(lp, L"w");
        if (!g_log) { swprintf(lp, MAX_PATH, L"%ls\\Fable3Restored.log", g_data); g_log = _wfopen(lp, L"w"); }
    }

    char name[64];
    xl_cfg_str("Profile", "Name", "", name, sizeof name);
    if (!name[0]) {
        wchar_t wn[64]; DWORD n = 64;
        if (!GetUserNameW(wn, &n)) wcscpy(wn, L"Hero");
        WideCharToMultiByte(CP_UTF8, 0, wn, -1, name, sizeof name, NULL, NULL);
    }
    /* gamertags are max 15 chars, ASCII */
    int j = 0;
    for (int i = 0; name[i] && j < XUSER_NAME_SIZE - 1; i++)
        if ((unsigned char)name[i] >= 32 && (unsigned char)name[i] < 127) g_name[j++] = name[i];
    if (!j) strcpy(g_name, "Hero");
    g_xuid = make_xuid();
    xl_log("exe loaded at %p", GetModuleHandleW(NULL));
    xl_log("Fable III Restored xlive %s %s, profile '%s' xuid %016llX", __DATE__, __TIME__, g_name, (unsigned long long)g_xuid);
    return TRUE;
}

void xl_init_once(void) { InitOnceExecuteOnce(&g_once, init_cb, NULL, NULL); }

DWORD xl_complete(XOVERLAPPED *ov, DWORD result, DWORD extended)
{
    if (!ov) return result;
    ov->InternalLow = result;
    ov->InternalHigh = extended;
    ov->dwExtendedError = (result == ERROR_SUCCESS) ? 0 : result;
    if (ov->hEvent) SetEvent(ov->hEvent);
    if (ov->pCompletionRoutine) ov->pCompletionRoutine(result, extended, ov);
    return ERROR_IO_PENDING;
}

HANDLE xl_handle_new(int kind)
{
    xl_handle *h = calloc(1, sizeof *h);
    h->magic = XL_MAGIC;
    h->kind = kind;
    return (HANDLE)h;
}

xl_handle *xl_handle_get(HANDLE hh)
{
    xl_handle *h = (xl_handle *)hh;
    if (!h || h == INVALID_HANDLE_VALUE) return NULL;
    if (IsBadReadPtr(h, sizeof *h) || h->magic != XL_MAGIC) return NULL;
    return h;
}

/* ---------- generic exports ---------- */

DWORD XAPI XCloseHandle(HANDLE hObject)
{
    xl_handle *h = xl_handle_get(hObject);
    if (!h) return CloseHandle(hObject) ? ERROR_SUCCESS : GetLastError();
    if (h->destroy) h->destroy(h);
    h->magic = 0;
    free(h);
    return ERROR_SUCCESS;
}

DWORD XAPI XEnumerate(HANDLE hEnum, PVOID pvBuffer, DWORD cbBuffer, PDWORD pcItemsReturned, XOVERLAPPED *pOverlapped)
{
    xl_handle *h = xl_handle_get(hEnum);
    DWORD items = 0, r;
    if (!h || !h->enumerate) {
        xl_log("XEnumerate(%p): bad handle", hEnum);
        r = ERROR_INVALID_HANDLE;
    } else {
        r = h->enumerate(h, pvBuffer, cbBuffer, &items);
    }
    if (pcItemsReturned) *pcItemsReturned = items;
    if (pOverlapped) return xl_complete(pOverlapped, r, items);
    return r;
}

DWORD XAPI XGetOverlappedResult(XOVERLAPPED *pOverlapped, LPDWORD pdwResult, BOOL bWait)
{
    (void)bWait;
    if (!pOverlapped) return ERROR_INVALID_PARAMETER;
    if (pdwResult) *pdwResult = (DWORD)pOverlapped->InternalHigh;
    return (DWORD)pOverlapped->InternalLow;
}

DWORD XAPI XGetOverlappedExtendedError(XOVERLAPPED *pOverlapped)
{
    if (!pOverlapped) return GetLastError();
    return pOverlapped->dwExtendedError ? pOverlapped->dwExtendedError : (DWORD)pOverlapped->InternalLow;
}

DWORD XAPI XCancelOverlapped(XOVERLAPPED *pOverlapped)
{
    if (pOverlapped) pOverlapped->InternalLow = ERROR_CANCELLED;
    return ERROR_SUCCESS;
}

/* ---------- crash log: first-chance access violations with a short stack ---------- */
static LONG CALLBACK crash_veh(EXCEPTION_POINTERS *e)
{
    static LONG count;
    DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&count) > 8) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = e->ContextRecord;
    xl_log("EXCEPTION %08lX at %08lX (%s %08lX)  eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX",
           code, xl_va((DWORD)c->Eip), e->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
           (DWORD)e->ExceptionRecord->ExceptionInformation[1], c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    /* return addresses found on the stack that point into Fable3.exe code */
    HMODULE exe = GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
    DWORD lo = (DWORD)(ULONG_PTR)exe + 0x1000, hi = (DWORD)(ULONG_PTR)exe + nt->OptionalHeader.SizeOfCode + 0x1000;
    char buf[512]; int n = 0;
    DWORD *sp = (DWORD *)c->Esp;
    for (int i = 0; i < 256 && n < 480; i++) {
        if (IsBadReadPtr(sp + i, 4)) break;
        if (sp[i] >= lo && sp[i] < hi) n += snprintf(buf + n, sizeof buf - n, " %08lX", xl_va(sp[i]));
    }
    buf[n] = 0;
    xl_log("  stack:%s", buf);
    BYTE *ip = (BYTE *)c->Eip - 16;
    if (!IsBadReadPtr(ip, 48)) {
        n = 0;
        for (int i = 0; i < 48; i++) n += snprintf(buf + n, sizeof buf - n, "%02X%s", ip[i], i == 15 ? "|" : " ");
        xl_log("  code:%s", buf);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Fable3.exe refuses to start ("Wrong executable") unless the SecuROM launcher (F3Secu.exe)
 * created this named block first. We create it ourselves, so the game starts directly. */
static HANDLE g_sms;
static void create_launcher_block(void)
{
    g_sms = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x1000, L"-=[SMS_Fable3.exe_SMS]=-");
    BOOL existed = GetLastError() == ERROR_ALREADY_EXISTS;
    xl_log("launcher block %s", g_sms ? (existed ? "already present" : "created") : "FAILED");
    if (g_sms && !existed) {
        BYTE *v = MapViewOfFile(g_sms, FILE_MAP_WRITE, 0, 0, 0x6C4);
        if (v) {
            *(DWORD *)(v + 0x6C) = 0x1000B;                         /* launcher interface version */
            GetModuleFileNameW(NULL, (wchar_t *)(v + 0x2A8), 0x200); /* game executable path */
            UnmapViewOfFile(v);
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        xl_init_once();
        if (xl_cfg_int("Debug", "CrashLog", 1)) AddVectoredExceptionHandler(0, crash_veh);
        create_launcher_block();
        extern void xl_d3dfix_attach(void);
        xl_d3dfix_attach();
        extern void xl_secu_attach(void);
        xl_secu_attach();
        extern void xl_cloth_attach(void);
        xl_cloth_attach();
        extern void xl_video_attach(void);
        xl_video_attach();
        extern void xl_test_attach(void);
        xl_test_attach();
    }
    return TRUE;
}
