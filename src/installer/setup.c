/* Fable III Restored - one-file installer / uninstaller.
 *   Fable3Restored-Setup.exe                    install (pick the game folder, click Install)
 *   Fable3Restored-Setup.exe --uninstall <dir>  remove it again (used by Apps & features)
 *   Fable3Restored-Setup.exe --silent <dir>     unattended install (for testing)
 * What it does: installs our xlive.dll + launcher into the game folder (originals kept),
 * downloads Microsoft's three Fable III PC DLC packages from download.xbox.com, verifies and
 * unpacks them, installs the DirectX 9 files the game needs when missing, copies saves from an
 * older profile, creates shortcuts and an uninstall entry. */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wininet.h>
#include <setupapi.h>
#include <bcrypt.h>
#include <stdio.h>
#include <wchar.h>
#include "../common/identity.h"
#include "nvidia.h"

#define APP_NAME L"Fable III Restored"
#define APP_VER  L"1.0.0"
#define UNINST_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\FableIIIRestored"
#define RES_XLIVE    101
#define RES_LAUNCHER 102

typedef struct { const wchar_t *name, *id, *url, *sha; ULONGLONG size; } dlc_t;
static const dlc_t k_dlc[3] = {
    { L"Inquisitor's Pack and tattoos", L"60340833BA99AD081835DF2F3EF01F3F247D0DC3",
      L"http://download.xbox.com/content/4d53090a/4d53090a0ecf0010.cab",
      L"836b94464b01f74e7cad6aeb7ec426b83aac12ecb180da6dfa9508730e136865", 1546685ULL },
    { L"Understone Quest Pack and launch items", L"B7D5FE3D720F347CE446785C3F55488C8E7FF201",
      L"http://download.xbox.com/content/4d53090a/4d53090a0ecf0000.cab",
      L"869a0147251b109a8c14ad842e875b381c93c8e6a883a064d9c29b904697caae", 256943166ULL },
    { L"Traitor's Keep", L"D7FCB87DC6790538CC5EE45EC44EC782603B8ACB",
      L"http://download.xbox.com/content/4d53090a/4d53090a0ecf000e.cab",
      L"3728c92b4e4ef7f6c78fcf5720a98fcacb960d835d4e5c6d7ac5ff1d4fed9f79", 1025720613ULL },
};
static const wchar_t *k_known_exe_sha = L"aac98798e77ca32282d663af54fc185c3b5256b5317ed8568d7f12a0ef42da42";

static HWND g_wnd, g_dir_edit, g_found, g_cb_dlc, g_cb_desk, g_cb_saves, g_prog, g_status, g_btn, g_browse;
static HFONT g_font, g_big;
static wchar_t g_dir[MAX_PATH];
static volatile LONG g_busy;
static int g_silent, g_quiet_uninstall;
static wchar_t g_log[8192];

#define WM_APP_STATUS (WM_APP + 1)
#define WM_APP_PROG   (WM_APP + 2)
#define WM_APP_DONE   (WM_APP + 3)

/* ---------------- small helpers ---------------- */
static void status(const wchar_t *fmt, ...)
{
    wchar_t *b = malloc(1024 * sizeof(wchar_t));
    va_list ap; va_start(ap, fmt); vswprintf(b, 1024, fmt, ap); va_end(ap);
    if (wcslen(g_log) + wcslen(b) + 4 < 8192) { wcscat(g_log, b); wcscat(g_log, L"\r\n"); }
    if (g_silent) { wprintf(L"%ls\n", b); free(b); return; }
    PostMessageW(g_wnd, WM_APP_STATUS, 0, (LPARAM)b);
}
static void progress(int permille) { if (!g_silent) PostMessageW(g_wnd, WM_APP_PROG, permille, 0); }

static int file_exists(const wchar_t *p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static int dir_exists(const wchar_t *p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }

static void make_dirs(const wchar_t *path) { SHCreateDirectoryExW(NULL, path, NULL); }

static int write_resource(int id, const wchar_t *dst)
{
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    if (!r) return 0;
    DWORD n = SizeofResource(NULL, r);
    const void *p = LockResource(LoadResource(NULL, r));
    HANDLE f = CreateFileW(dst, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0;
    BOOL ok = WriteFile(f, p, n, &w, NULL) && w == n;
    CloseHandle(f);
    return ok;
}

static int sha256_file(const wchar_t *path, wchar_t out[65], ULONGLONG *size)
{
    BCRYPT_ALG_HANDLE alg; BCRYPT_HASH_HANDLE h;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0)) return 0;
    BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) { BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(alg, 0); return 0; }
    static BYTE buf[1 << 20];
    DWORD n; ULONGLONG tot = 0;
    while (ReadFile(f, buf, sizeof buf, &n, NULL) && n) { BCryptHashData(h, buf, n, 0); tot += n; }
    CloseHandle(f);
    BYTE d[32];
    BCryptFinishHash(h, d, 32, 0);
    BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(alg, 0);
    for (int i = 0; i < 32; i++) swprintf(out + i * 2, 3, L"%02x", d[i]);
    if (size) *size = tot;
    return 1;
}

/* ---------------- finding the game ---------------- */
static int is_game_dir(const wchar_t *d)
{
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\Fable3.exe", d);
    if (!file_exists(p)) return 0;
    swprintf(p, MAX_PATH, L"%ls\\data\\levels.bnk", d);
    return file_exists(p);
}

