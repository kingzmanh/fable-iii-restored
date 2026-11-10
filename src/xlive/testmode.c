/* Fable III Restored - automated test mode (only when F3R_TEST=1 is set by our test script).
 *  - keeps the game window off-screen and out of the taskbar
 *  - mutes this process's audio session (and unmutes it again on exit)
 *  - F3R_SHOTS=ms,ms,...   saves back-buffer screenshots (BMP) to F3R_SHOTDIR
 *  - F3R_QUIT=ms           ends the process after ms
 *  - F3R_KEYS=ms:vk,...    taps a virtual key at a time (keybd_event to the game window) */
#define COBJMACROS
#include "xl.h"
#include <d3d9.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

static int g_test = -1;
static IDirect3DDevice9 *g_dev;
static HWND g_hwnd;
static DWORD g_t0;
static DWORD g_shots[32]; static int g_nshots, g_shot_i;
static DWORD g_quit;
static struct { DWORD t; BYTE vk; int done; } g_keys[64]; static int g_nkeys;

int xl_test_mode(void)
{
    if (g_test < 0) {
        char v[16];
        g_test = GetEnvironmentVariableA("F3R_TEST", v, sizeof v) && v[0] == '1';
    }
    return g_test;
}

static const GUID IID_IMMDeviceEnumerator_ = {0xa95664d2,0x9614,0x4f35,{0xa7,0x46,0xde,0x8d,0xb6,0x36,0x17,0xe6}};
static const GUID CLSID_MMDeviceEnumerator_ = {0xbcde0395,0xe52f,0x467c,{0x8e,0x3d,0xc4,0x57,0x92,0x91,0x69,0x2e}};

static const GUID IID_IAudioSessionManager2_ = {0x77aa99a0,0x1bd6,0x484f,{0x8b,0xc7,0x2c,0x65,0x4c,0x9a,0x9b,0x6f}};
static const GUID IID_IAudioSessionControl2_ = {0xbfb7ff88,0x7239,0x4fc9,{0x8f,0xa2,0x07,0xc9,0x50,0xbe,0x9c,0x6d}};
static const GUID IID_ISimpleAudioVolume_ = {0x87ce5498,0x68d6,0x44e5,{0x92,0x15,0x6d,0xa4,0x7e,0xf8,0x83,0xd8}};

/* Mute (or unmute) every audio session of this process on every render device.
 * Returns how many sessions were touched. */
static int mute_sessions(BOOL m)
{
    int touched = 0;
    IMMDeviceEnumerator *en = NULL;
    IMMDeviceCollection *col = NULL;
    if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator_, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator_, (void **)&en))) return 0;
    UINT ndev = 0;
    if (SUCCEEDED(IMMDeviceEnumerator_EnumAudioEndpoints(en, eRender, DEVICE_STATE_ACTIVE, &col))) IMMDeviceCollection_GetCount(col, &ndev);
    for (UINT d = 0; d < ndev; d++) {
        IMMDevice *dev = NULL; IAudioSessionManager2 *sm = NULL; IAudioSessionEnumerator *se = NULL;
        if (FAILED(IMMDeviceCollection_Item(col, d, &dev))) continue;
        if (SUCCEEDED(IMMDevice_Activate(dev, &IID_IAudioSessionManager2_, CLSCTX_ALL, NULL, (void **)&sm)) &&
            SUCCEEDED(IAudioSessionManager2_GetSessionEnumerator(sm, &se))) {
            int n = 0;
            IAudioSessionEnumerator_GetCount(se, &n);
            for (int i = 0; i < n; i++) {
                IAudioSessionControl *c = NULL; IAudioSessionControl2 *c2 = NULL; ISimpleAudioVolume *v = NULL;
                if (FAILED(IAudioSessionEnumerator_GetSession(se, i, &c))) continue;
                if (SUCCEEDED(IAudioSessionControl_QueryInterface(c, &IID_IAudioSessionControl2_, (void **)&c2))) {
                    DWORD pid = 0;
                    IAudioSessionControl2_GetProcessId(c2, &pid);
                    if (pid == GetCurrentProcessId() &&
                        SUCCEEDED(IAudioSessionControl_QueryInterface(c, &IID_ISimpleAudioVolume_, (void **)&v))) {
                        BOOL cur = !m;
                        ISimpleAudioVolume_GetMute(v, &cur);
                        if (cur != m) { ISimpleAudioVolume_SetMute(v, m, NULL); touched++; }
                        ISimpleAudioVolume_Release(v);
                    }
                    IAudioSessionControl2_Release(c2);
                }
                IAudioSessionControl_Release(c);
            }
        }
        if (se) IAudioSessionEnumerator_Release(se);
        if (sm) IAudioSessionManager2_Release(sm);
        IMMDevice_Release(dev);
    }
    if (col) IMMDeviceCollection_Release(col);
    IMMDeviceEnumerator_Release(en);
    return touched;
}

static volatile LONG g_unmuting;