static int find_in_library(const wchar_t *lib, wchar_t *out)
{
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\steamapps\\common\\Fable 3", lib);
    if (is_game_dir(p)) { wcscpy(out, p); return 1; }
    return 0;
}

static int detect_game(wchar_t *out)
{
    HKEY k;
    wchar_t v[MAX_PATH]; DWORD cb;
    /* Steam: main folder + every library in libraryfolders.vdf */
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &k) == 0) {
        cb = sizeof v;
        if (RegQueryValueExW(k, L"SteamPath", NULL, NULL, (BYTE *)v, &cb) == 0) {
            for (wchar_t *c = v; *c; c++) if (*c == L'/') *c = L'\\';
            if (find_in_library(v, out)) { RegCloseKey(k); return 1; }
            wchar_t vdf[MAX_PATH];
            swprintf(vdf, MAX_PATH, L"%ls\\steamapps\\libraryfolders.vdf", v);
            FILE *f = _wfopen(vdf, L"rb");
            if (f) {
                char line[1024];
                while (fgets(line, sizeof line, f)) {
                    char *q = strstr(line, "\"path\"");
                    if (!q) continue;
                    q = strchr(q + 6, '"');
                    if (!q) continue;
                    char *e = strrchr(q + 1, '"');
                    if (!e) continue;
                    *e = 0;
                    char path[MAX_PATH]; int j = 0;
                    for (char *s = q + 1; *s && j < MAX_PATH - 1; s++) { if (s[0] == '\\' && s[1] == '\\') s++; path[j++] = *s; }
                    path[j] = 0;
                    wchar_t wp[MAX_PATH];
                    MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, MAX_PATH);
                    if (find_in_library(wp, out)) { fclose(f); RegCloseKey(k); return 1; }
                }
                fclose(f);
            }
        }
        RegCloseKey(k);
    }
    /* the registry value Steam's install script writes */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\FableIII\\InstallLocation", 0, KEY_READ | KEY_WOW64_32KEY, &k) == 0) {
        cb = sizeof v;
        if (RegQueryValueExW(k, NULL, NULL, NULL, (BYTE *)v, &cb) == 0 && is_game_dir(v)) { wcscpy(out, v); RegCloseKey(k); return 1; }
        RegCloseKey(k);
    }
    /* common retail / Games for Windows install folders */
    static const wchar_t *guess[] = {
        L"C:\\Program Files (x86)\\Microsoft Games\\Fable III", L"C:\\Program Files\\Microsoft Games\\Fable III",
        L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Fable 3", L"C:\\Games\\Fable III",
    };
    for (int i = 0; i < (int)(sizeof guess / sizeof *guess); i++) if (is_game_dir(guess[i])) { wcscpy(out, guess[i]); return 1; }
    return 0;
}

/* ---------------- download + unpack DLC ---------------- */
static const dlc_t *g_cur_dlc;
static ULONGLONG g_done_bytes, g_total_bytes;

static int download(const dlc_t *d, const wchar_t *dst)
{
    HINTERNET net = InternetOpenW(L"FableIIIRestored/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!net) return 0;
    wchar_t part[MAX_PATH];
    swprintf(part, MAX_PATH, L"%ls.part", dst);
    ULONGLONG have = 0;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(part, GetFileExInfoStandard, &fa)) have = ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    if (have > d->size) { DeleteFileW(part); have = 0; }
    wchar_t hdr[96] = L"";
    if (have) swprintf(hdr, 96, L"Range: bytes=%llu-\r\n", have);
    HINTERNET u = InternetOpenUrlW(net, d->url, have ? hdr : NULL, have ? (DWORD)-1 : 0,
                                   INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_KEEP_CONNECTION, 0);
    if (!u) { InternetCloseHandle(net); return 0; }
    DWORD code = 0, cl = sizeof code;
    HttpQueryInfoW(u, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &cl, NULL);
    if (have && code != 206) have = 0;   /* server ignored the range: start over */
    if (code != 200 && code != 206) { InternetCloseHandle(u); InternetCloseHandle(net); status(L"   server answered HTTP %lu", code); return 0; }
    HANDLE f = CreateFileW(part, GENERIC_WRITE, 0, NULL, have ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { InternetCloseHandle(u); InternetCloseHandle(net); return 0; }
    if (have) SetFilePointer(f, 0, NULL, FILE_END);
    static BYTE buf[1 << 18];
    DWORD n;
    ULONGLONG got = have;
    DWORD last = 0;
    int ok = 1;
    for (;;) {
        if (!InternetReadFile(u, buf, sizeof buf, &n)) { ok = 0; break; }
        if (!n) break;
        DWORD w; if (!WriteFile(f, buf, n, &w, NULL) || w != n) { ok = 0; break; }
        got += n;
        if (GetTickCount() - last > 250) {
            last = GetTickCount();
            progress((int)((g_done_bytes + got) * 1000 / (g_total_bytes ? g_total_bytes : 1)));
            if (!g_silent) {
                wchar_t *b = malloc(256 * sizeof(wchar_t));
                swprintf(b, 256, L"Downloading %ls: %llu / %llu MB", d->name, got >> 20, d->size >> 20);
                PostMessageW(g_wnd, WM_APP_STATUS, 1, (LPARAM)b);
            }
        }
    }
    CloseHandle(f);
    InternetCloseHandle(u); InternetCloseHandle(net);
    if (!ok || got != d->size) return 0;
    DeleteFileW(dst);
    return MoveFileW(part, dst);
}

static UINT CALLBACK cab_cb(PVOID ctx, UINT note, UINT_PTR p1, UINT_PTR p2)
{
    (void)p2;
    const wchar_t *root = ctx;
    if (note == SPFILENOTIFY_FILEINCABINET) {
        FILE_IN_CABINET_INFO_W *fi = (FILE_IN_CABINET_INFO_W *)p1;
        swprintf(fi->FullTargetName, MAX_PATH, L"%ls\\%ls", root, fi->NameInCabinet);
        wchar_t dir[MAX_PATH];
        wcscpy(dir, fi->FullTargetName);
        *wcsrchr(dir, L'\\') = 0;
        make_dirs(dir);
        return FILEOP_DOIT;
    }
    return NO_ERROR;
}

static int dlc_installed(const dlc_t *d)
{
    wchar_t m[MAX_PATH];
    swprintf(m, MAX_PATH, L"%ls\\DLC\\%ls\\installed.txt", g_dir, d->id);
    return file_exists(m);
}

static int install_dlc(void)
{
    wchar_t tmp[MAX_PATH], self[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wcscat(tmp, L"Fable3Restored");
    make_dirs(tmp);
    GetModuleFileNameW(NULL, self, MAX_PATH);
    *wcsrchr(self, L'\\') = 0;
    g_total_bytes = 0; g_done_bytes = 0;
    for (int i = 0; i < 3; i++) if (!dlc_installed(&k_dlc[i])) g_total_bytes += k_dlc[i].size;
    for (int i = 0; i < 3; i++) {
        const dlc_t *d = &k_dlc[i];
        g_cur_dlc = d;
        if (dlc_installed(d)) { status(L"DLC already installed: %ls", d->name); continue; }
        wchar_t cab[MAX_PATH], sha[65];
        const wchar_t *file = wcsrchr(d->url, L'/') + 1;
        /* a copy next to the installer (offline install) is used when it matches */
        swprintf(cab, MAX_PATH, L"%ls\\%ls", self, file);
        ULONGLONG sz = 0;
        if (!(file_exists(cab) && sha256_file(cab, sha, &sz) && !wcscmp(sha, d->sha))) {
            swprintf(cab, MAX_PATH, L"%ls\\%ls", tmp, file);
            if (!(file_exists(cab) && sha256_file(cab, sha, &sz) && !wcscmp(sha, d->sha))) {
                status(L"Downloading %ls (%llu MB) from Microsoft...", d->name, d->size >> 20);
                int ok = 0;
                for (int attempt = 0; attempt < 4 && !ok; attempt++) {
                    if (attempt) status(L"   connection dropped, resuming (try %d)...", attempt + 1);
                    ok = download(d, cab);
                }
                if (!ok) { status(L"ERROR: could not download %ls. Check your internet connection and run setup again (it resumes).", d->name); return 0; }
                if (!sha256_file(cab, sha, &sz) || wcscmp(sha, d->sha)) {
                    DeleteFileW(cab);
                    status(L"ERROR: %ls download is damaged. Run setup again to download it again.", d->name);
                    return 0;
                }
            }
        }
        status(L"Unpacking %ls...", d->name);
        wchar_t root[MAX_PATH];
        swprintf(root, MAX_PATH, L"%ls\\DLC\\%ls", g_dir, d->id);
        make_dirs(root);
        if (!SetupIterateCabinetW(cab, 0, cab_cb, root)) { status(L"ERROR: unpacking %ls failed (%lu).", d->name, GetLastError()); return 0; }
        wchar_t m[MAX_PATH];
        swprintf(m, MAX_PATH, L"%ls\\installed.txt", root);
        FILE *f = _wfopen(m, L"w");
        if (f) { fwprintf(f, L"%ls\n%ls\n", d->name, d->sha); fclose(f); }
        if (wcsncmp(cab, tmp, wcslen(tmp)) == 0) DeleteFileW(cab);  /* free 1.3 GB of temp space */
        g_done_bytes += d->size;
        progress((int)(g_done_bytes * 1000 / (g_total_bytes ? g_total_bytes : 1)));
        status(L"Installed DLC: %ls", d->name);
    }
    RemoveDirectoryW(tmp);
    return 1;
}

/* ---------------- DirectX 9 runtime files ---------------- */
static void ensure_directx(void)
{
    wchar_t sys[MAX_PATH], p[MAX_PATH];
    if (!GetSystemWow64DirectoryW(sys, MAX_PATH)) GetSystemDirectoryW(sys, MAX_PATH);
    static const wchar_t *need[] = { L"d3dx9_42.dll", L"X3DAudio1_7.dll", L"xinput1_3.dll" };
    int missing = 0;
    for (int i = 0; i < 3; i++) { swprintf(p, MAX_PATH, L"%ls\\%ls", sys, need[i]); if (!file_exists(p)) missing++; }
    if (!missing) { status(L"DirectX 9 files: present"); return; }
    swprintf(p, MAX_PATH, L"%ls\\SupportFiles\\DirectX\\DXSETUP.exe", g_dir);
    if (!file_exists(p)) { status(L"WARNING: DirectX 9 files missing and the game's DirectX setup was not found."); return; }
    status(L"Installing DirectX 9 files from the game's own setup...");
    SHELLEXECUTEINFOW se = { .cbSize = sizeof se, .fMask = SEE_MASK_NOCLOSEPROCESS, .lpFile = p, .lpParameters = L"/silent", .nShow = SW_HIDE };
    if (ShellExecuteExW(&se) && se.hProcess) { WaitForSingleObject(se.hProcess, 10 * 60 * 1000); CloseHandle(se.hProcess); }
}

/* Steam runs these from the game's install script on first launch (GFWL setup, a product key
 * tool). They are not needed any more; mark them as done so Steam skips them. */
static void mark_steam_preinstalls(void)
{
    static const wchar_t *k[][2] = { { L"MSVCR", L"MSVCR" }, { L"DirectX", L"directx" }, { L"G4W", L"G4W" }, { L"GfWLPKSetter", L"GfWLPKSetter" } };
    for (int i = 0; i < 4; i++) {
        wchar_t path[256]; HKEY h; DWORD one = 1;
        swprintf(path, 256, L"SOFTWARE\\Microsoft\\FableIII\\PreInstalls\\%ls", k[i][0]);
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, NULL, 0, KEY_WRITE | KEY_WOW64_32KEY, NULL, &h, NULL) == 0) {
            RegSetValueExW(h, k[i][1], 0, REG_DWORD, (BYTE *)&one, 4);
            RegCloseKey(h);
        }
    }
}