/* polls from process start: any new session of ours is muted within ~50 ms */
static DWORD WINAPI mute_thread(LPVOID p)
{
    (void)p;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    while (!g_unmuting) {
        int n = mute_sessions(TRUE);
        if (n) xl_log("test: muted %d audio session(s)", n);
        Sleep(50);
    }
    return 0;
}

static void flag_path(wchar_t *out) { swprintf(out, MAX_PATH, L"%ls\test_muted.flag", xl_data_dir()); }

static void set_mute(BOOL m)
{
    if (m) return;   /* muting is done by mute_thread */
    InterlockedExchange(&g_unmuting, 1);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    mute_sessions(FALSE);  /* so Windows doesn't remember Fable3.exe as muted for real play */
    wchar_t f[MAX_PATH]; flag_path(f); DeleteFileW(f);
}

/* normal play after a test run that was killed before it could unmute: undo our mute once */
static DWORD WINAPI unmute_thread(LPVOID p)
{
    (void)p;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    for (int i = 0; i < 300; i++) { mute_sessions(FALSE); Sleep(100); }
    wchar_t f[MAX_PATH]; flag_path(f); DeleteFileW(f);
    return 0;
}

static void parse_list(const char *env, DWORD *out, int max, int *n)
{
    char v[512];
    *n = 0;
    if (!GetEnvironmentVariableA(env, v, sizeof v)) return;
    for (char *p = strtok(v, ","); p && *n < max; p = strtok(NULL, ",")) out[(*n)++] = strtoul(p, NULL, 10);
}