/* ---------------- saves from an older profile ---------------- */
static void import_saves(void)
{
    wchar_t *sg = NULL, root[MAX_PATH], mine[MAX_PATH];
    if (SHGetKnownFolderPath(&FOLDERID_SavedGames, 0, NULL, &sg) != S_OK) return;
    swprintf(root, MAX_PATH, L"%ls\\Lionhead Studios\\Fable 3", sg);
    CoTaskMemFree(sg);
    swprintf(mine, MAX_PATH, L"%ls\\%016llX", root, (unsigned long long)f3r_default_xuid());
    wchar_t pat[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%ls\\*.bin", mine);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) { FindClose(h); status(L"Saves: your profile already has saves"); return; }
    /* newest other profile folder that has saves */
    swprintf(pat, MAX_PATH, L"%ls\\*", root);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) { status(L"Saves: no older saves found"); return; }
    wchar_t best[MAX_PATH] = L""; FILETIME bt = { 0 };
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.' || !_wcsicmp(fd.cFileName, L"backups")) continue;
        wchar_t sub[MAX_PATH]; WIN32_FIND_DATAW f2;
        swprintf(sub, MAX_PATH, L"%ls\\%ls\\*.bin", root, fd.cFileName);
        HANDLE h2 = FindFirstFileW(sub, &f2);
        if (h2 == INVALID_HANDLE_VALUE) continue;
        do { if (CompareFileTime(&f2.ftLastWriteTime, &bt) > 0) { bt = f2.ftLastWriteTime; swprintf(best, MAX_PATH, L"%ls\\%ls", root, fd.cFileName); } } while (FindNextFileW(h2, &f2));
        FindClose(h2);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!best[0]) { status(L"Saves: no older saves found"); return; }
    make_dirs(mine);
    swprintf(pat, MAX_PATH, L"%ls\\*", best);
    h = FindFirstFileW(pat, &fd);
    int n = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            wchar_t a[MAX_PATH], b[MAX_PATH];
            swprintf(a, MAX_PATH, L"%ls\\%ls", best, fd.cFileName);
            swprintf(b, MAX_PATH, L"%ls\\%ls", mine, fd.cFileName);
            if (CopyFileW(a, b, TRUE)) n++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    status(L"Saves: copied %d files from %ls (the originals stay where they were)", n, wcsrchr(best, L'\\') + 1);
}

/* ---------------- shortcuts / uninstall entry ---------------- */
static void shortcut(const wchar_t *lnk, const wchar_t *target, const wchar_t *args, const wchar_t *icon, const wchar_t *desc)
{
    IShellLinkW *sl; IPersistFile *pf;
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl))) return;
    sl->lpVtbl->SetPath(sl, target);
    sl->lpVtbl->SetArguments(sl, args ? args : L"");
    sl->lpVtbl->SetWorkingDirectory(sl, g_dir);
    sl->lpVtbl->SetIconLocation(sl, icon, 0);
    sl->lpVtbl->SetDescription(sl, desc);
    if (SUCCEEDED(sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) { pf->lpVtbl->Save(pf, lnk, TRUE); pf->lpVtbl->Release(pf); }
    sl->lpVtbl->Release(sl);
}

static void start_menu_dir(wchar_t *out)
{
    wchar_t *p = NULL;
    SHGetKnownFolderPath(&FOLDERID_CommonPrograms, 0, NULL, &p);
    swprintf(out, MAX_PATH, L"%ls\\" APP_NAME, p ? p : L"");
    if (p) CoTaskMemFree(p);
}
static void desktop_link(wchar_t *out)
{
    wchar_t *p = NULL;
    SHGetKnownFolderPath(&FOLDERID_PublicDesktop, 0, NULL, &p);
    swprintf(out, MAX_PATH, L"%ls\\Fable III.lnk", p ? p : L"");
    if (p) CoTaskMemFree(p);
}

static void write_default_ini(void)
{
    wchar_t ini[MAX_PATH];
    swprintf(ini, MAX_PATH, L"%ls\\Fable3Restored.ini", g_dir);
    if (file_exists(ini)) return;
    FILE *f = _wfopen(ini, L"w");
    if (!f) return;
    fputs("; Fable III Restored settings - easiest to change with \"Options & Co-op\" in the Start menu.\n"
          "[Profile]\n; Name shown to your co-op partner (max 15 letters). Empty = your Windows user name.\nName=\n"
          "; XUID=  (optional: hex profile id to use an older profile's save folder)\n\n"
          "[Display]\n; borderless, fullscreen or windowed\nMode=borderless\n"
          "; auto = your desktop resolution, or e.g. 3440x1440, or game = keep the in-game choice\nResolution=auto\n"
          "; 1 = vsync at the full refresh rate (the game's own vsync halves it)\nVSync=1\n"
          "; frame rate cap (0 = none)\nFPSLimit=0\n"
          "; optional fixed hair and cloth physics steps per second (0 = game default, e.g. 30 or 60)\nClothRate=0\n\n"
          "[Game]\nSkipIntro=1\n\n"
          "[Network]\n; UDP port the game uses for co-op (both players use the same)\nPort=3074\n\n"
          "[Coop]\nJoinIP=\nLastJoinIP=\nHost=0\n\n"
          "[Debug]\nLog=1\nCrashLog=1\n", f);
    fclose(f);
}

static int install(void)
{
    wchar_t p[MAX_PATH], q[MAX_PATH], sha[65];
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (!is_game_dir(g_dir)) { status(L"ERROR: Fable3.exe was not found in that folder."); return 0; }
    swprintf(p, MAX_PATH, L"%ls\\Fable3.exe", g_dir);
    status(L"Checking Fable3.exe...");
    if (sha256_file(p, sha, NULL) && wcscmp(sha, k_known_exe_sha))
        status(L"NOTE: this Fable3.exe is a build we have not tested (Steam 1.0.0.283 is). Installing anyway.");
    progress(10);

    /* 1. our xlive.dll (keep any previous one) */
    swprintf(p, MAX_PATH, L"%ls\\xlive.dll", g_dir);
    swprintf(q, MAX_PATH, L"%ls\\xlive.dll.before-restored", g_dir);
    if (file_exists(p) && !file_exists(q)) {
        HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        DWORD sz = GetFileSize(f, NULL); CloseHandle(f);
        HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(RES_XLIVE), (LPCWSTR)RT_RCDATA);
        if (sz != SizeofResource(NULL, r)) MoveFileW(p, q);
    }
    if (!write_resource(RES_XLIVE, p)) { status(L"ERROR: could not write xlive.dll (is the game running?)"); return 0; }
    status(L"Installed the Games for Windows LIVE replacement (xlive.dll)");

    /* 2. our launcher takes FableLauncher.exe's place; the original is kept */
    swprintf(p, MAX_PATH, L"%ls\\FableLauncher.exe", g_dir);
    swprintf(q, MAX_PATH, L"%ls\\FableLauncher_original.exe", g_dir);
    if (file_exists(p) && !file_exists(q)) MoveFileW(p, q);
    if (!write_resource(RES_LAUNCHER, p)) { status(L"ERROR: could not write FableLauncher.exe"); return 0; }
    status(L"Installed the launcher (Steam's Play button now starts the game directly)");
    write_default_ini();
    mark_steam_preinstalls();
    progress(30);

    ensure_directx();
    {   /* the Steam guide's FPS fix: Nvidia driver VSync + triple buffering for Fable3.exe */
        wchar_t m[256];
        nv_apply_fps_fix(m, 256);
        status(L"Nvidia FPS fix: %ls", m);
    }
    if (SendMessageW(g_cb_saves, BM_GETCHECK, 0, 0) == BST_CHECKED || g_silent) import_saves();

    /* 3. shortcuts + uninstall entry (not in automated UI tests) */
    if (GetEnvironmentVariableW(L"F3R_TEST", NULL, 0) > 0) { status(L"Test run: shortcuts and uninstall entry skipped"); goto dlc; }
    wchar_t sm[MAX_PATH], lnk[MAX_PATH], launcher[MAX_PATH], gameexe[MAX_PATH], uninst[MAX_PATH], self[MAX_PATH];
    swprintf(launcher, MAX_PATH, L"%ls\\FableLauncher.exe", g_dir);
    swprintf(gameexe, MAX_PATH, L"%ls\\Fable3.exe", g_dir);
    swprintf(uninst, MAX_PATH, L"%ls\\Fable3Restored-Uninstall.exe", g_dir);
    GetModuleFileNameW(NULL, self, MAX_PATH);
    if (_wcsicmp(self, uninst)) CopyFileW(self, uninst, FALSE);
    start_menu_dir(sm);
    make_dirs(sm);
    swprintf(lnk, MAX_PATH, L"%ls\\Fable III.lnk", sm);
    shortcut(lnk, launcher, NULL, gameexe, L"Play Fable III");
    swprintf(lnk, MAX_PATH, L"%ls\\Options and Co-op.lnk", sm);
    shortcut(lnk, launcher, L"--options", launcher, L"Fable III Restored options and online co-op");
    wchar_t uargs[MAX_PATH + 16];
    swprintf(uargs, MAX_PATH + 16, L"--uninstall \"%ls\"", g_dir);
    swprintf(lnk, MAX_PATH, L"%ls\\Uninstall Fable III Restored.lnk", sm);
    shortcut(lnk, uninst, uargs, uninst, L"Remove Fable III Restored");
    if (SendMessageW(g_cb_desk, BM_GETCHECK, 0, 0) == BST_CHECKED || g_silent) { desktop_link(lnk); shortcut(lnk, launcher, NULL, gameexe, L"Play Fable III"); }
    HKEY h;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, UNINST_KEY, 0, NULL, 0, KEY_WRITE, NULL, &h, NULL) == 0) {
        wchar_t cmd[MAX_PATH * 2];
        swprintf(cmd, MAX_PATH * 2, L"\"%ls\" --uninstall \"%ls\"", uninst, g_dir);
        RegSetValueExW(h, L"DisplayName", 0, REG_SZ, (BYTE *)APP_NAME, sizeof APP_NAME);
        RegSetValueExW(h, L"DisplayVersion", 0, REG_SZ, (BYTE *)APP_VER, sizeof APP_VER);
        RegSetValueExW(h, L"Publisher", 0, REG_SZ, (BYTE *)L"Fable III Restored project", sizeof L"Fable III Restored project");
        RegSetValueExW(h, L"UninstallString", 0, REG_SZ, (BYTE *)cmd, (DWORD)(wcslen(cmd) + 1) * 2);
        RegSetValueExW(h, L"InstallLocation", 0, REG_SZ, (BYTE *)g_dir, (DWORD)(wcslen(g_dir) + 1) * 2);
        RegSetValueExW(h, L"DisplayIcon", 0, REG_SZ, (BYTE *)launcher, (DWORD)(wcslen(launcher) + 1) * 2);
        DWORD one = 1;
        RegSetValueExW(h, L"NoModify", 0, REG_DWORD, (BYTE *)&one, 4);
        RegSetValueExW(h, L"NoRepair", 0, REG_DWORD, (BYTE *)&one, 4);
        RegCloseKey(h);
    }
    status(L"Created Start menu entries%ls", SendMessageW(g_cb_desk, BM_GETCHECK, 0, 0) == BST_CHECKED ? L" and a desktop shortcut" : L"");
    progress(40);