static void hide_window(void)
{
    if (!g_hwnd) return;
    LONG ex = GetWindowLongW(g_hwnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_TOOLWINDOW)) SetWindowLongW(g_hwnd, GWL_EXSTYLE, (ex | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW);
    RECT r; GetWindowRect(g_hwnd, &r);
    if (r.left > -20000) SetWindowPos(g_hwnd, NULL, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void xl_test_on_device(void *dev)
{
    if (!xl_test_mode()) return;
    g_dev = dev;
    D3DDEVICE_CREATION_PARAMETERS cp;
    if (g_dev && SUCCEEDED(IDirect3DDevice9_GetCreationParameters(g_dev, &cp))) g_hwnd = cp.hFocusWindow;
    xl_log("test: device focus window %p", g_hwnd);
    hide_window();
}

static void save_shot(int idx)
{
    IDirect3DSurface9 *bb = NULL, *sys = NULL;
    D3DSURFACE_DESC d;
    if (FAILED(IDirect3DDevice9_GetBackBuffer(g_dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    IDirect3DSurface9_GetDesc(bb, &d);
    if (SUCCEEDED(IDirect3DDevice9_CreateOffscreenPlainSurface(g_dev, d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, NULL)) &&
        SUCCEEDED(IDirect3DDevice9_GetRenderTargetData(g_dev, bb, sys))) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(IDirect3DSurface9_LockRect(sys, &lr, NULL, D3DLOCK_READONLY))) {
            char dir[MAX_PATH] = ".", path[MAX_PATH];
            GetEnvironmentVariableA("F3R_SHOTDIR", dir, sizeof dir);
            snprintf(path, sizeof path, "%s\\shot_%02d_%lums.bmp", dir, idx, GetTickCount() - g_t0);
            FILE *f = fopen(path, "wb");
            if (f) {
                DWORD w = d.Width, h = d.Height, row = w * 3, pad = (4 - row % 4) % 4, img = (row + pad) * h;
                BITMAPFILEHEADER fh = { 0x4D42, 54 + img, 0, 0, 54 };
                BITMAPINFOHEADER ih = { 40, (LONG)w, (LONG)h, 1, 24, 0, img, 0, 0, 0, 0 };
                fwrite(&fh, sizeof fh, 1, f); fwrite(&ih, sizeof ih, 1, f);
                BYTE *line = malloc(row + pad);
                memset(line, 0, row + pad);
                for (LONG y = (LONG)h - 1; y >= 0; y--) {
                    const BYTE *src = (BYTE *)lr.pBits + y * lr.Pitch;
                    for (DWORD x = 0; x < w; x++) { line[x * 3] = src[x * 4]; line[x * 3 + 1] = src[x * 4 + 1]; line[x * 3 + 2] = src[x * 4 + 2]; }
                    fwrite(line, 1, row + pad, f);
                }
                free(line);
                fclose(f);
                xl_log("test: screenshot %s", path);
            }
            IDirect3DSurface9_UnlockRect(sys);
        }
    }
    if (sys) IDirect3DSurface9_Release(sys);
    IDirect3DSurface9_Release(bb);
}

void xl_test_frame(void)
{
    static DWORD last_hide;
    if (!xl_test_mode()) return;
    if (!g_t0) {
        char v[512];
        g_t0 = GetTickCount();
        parse_list("F3R_SHOTS", g_shots, 32, &g_nshots);
        if (GetEnvironmentVariableA("F3R_QUIT", v, sizeof v)) g_quit = strtoul(v, NULL, 10);
        if (GetEnvironmentVariableA("F3R_WMKEYS", v, sizeof v)) {
            for (char *p = strtok(v, ","); p && g_nkeys < 64; p = strtok(NULL, ",")) {
                char *c = strchr(p, ':');
                if (!c) continue;
                g_keys[g_nkeys].t = strtoul(p, NULL, 10);
                g_keys[g_nkeys].vk = (BYTE)strtoul(c + 1, NULL, 0);
                g_nkeys++;
            }
        }
    }
    DWORD now = GetTickCount() - g_t0;
    if (now - last_hide > 500) {
        hide_window(); last_hide = now;
        if (g_hwnd) {   /* the game only takes input while it believes it is the active app */
            PostMessageW(g_hwnd, WM_ACTIVATEAPP, TRUE, 0);
            PostMessageW(g_hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
            PostMessageW(g_hwnd, WM_SETFOCUS, 0, 0);
        }
    }
    while (g_shot_i < g_nshots && now >= g_shots[g_shot_i]) save_shot(g_shot_i++);
    for (int i = 0; i < g_nkeys; i++) {
        if (g_keys[i].done || now < g_keys[i].t) continue;
        g_keys[i].done = 1;
        xl_log("test: key %02X", g_keys[i].vk);
        PostMessageW(g_hwnd, WM_KEYDOWN, g_keys[i].vk, 1);
        PostMessageW(g_hwnd, WM_KEYUP, g_keys[i].vk, 0xC0000001);
    }
    if (g_quit && now >= g_quit) {
        xl_log("test: quit after %lu ms", now);
        set_mute(FALSE);
        TerminateProcess(GetCurrentProcess(), 0);
    }
}

/* unmute on normal exit too */
void xl_test_exit(void) { if (g_test == 1) set_mute(FALSE); }

/* In test mode, windows are created off-screen and as tool windows so nothing ever flashes
 * on the desktop or taskbar: patch Fable3.exe's import of CreateWindowExW/A. */
static HWND (WINAPI *real_CreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static HWND (WINAPI *real_CreateWindowExA)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);

static HWND WINAPI test_CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD st, int x, int y, int w, int h, HWND p, HMENU m, HINSTANCE i, LPVOID lp)
{
    if (!p) { ex = (ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) & ~WS_EX_APPWINDOW; x = -32000; y = -32000; }
    HWND r = real_CreateWindowExW(ex, cls, name, st, x, y, w, h, p, m, i, lp);
    xl_log("test: CreateWindowExW('%ls', '%ls', parent %p) -> %p", (ULONG_PTR)cls > 0xFFFF ? cls : L"#", name ? name : L"", p, r);
    if (!p && !g_hwnd) g_hwnd = r;
    return r;
}
static HWND WINAPI test_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD st, int x, int y, int w, int h, HWND p, HMENU m, HINSTANCE i, LPVOID lp)
{
    if (!p) { ex = (ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) & ~WS_EX_APPWINDOW; x = -32000; y = -32000; }
    HWND r = real_CreateWindowExA(ex, cls, name, st, x, y, w, h, p, m, i, lp);
    xl_log("test: CreateWindowExA('%s', '%s', parent %p) -> %p", (ULONG_PTR)cls > 0xFFFF ? cls : "#", name ? name : "", p, r);
    if (!p && !g_hwnd) g_hwnd = r;
    return r;
}

void patch_iat(HMODULE mod, const char *dll, const char *fn, void *hook, void **orig)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dd.VirtualAddress) return;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (void *)(base + dd.VirtualAddress); d->Name; d++) {
        if (_stricmp((char *)base + d->Name, dll)) continue;
        IMAGE_THUNK_DATA *names = (void *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA *iat = (void *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (void *)(base + names->u1.AddressOfData);
            if (strcmp((char *)ibn->Name, fn)) continue;
            DWORD old;
            VirtualProtect(&iat->u1.Function, 4, PAGE_READWRITE, &old);
            *orig = (void *)iat->u1.Function;
            iat->u1.Function = (ULONG_PTR)hook;
            VirtualProtect(&iat->u1.Function, 4, old, &old);
        }
    }
}

static int (WINAPI *real_MessageBoxW)(HWND, LPCWSTR, LPCWSTR, UINT);
static int (WINAPI *real_MessageBoxA)(HWND, LPCSTR, LPCSTR, UINT);
static int WINAPI test_MessageBoxW(HWND h, LPCWSTR t, LPCWSTR c, UINT f) { (void)h; (void)f; xl_log("test: MessageBox '%ls': %ls", c ? c : L"", t ? t : L""); return IDOK; }
static int WINAPI test_MessageBoxA(HWND h, LPCSTR t, LPCSTR c, UINT f) { (void)h; (void)f; xl_log("test: MessageBox '%s': %s", c ? c : "", t ? t : ""); return IDOK; }