dlc:
    /* 4. DLC */
    if (g_silent || SendMessageW(g_cb_dlc, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        if (!install_dlc()) return 0;
    }
    progress(1000);
    return 1;
}

static int uninstall(void)
{
    wchar_t p[MAX_PATH], q[MAX_PATH];
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    swprintf(p, MAX_PATH, L"%ls\\FableLauncher.exe", g_dir);
    swprintf(q, MAX_PATH, L"%ls\\FableLauncher_original.exe", g_dir);
    if (file_exists(q)) { DeleteFileW(p); MoveFileW(q, p); }
    swprintf(p, MAX_PATH, L"%ls\\xlive.dll", g_dir);
    swprintf(q, MAX_PATH, L"%ls\\xlive.dll.before-restored", g_dir);
    DeleteFileW(p);
    if (file_exists(q)) MoveFileW(q, p);
    swprintf(p, MAX_PATH, L"%ls\\Fable3Restored.log", g_dir); DeleteFileW(p);
    wchar_t dlc[MAX_PATH];
    swprintf(dlc, MAX_PATH, L"%ls\\DLC", g_dir);
    if (dir_exists(dlc) && !g_quiet_uninstall && MessageBoxW(NULL, L"Also delete the downloaded DLC (about 2 GB)?\n\nKeep it if you plan to install Fable III Restored again.",
                                       APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES) {
        wchar_t from[MAX_PATH + 2] = { 0 };
        wcscpy(from, dlc);
        SHFILEOPSTRUCTW op = { .wFunc = FO_DELETE, .pFrom = from, .fFlags = FOF_NO_UI };
        SHFileOperationW(&op);
    }
    wchar_t sm[MAX_PATH], lnk[MAX_PATH];
    start_menu_dir(sm);
    const wchar_t *names[] = { L"Fable III.lnk", L"Options and Co-op.lnk", L"Uninstall Fable III Restored.lnk" };
    for (int i = 0; i < 3; i++) { swprintf(lnk, MAX_PATH, L"%ls\\%ls", sm, names[i]); DeleteFileW(lnk); }
    RemoveDirectoryW(sm);
    desktop_link(lnk); DeleteFileW(lnk);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, UNINST_KEY);
    nv_remove_fps_fix();
    /* the uninstaller is this very file when started from Apps & features: delete it after exit */
    wchar_t self[MAX_PATH], cmd[MAX_PATH * 2];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    swprintf(cmd, MAX_PATH * 2, L"/c ping 127.0.0.1 -n 3 >nul & del \"%ls\\Fable3Restored-Uninstall.exe\"", g_dir);
    ShellExecuteW(NULL, NULL, L"cmd.exe", cmd, NULL, SW_HIDE);
    return 1;
}

/* ---------------- UI ---------------- */
static void save_log(int ok)
{
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\Fable3Restored-setup.log", g_dir);
    FILE *f = _wfopen(p, L"w");
    if (f) { fwprintf(f, L"Fable III Restored " APP_VER L" setup\n%ls\n%ls\n", g_log, ok ? L"RESULT: OK" : L"RESULT: FAILED"); fclose(f); }
}

static DWORD WINAPI worker(LPVOID p)
{
    (void)p;
    int ok = install();
    save_log(ok);
    PostMessageW(g_wnd, WM_APP_DONE, ok, 0);
    return 0;
}

static void update_found(void)
{
    GetWindowTextW(g_dir_edit, g_dir, MAX_PATH);
    size_t n = wcslen(g_dir);
    while (n && (g_dir[n - 1] == L'\\' || g_dir[n - 1] == L'/')) g_dir[--n] = 0;
    wchar_t q[MAX_PATH];
    swprintf(q, MAX_PATH, L"%ls\\FableLauncher_original.exe", g_dir);
    if (is_game_dir(g_dir))
        SetWindowTextW(g_found, file_exists(q) ? L"\x2714 Fable III found (Fable III Restored is already installed here - Install updates it)" : L"\x2714 Fable III found");
    else
        SetWindowTextW(g_found, L"\x2716 Fable3.exe is not in this folder - use Browse to pick your Fable III folder");
    EnableWindow(g_btn, is_game_dir(g_dir) && !g_busy);
}