/* API trace (F3R_TRACE=1): who asks what, with the caller address */
DWORD xl_va(DWORD a);
#define RA() xl_va((DWORD)(ULONG_PTR)__builtin_return_address(0))
static LPWSTR (WINAPI *r_GetCommandLineW)(void);
static LPSTR (WINAPI *r_GetCommandLineA)(void);
static HANDLE (WINAPI *r_OpenEventW)(DWORD, BOOL, LPCWSTR);
static HANDLE (WINAPI *r_OpenEventA)(DWORD, BOOL, LPCSTR);
static HANDLE (WINAPI *r_OpenMutexW)(DWORD, BOOL, LPCWSTR);
static HANDLE (WINAPI *r_OpenMutexA)(DWORD, BOOL, LPCSTR);
static HANDLE (WINAPI *r_CreateMutexW)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
static HANDLE (WINAPI *r_CreateMutexA)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
static HANDLE (WINAPI *r_CreateEventW)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
static HANDLE (WINAPI *r_CreateEventA)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR);
static HANDLE (WINAPI *r_OpenFileMappingW)(DWORD, BOOL, LPCWSTR);
static HANDLE (WINAPI *r_OpenFileMappingA)(DWORD, BOOL, LPCSTR);
static HANDLE (WINAPI *r_CreateFileMappingA)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCSTR);
static HANDLE (WINAPI *r_CreateFileMappingW)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCWSTR);
static DWORD (WINAPI *r_GetEnvironmentVariableW)(LPCWSTR, LPWSTR, DWORD);
static DWORD (WINAPI *r_GetEnvironmentVariableA)(LPCSTR, LPSTR, DWORD);
static HWND (WINAPI *r_FindWindowW)(LPCWSTR, LPCWSTR);
static HWND (WINAPI *r_FindWindowA)(LPCSTR, LPCSTR);
static HANDLE (WINAPI *r_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static HANDLE (WINAPI *r_CreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

static LPWSTR WINAPI t_GetCommandLineW(void) { LPWSTR r = r_GetCommandLineW(); xl_log("trace %08lX GetCommandLineW", RA()); return r; }
static LPSTR WINAPI t_GetCommandLineA(void) { LPSTR r = r_GetCommandLineA(); xl_log("trace %08lX GetCommandLineA", RA()); return r; }
static HANDLE WINAPI t_OpenEventW(DWORD a, BOOL b, LPCWSTR n) { HANDLE r = r_OpenEventW(a, b, n); xl_log("trace %08lX OpenEventW(%ls) -> %p", RA(), n ? n : L"", r); return r; }
static HANDLE WINAPI t_OpenEventA(DWORD a, BOOL b, LPCSTR n) { HANDLE r = r_OpenEventA(a, b, n); xl_log("trace %08lX OpenEventA(%s) -> %p", RA(), n ? n : "", r); return r; }
static HANDLE WINAPI t_OpenMutexW(DWORD a, BOOL b, LPCWSTR n) { HANDLE r = r_OpenMutexW(a, b, n); xl_log("trace %08lX OpenMutexW(%ls) -> %p", RA(), n ? n : L"", r); return r; }
static HANDLE WINAPI t_OpenMutexA(DWORD a, BOOL b, LPCSTR n) { HANDLE r = r_OpenMutexA(a, b, n); xl_log("trace %08lX OpenMutexA(%s) -> %p", RA(), n ? n : "", r); return r; }
static HANDLE WINAPI t_CreateMutexW(LPSECURITY_ATTRIBUTES s, BOOL b, LPCWSTR n) { HANDLE r = r_CreateMutexW(s, b, n); xl_log("trace %08lX CreateMutexW(%ls) -> %p err %lu", RA(), n ? n : L"", r, GetLastError()); return r; }
static HANDLE WINAPI t_CreateMutexA(LPSECURITY_ATTRIBUTES s, BOOL b, LPCSTR n) { HANDLE r = r_CreateMutexA(s, b, n); xl_log("trace %08lX CreateMutexA(%s) -> %p err %lu", RA(), n ? n : "", r, GetLastError()); return r; }
static HANDLE WINAPI t_CreateEventW(LPSECURITY_ATTRIBUTES s, BOOL m, BOOL i, LPCWSTR n) { HANDLE r = r_CreateEventW(s, m, i, n); if (n) xl_log("trace %08lX CreateEventW(%ls) -> %p err %lu", RA(), n, r, GetLastError()); return r; }
static HANDLE WINAPI t_CreateEventA(LPSECURITY_ATTRIBUTES s, BOOL m, BOOL i, LPCSTR n) { HANDLE r = r_CreateEventA(s, m, i, n); if (n) xl_log("trace %08lX CreateEventA(%s) -> %p err %lu", RA(), n, r, GetLastError()); return r; }
static HANDLE WINAPI t_OpenFileMappingW(DWORD a, BOOL b, LPCWSTR n) { HANDLE r = r_OpenFileMappingW(a, b, n); xl_log("trace %08lX OpenFileMappingW(%ls) -> %p", RA(), n ? n : L"", r); return r; }
static HANDLE WINAPI t_OpenFileMappingA(DWORD a, BOOL b, LPCSTR n) { HANDLE r = r_OpenFileMappingA(a, b, n); xl_log("trace %08lX OpenFileMappingA(%s) -> %p", RA(), n ? n : "", r); return r; }
static HANDLE WINAPI t_CreateFileMappingA(HANDLE f, LPSECURITY_ATTRIBUTES s, DWORD p, DWORD h, DWORD l, LPCSTR n) { HANDLE r = r_CreateFileMappingA(f, s, p, h, l, n); if (n) xl_log("trace %08lX CreateFileMappingA(%s) -> %p err %lu", RA(), n, r, GetLastError()); return r; }
static HANDLE WINAPI t_CreateFileMappingW(HANDLE f, LPSECURITY_ATTRIBUTES s, DWORD p, DWORD h, DWORD l, LPCWSTR n) { HANDLE r = r_CreateFileMappingW(f, s, p, h, l, n); if (n) xl_log("trace %08lX CreateFileMappingW(%ls) -> %p err %lu", RA(), n, r, GetLastError()); return r; }
static DWORD WINAPI t_GetEnvironmentVariableW(LPCWSTR n, LPWSTR b, DWORD c) { DWORD r = r_GetEnvironmentVariableW(n, b, c); xl_log("trace %08lX GetEnvironmentVariableW(%ls) -> %lu", RA(), n ? n : L"", r); return r; }
static DWORD WINAPI t_GetEnvironmentVariableA(LPCSTR n, LPSTR b, DWORD c) { DWORD r = r_GetEnvironmentVariableA(n, b, c); xl_log("trace %08lX GetEnvironmentVariableA(%s) -> %lu", RA(), n ? n : "", r); return r; }
static HWND WINAPI t_FindWindowW(LPCWSTR c, LPCWSTR n) { HWND r = r_FindWindowW(c, n); xl_log("trace %08lX FindWindowW(%ls,%ls) -> %p", RA(), (ULONG_PTR)c > 0xFFFF ? c : L"#", n ? n : L"", r); return r; }
static HWND WINAPI t_FindWindowA(LPCSTR c, LPCSTR n) { HWND r = r_FindWindowA(c, n); xl_log("trace %08lX FindWindowA(%s,%s) -> %p", RA(), (ULONG_PTR)c > 0xFFFF ? c : "#", n ? n : "", r); return r; }
static HANDLE WINAPI t_CreateFileW(LPCWSTR n, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD d, DWORD f, HANDLE t) { HANDLE r = r_CreateFileW(n, a, s, sa, d, f, t); xl_log("trace %08lX CreateFileW(%ls) -> %p", RA(), n ? n : L"", r); return r; }
static HANDLE WINAPI t_CreateFileA(LPCSTR n, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD d, DWORD f, HANDLE t) { HANDLE r = r_CreateFileA(n, a, s, sa, d, f, t); xl_log("trace %08lX CreateFileA(%s) -> %p", RA(), n ? n : "", r); return r; }

/* watch reads of a mapped view: PAGE_GUARD + single step re-arm (F3R_WATCHMAP=1) */
static BYTE *g_wview; static SIZE_T g_wsize;
static LPVOID (WINAPI *r_MapViewOfFile)(HANDLE, DWORD, DWORD, DWORD, SIZE_T);
static LONG CALLBACK watch_veh(EXCEPTION_POINTERS *e)
{
    static DWORD rearm;
    DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code == STATUS_GUARD_PAGE_VIOLATION && g_wview) {
        ULONG_PTR a = e->ExceptionRecord->ExceptionInformation[1];
        if (a >= (ULONG_PTR)g_wview && a < (ULONG_PTR)g_wview + g_wsize) {
            xl_log("watch %08lX %s +%04lX", xl_va((DWORD)e->ContextRecord->Eip), e->ExceptionRecord->ExceptionInformation[0] ? "W" : "R", (DWORD)(a - (ULONG_PTR)g_wview));
            e->ContextRecord->EFlags |= 0x100;
            rearm = 1;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    if (code == STATUS_SINGLE_STEP && rearm) {
        DWORD old;
        rearm = 0;
        VirtualProtect(g_wview, g_wsize, PAGE_READWRITE | PAGE_GUARD, &old);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static LPVOID WINAPI t_MapViewOfFile(HANDLE h, DWORD acc, DWORD hi, DWORD lo, SIZE_T n)
{
    LPVOID r = r_MapViewOfFile(h, acc, hi, lo, n);
    xl_log("trace %08lX MapViewOfFile(%p, %lX, %lu) -> %p", RA(), h, acc, (DWORD)n, r);
    if (r && !g_wview) {
        MEMORY_BASIC_INFORMATION mi; VirtualQuery(r, &mi, sizeof mi);
        g_wview = r; g_wsize = mi.RegionSize;
        AddVectoredExceptionHandler(1, watch_veh);
        DWORD old; VirtualProtect(g_wview, g_wsize, PAGE_READWRITE | PAGE_GUARD, &old);
    }
    return r;
}

static void trace_attach(HMODULE exe)
{
    char w[8];
    if (GetEnvironmentVariableA("F3R_WATCHMAP", w, sizeof w) && w[0] == '1') {
        /* stand-in for the launcher's block so we can see what the game reads */
        HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x1000, L"-=[SMS_Fable3.exe_SMS]=-");
        xl_log("watch: created SMS mapping %p", m);
        patch_iat(exe, "KERNEL32.dll", "MapViewOfFile", (void *)t_MapViewOfFile, (void **)&r_MapViewOfFile);
    }
    char v[8];
    if (!GetEnvironmentVariableA("F3R_TRACE", v, sizeof v) || v[0] != '1') return;
#define H(dll, fn) patch_iat(exe, dll, #fn, (void *)t_##fn, (void **)&r_##fn)
    H("KERNEL32.dll", GetCommandLineW); H("KERNEL32.dll", GetCommandLineA);
    H("KERNEL32.dll", OpenEventW); H("KERNEL32.dll", OpenEventA);
    H("KERNEL32.dll", OpenMutexW); H("KERNEL32.dll", OpenMutexA);
    H("KERNEL32.dll", CreateMutexW); H("KERNEL32.dll", CreateMutexA);
    H("KERNEL32.dll", CreateEventW); H("KERNEL32.dll", CreateEventA);
    H("KERNEL32.dll", OpenFileMappingW); H("KERNEL32.dll", OpenFileMappingA);
    H("KERNEL32.dll", CreateFileMappingW); H("KERNEL32.dll", CreateFileMappingA);
    H("KERNEL32.dll", GetEnvironmentVariableW); H("KERNEL32.dll", GetEnvironmentVariableA);
    H("USER32.dll", FindWindowW); H("USER32.dll", FindWindowA);
    H("KERNEL32.dll", CreateFileW); H("KERNEL32.dll", CreateFileA);
#undef H
    xl_log("trace on");
}

/* ---- virtual controller: F3R_PAD="ms:buttons_hex[:dur_ms[:lx:ly]],..." ---- */
typedef struct { DWORD dwPacketNumber; WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XINPUT_STATE_;
static struct { DWORD t, dur; WORD btn; SHORT lx, ly; } g_pad[64];
static int g_npad;
static DWORD g_pad_t0, g_pad_pkt;
static DWORD (WINAPI *r_XInputGetState)(DWORD, XINPUT_STATE_ *);
static DWORD (WINAPI *r_XInputGetCapabilities)(DWORD, DWORD, void *);

static DWORD WINAPI t_XInputGetState(DWORD u, XINPUT_STATE_ *s)
{
    static LONG calls;
    if (InterlockedIncrement(&calls) % 600 == 1) xl_log("test: XInputGetState polled (%ld calls, user %lu)", calls, u);
    if (u != 0) return ERROR_DEVICE_NOT_CONNECTED;
    if (!g_pad_t0) g_pad_t0 = GetTickCount();
    DWORD now = GetTickCount() - g_pad_t0;
    memset(s, 0, sizeof *s);
    for (int i = 0; i < g_npad; i++)
        if (now >= g_pad[i].t && now < g_pad[i].t + g_pad[i].dur) {
            s->wButtons |= g_pad[i].btn;
            if (g_pad[i].lx || g_pad[i].ly) { s->sThumbLX = g_pad[i].lx; s->sThumbLY = g_pad[i].ly; }
        }
    s->dwPacketNumber = ++g_pad_pkt;
    return ERROR_SUCCESS;
}
static DWORD WINAPI t_XInputGetCapabilities(DWORD u, DWORD f, void *c)
{
    (void)f;
    if (u != 0) return ERROR_DEVICE_NOT_CONNECTED;
    memset(c, 0, 20);
    ((BYTE *)c)[0] = 1;  /* XINPUT_DEVTYPE_GAMEPAD */
    ((BYTE *)c)[1] = 1;  /* XINPUT_DEVSUBTYPE_GAMEPAD */
    return ERROR_SUCCESS;
}

static void patch_iat_ord(HMODULE mod, const char *dll, WORD ord, void *hook, void **orig)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *d = (void *)(base + dd.VirtualAddress); d->Name; d++) {
        if (_stricmp((char *)base + d->Name, dll)) continue;
        IMAGE_THUNK_DATA *names = (void *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA *iat = (void *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (!IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal) || IMAGE_ORDINAL(names->u1.Ordinal) != ord) continue;
            DWORD old;
            VirtualProtect(&iat->u1.Function, 4, PAGE_READWRITE, &old);
            *orig = (void *)iat->u1.Function;
            iat->u1.Function = (ULONG_PTR)hook;
            VirtualProtect(&iat->u1.Function, 4, old, &old);
        }
    }
}

/* the hidden test window never has focus; tell the game it does (and keep the real cursor free) */
static HWND (WINAPI *r_GetForegroundWindow)(void);
static BOOL (WINAPI *r_SetForegroundWindow)(HWND);
static BOOL (WINAPI *r_SetCursorPos)(int, int);
static BOOL (WINAPI *r_ClipCursor)(const RECT *);
static HWND WINAPI t_GetForegroundWindow(void) { return g_hwnd ? g_hwnd : r_GetForegroundWindow(); }
static BOOL WINAPI t_SetForegroundWindow(HWND h) { (void)h; return TRUE; }
static BOOL WINAPI t_SetCursorPos(int x, int y) { (void)x; (void)y; return TRUE; }
static BOOL WINAPI t_ClipCursor(const RECT *r) { (void)r; return TRUE; }

/* ---- scripted keyboard through DirectInput (F3R_KEYS="ms:vk[:dur],...") ----
 * IDirectInput8A vtable: 3 CreateDevice.  IDirectInputDevice8A: 7 Acquire, 8 Unacquire,
 * 9 GetDeviceState, 10 GetDeviceData, 13 SetCooperativeLevel. */
static struct { DWORD t, dur; BYTE dik; int down_sent, up_sent; } g_dk[64];
static int g_ndk;
static DWORD g_dk_t0;
static void *g_kbd;   /* the keyboard device */
typedef struct { DWORD dwOfs, dwData, dwTimeStamp, dwSequence; UINT_PTR uAppData; } DIDOD;
static HRESULT (WINAPI *r_Acquire)(void *);
static HRESULT (WINAPI *r_GetDeviceState)(void *, DWORD, void *);
static HRESULT (WINAPI *r_GetDeviceData)(void *, DWORD, DIDOD *, DWORD *, DWORD);
static HRESULT (WINAPI *r_CreateDevice8)(void *, REFGUID, void **, void *);
static HRESULT (WINAPI *r_DirectInput8Create)(HINSTANCE, DWORD, REFIID, void **, void *);
static const GUID GUID_SysKeyboard_ = {0x6F1D2B61,0xD5A0,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};

static DWORD dk_now(void) { if (!g_dk_t0) g_dk_t0 = GetTickCount(); return GetTickCount() - g_dk_t0; }
static HRESULT WINAPI t_Acquire(void *d) { HRESULT r = r_Acquire(d); return d == g_kbd ? 0 : r; }
static HRESULT WINAPI t_GetDeviceState(void *d, DWORD cb, void *p)
{
    HRESULT r = r_GetDeviceState(d, cb, p);
    if (d != g_kbd) return r;
    if (FAILED(r)) memset(p, 0, cb);
    DWORD now = dk_now();
    for (int i = 0; i < g_ndk; i++)
        if (now >= g_dk[i].t && now < g_dk[i].t + g_dk[i].dur && g_dk[i].dik < cb) ((BYTE *)p)[g_dk[i].dik] = 0x80;
    return 0;
}
static HRESULT WINAPI t_GetDeviceData(void *d, DWORD cbo, DIDOD *rg, DWORD *pn, DWORD fl)
{
    if (d != g_kbd) return r_GetDeviceData(d, cbo, rg, pn, fl);
    DWORD max = pn ? *pn : 0, n = 0;
    DWORD real = max;
    HRESULT r = r_GetDeviceData(d, cbo, rg, &real, fl);
    if (SUCCEEDED(r)) n = real;      /* keep anything real (none while hidden) */
    DWORD now = dk_now();
    for (int i = 0; i < g_ndk && rg; i++) {
        if (!g_dk[i].down_sent && now >= g_dk[i].t && n < max) {
            DIDOD *o = (DIDOD *)((BYTE *)rg + n * cbo); memset(o, 0, cbo);
            o->dwOfs = g_dk[i].dik; o->dwData = 0x80; o->dwTimeStamp = GetTickCount(); o->dwSequence = 1000 + i * 2;
            g_dk[i].down_sent = 1; n++;
            xl_log("test: key down DIK %02X", g_dk[i].dik);
        }
        if (g_dk[i].down_sent && !g_dk[i].up_sent && now >= g_dk[i].t + g_dk[i].dur && n < max) {
            DIDOD *o = (DIDOD *)((BYTE *)rg + n * cbo); memset(o, 0, cbo);
            o->dwOfs = g_dk[i].dik; o->dwData = 0; o->dwTimeStamp = GetTickCount(); o->dwSequence = 1001 + i * 2;
            g_dk[i].up_sent = 1; n++;
        }
    }
    if (pn) *pn = n;
    return 0;
}
static void hook_vt(void *obj, int idx, void *hook, void **orig)
{
    void **vt = *(void ***)obj; DWORD old;
    if (vt[idx] == hook) return;
    VirtualProtect(&vt[idx], 4, PAGE_READWRITE, &old);
    *orig = vt[idx]; vt[idx] = hook;
    VirtualProtect(&vt[idx], 4, old, &old);
}
static HRESULT WINAPI t_CreateDevice8(void *di, REFGUID g, void **dev, void *outer)
{
    HRESULT r = r_CreateDevice8(di, g, dev, outer);
    if (SUCCEEDED(r) && dev && *dev && IsEqualGUID(g, &GUID_SysKeyboard_)) {
        g_kbd = *dev;
        hook_vt(g_kbd, 7, (void *)t_Acquire, (void **)&r_Acquire);
        hook_vt(g_kbd, 9, (void *)t_GetDeviceState, (void **)&r_GetDeviceState);
        hook_vt(g_kbd, 10, (void *)t_GetDeviceData, (void **)&r_GetDeviceData);
        xl_log("test: scripted keyboard attached");
    }
    return r;
}
static HRESULT WINAPI t_DirectInput8Create(HINSTANCE h, DWORD v, REFIID riid, void **out, void *outer)
{
    HRESULT r = r_DirectInput8Create(h, v, riid, out, outer);
    if (SUCCEEDED(r) && out && *out) hook_vt(*out, 3, (void *)t_CreateDevice8, (void **)&r_CreateDevice8);
    return r;
}

static void keys_attach(HMODULE exe)
{
    char v[1024];
    if (!GetEnvironmentVariableA("F3R_KEYS", v, sizeof v) || !v[0]) return;
    for (char *p = strtok(v, ","); p && g_ndk < 64; p = strtok(NULL, ",")) {
        unsigned long t = 0, vk = 0, d = 120;
        if (sscanf(p, "%lu:%li:%lu", &t, (long *)&vk, &d) < 2) continue;
        g_dk[g_ndk].t = t; g_dk[g_ndk].dur = d;
        g_dk[g_ndk].dik = (BYTE)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        g_ndk++;
    }
    patch_iat(exe, "DINPUT8.dll", "DirectInput8Create", (void *)t_DirectInput8Create, (void **)&r_DirectInput8Create);
    xl_log("test: %d scripted keys", g_ndk);
}

static void pad_attach(HMODULE exe)
{
    keys_attach(exe);
    patch_iat(exe, "USER32.dll", "GetForegroundWindow", (void *)t_GetForegroundWindow, (void **)&r_GetForegroundWindow);
    patch_iat(exe, "USER32.dll", "SetForegroundWindow", (void *)t_SetForegroundWindow, (void **)&r_SetForegroundWindow);
    patch_iat(exe, "USER32.dll", "SetCursorPos", (void *)t_SetCursorPos, (void **)&r_SetCursorPos);
    patch_iat(exe, "USER32.dll", "ClipCursor", (void *)t_ClipCursor, (void **)&r_ClipCursor);
    char v[1024];
    if (!GetEnvironmentVariableA("F3R_PAD", v, sizeof v)) return;
    for (char *p = strtok(v, ","); p && g_npad < 64; p = strtok(NULL, ",")) {
        unsigned long t = 0, b = 0, d = 150; long lx = 0, ly = 0;
        sscanf(p, "%lu:%lx:%lu:%ld:%ld", &t, &b, &d, &lx, &ly);
        g_pad[g_npad].t = t; g_pad[g_npad].btn = (WORD)b; g_pad[g_npad].dur = d;
        g_pad[g_npad].lx = (SHORT)lx; g_pad[g_npad].ly = (SHORT)ly;
        g_npad++;
    }
    patch_iat_ord(exe, "XINPUT1_3.dll", 2, (void *)t_XInputGetState, (void **)&r_XInputGetState);
    patch_iat_ord(exe, "XINPUT1_3.dll", 4, (void *)t_XInputGetCapabilities, (void **)&r_XInputGetCapabilities);
    xl_log("test: virtual pad with %d events", g_npad);
}

void xl_test_attach(void)
{
    wchar_t flag[MAX_PATH]; flag_path(flag);
    if (!xl_test_mode()) {
        if (GetFileAttributesW(flag) != INVALID_FILE_ATTRIBUTES) CloseHandle(CreateThread(NULL, 0, unmute_thread, NULL, 0, NULL));
        return;
    }
    CloseHandle(CreateFileW(flag, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, mute_thread, NULL, 0, NULL));
    HMODULE exe = GetModuleHandleW(NULL);
    trace_attach(exe);
    pad_attach(exe);
    patch_iat(exe, "USER32.dll", "MessageBoxW", (void *)test_MessageBoxW, (void **)&real_MessageBoxW);
    patch_iat(exe, "USER32.dll", "MessageBoxA", (void *)test_MessageBoxA, (void **)&real_MessageBoxA);
    patch_iat(exe, "USER32.dll", "CreateWindowExW", (void *)test_CreateWindowExW, (void **)&real_CreateWindowExW);
    patch_iat(exe, "USER32.dll", "CreateWindowExA", (void *)test_CreateWindowExA, (void **)&real_CreateWindowExA);
    xl_log("test mode: hidden window, muted audio");
}