static HWND add(const wchar_t *cls, const wchar_t *txt, DWORD st, int x, int y, int w, int h, int id, HFONT f)
{
    HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | st, x, y, w, h, g_wnd, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
    return c;
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == 10 && HIWORD(wp) == EN_CHANGE) update_found();
        if (LOWORD(wp) == 11) {  /* browse */
            IFileOpenDialog *d;
            if (SUCCEEDED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&d))) {
                d->lpVtbl->SetOptions(d, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                d->lpVtbl->SetTitle(d, L"Pick your Fable III folder (the one with Fable3.exe)");
                if (SUCCEEDED(d->lpVtbl->Show(d, h))) {
                    IShellItem *it; wchar_t *path;
                    if (SUCCEEDED(d->lpVtbl->GetResult(d, &it))) {
                        if (SUCCEEDED(it->lpVtbl->GetDisplayName(it, SIGDN_FILESYSPATH, &path))) { SetWindowTextW(g_dir_edit, path); CoTaskMemFree(path); }
                        it->lpVtbl->Release(it);
                    }
                }
                d->lpVtbl->Release(d);
            }
        }
        if (LOWORD(wp) == 12) {  /* install / close */
            if (g_busy == 2) { DestroyWindow(h); return 0; }
            update_found();
            if (!is_game_dir(g_dir)) return 0;
            InterlockedExchange(&g_busy, 1);
            EnableWindow(g_btn, FALSE); EnableWindow(g_browse, FALSE); EnableWindow(g_dir_edit, FALSE);
            EnableWindow(g_cb_dlc, FALSE); EnableWindow(g_cb_desk, FALSE); EnableWindow(g_cb_saves, FALSE);
            CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
        }
        return 0;
    case WM_APP_STATUS: {
        wchar_t *b = (wchar_t *)lp;
        SetWindowTextW(g_status, b);
        free(b);
        return 0;
    }
    case WM_APP_PROG: SendMessageW(g_prog, PBM_SETPOS, wp, 0); return 0;
    case WM_APP_DONE:
        InterlockedExchange(&g_busy, 2);
        SetWindowTextW(g_btn, L"Close");
        EnableWindow(g_btn, TRUE);
        if (wp) {
            SetWindowTextW(g_status, L"Done! Start Fable III from Steam, the desktop or the Start menu.\r\n"
                                     L"Options and online co-op: Start menu \x2192 Fable III Restored \x2192 Options and Co-op.");
            SendMessageW(g_prog, PBM_SETPOS, 1000, 0);
        } else {
            SetWindowTextW(g_status, wcsstr(g_log, L"ERROR") ? wcsstr(g_log, L"ERROR") : L"Something went wrong.");
        }
        return 0;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)wp, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE: if (g_busy == 1 && MessageBoxW(h, L"Setup is still working. Stop it?", APP_NAME, MB_YESNO) != IDYES) return 0; break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cl, int show)
{
    (void)hp; (void)cl; (void)show;
    int argc; LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (argc >= 3 && !_wcsicmp(argv[1], L"--uninstall")) {
        lstrcpynW(g_dir, argv[2], MAX_PATH);
        if (MessageBoxW(NULL, L"Remove Fable III Restored from this Fable III folder?\n\nYour saves are not touched.", APP_NAME, MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        g_silent = 1;
        uninstall();
        MessageBoxW(NULL, L"Fable III Restored was removed.", APP_NAME, MB_ICONINFORMATION);
        return 0;
    }
    if (argc >= 3 && !_wcsicmp(argv[1], L"--nvidia-read")) {   /* tests: report the driver settings to a file */
        unsigned v, t; wchar_t w[64] = L"";
        int ok = nv_read_fps_fix(&v, &t, w, 64);
        FILE *f = _wfopen(argv[2], L"w");
        if (f) { fwprintf(f, L"nvapi %d, profile %ls, vsync %08X, triple %08X\n", ok, w, v, t); fclose(f); }
        return 0;
    }
    if (argc >= 3 && !_wcsicmp(argv[1], L"--nvidia-apply")) {
        wchar_t m[256]; int ok = nv_apply_fps_fix(m, 256);
        FILE *f = _wfopen(argv[2], L"w");
        if (f) { fwprintf(f, L"%d %ls\n", ok, m); fclose(f); }
        return ok ? 0 : 1;
    }
    if (argc >= 3 && !_wcsicmp(argv[1], L"--uninstall-silent")) {   /* tests: no questions, DLC kept */
        lstrcpynW(g_dir, argv[2], MAX_PATH);
        g_silent = g_quiet_uninstall = 1;
        return uninstall() ? 0 : 1;
    }
    if (argc >= 3 && !_wcsicmp(argv[1], L"--silent")) {
        AttachConsole(ATTACH_PARENT_PROCESS);
        freopen("CONOUT$", "w", stdout);
        lstrcpynW(g_dir, argv[2], MAX_PATH);
        g_silent = 1;
        int ok = install();
        wprintf(L"%ls\n", ok ? L"INSTALL OK" : L"INSTALL FAILED");
        return ok ? 0 : 1;
    }

    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);
    NONCLIENTMETRICSW ncm = { .cbSize = sizeof ncm };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 16 / 10;
    ncm.lfMessageFont.lfWeight = FW_SEMIBOLD;
    g_big = CreateFontIndirectW(&ncm.lfMessageFont);
    WNDCLASSW wc = { .lpfnWndProc = wndproc, .hInstance = hi, .lpszClassName = L"F3RSetup", .hbrBackground = GetSysColorBrush(COLOR_WINDOW),
                     .hCursor = LoadCursorW(NULL, IDC_ARROW), .hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1)) };
    RegisterClassW(&wc);
    RECT r = { 0, 0, 560, 430 };
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU, FALSE);
    int offscreen = GetEnvironmentVariableW(L"F3R_TEST", NULL, 0) > 0;   /* automated UI tests */
    g_wnd = CreateWindowExW(offscreen ? WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE : 0, L"F3RSetup", APP_NAME L" " APP_VER L" Setup",
                          WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                          offscreen ? -4000 : CW_USEDEFAULT, offscreen ? 100 : CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
    HICON ico = LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, 64, 64, 0);
    HWND pic = add(L"STATIC", L"", SS_ICON, 20, 18, 64, 64, 0, g_font);
    SendMessageW(pic, STM_SETICON, (WPARAM)ico, 0);
    add(L"STATIC", APP_NAME, 0, 100, 20, 440, 32, 0, g_big);
    add(L"STATIC", L"Makes the PC version of Fable III run again: no Games for Windows LIVE, "
                   L"no SecuROM activation, all DLC unlocked, saves that don't break and online "
                   L"co-op without Microsoft's servers.", 0, 100, 54, 440, 52, 0, g_font);
    add(L"STATIC", L"Your Fable III folder:", 0, 20, 110, 300, 18, 0, g_font);
    wchar_t found[MAX_PATH] = L"";
    detect_game(found);
    g_dir_edit = add(L"EDIT", found, WS_BORDER | ES_AUTOHSCROLL, 20, 130, 420, 24, 10, g_font);
    g_browse = add(L"BUTTON", L"Browse...", 0, 450, 129, 90, 26, 11, g_font);
    g_found = add(L"STATIC", L"", 0, 20, 160, 520, 18, 0, g_font);
    g_cb_dlc = add(L"BUTTON", L"Download and install all DLC (Traitor's Keep, Understone, Inquisitor, outfits...)\r\n"
                              L"1.3 GB from Microsoft's download server", BS_AUTOCHECKBOX | BS_MULTILINE, 20, 192, 520, 36, 0, g_font);
    SendMessageW(g_cb_dlc, BM_SETCHECK, BST_CHECKED, 0);
    g_cb_saves = add(L"BUTTON", L"Bring over saves from an older Fable III profile on this PC", BS_AUTOCHECKBOX, 20, 234, 520, 20, 0, g_font);
    SendMessageW(g_cb_saves, BM_SETCHECK, BST_CHECKED, 0);
    g_cb_desk = add(L"BUTTON", L"Desktop shortcut", BS_AUTOCHECKBOX, 20, 258, 520, 20, 0, g_font);
    SendMessageW(g_cb_desk, BM_SETCHECK, BST_CHECKED, 0);
    g_prog = add(PROGRESS_CLASSW, L"", 0, 20, 296, 520, 18, 0, g_font);
    SendMessageW(g_prog, PBM_SETRANGE32, 0, 1000);
    g_status = add(L"STATIC", L"Ready.", 0, 20, 320, 520, 48, 0, g_font);
    g_btn = add(L"BUTTON", L"Install", BS_DEFPUSHBUTTON, 440, 380, 100, 32, 12, g_font);
    update_found();
    ShowWindow(g_wnd, offscreen ? SW_SHOWNOACTIVATE : SW_SHOW);
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (IsDialogMessageW(g_wnd, &m)) continue;
        TranslateMessage(&m); DispatchMessageW(&m);
    }
    return 0;
}
