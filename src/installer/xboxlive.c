/* Fable III Restored - optional "Xbox Live mode": Microsoft's real Games for Windows LIVE, for
 * the original online co-op through an Xbox account (the method from the Steam guides, done
 * automatically):
 *  1. GFWL 3.5.92 + Marketplace 3.5.67 from Microsoft's own GFWL download server (checked),
 *     older versions removed first.
 *  2. One merged DLC folder <game>\XboxLiveMode\D7FCB87D...\ the way GFWL expects it:
 *     - all three DLC packs' files, hard-linked from <game>\DLC (no second copy),
 *     - package_collection_info.xmb generated from Microsoft's three lists with every
 *       premiumContentMask set to 0,
 *     - dlc2paid.bnk.dat copied with its premiumContentMask set to 0,
 *     - the Traitor's Keep license files from the guide (1 KB zip); the launcher copies the
 *       license to the player's Xbox ID after their first sign-in.
 *     GFWL's DLC folder (%LOCALAPPDATA%\Microsoft\Xlive\DLC\4D53090A\00000002\D7FC...) is a
 *     junction to it.
 * The launcher switches modes (setting our xlive.dll aside, Windows 7 compatibility + admin). */
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <wchar.h>
#include "xboxlive.h"

#define TK_ID L"D7FCB87DC6790538CC5EE45EC44EC782603B8ACB"
static const wchar_t *k_pack_ids[3] = {   /* order matters: later packs overwrite earlier */
    L"60340833BA99AD081835DF2F3EF01F3F247D0DC3",   /* DLC3 Inquisitor */
    L"B7D5FE3D720F347CE446785C3F55488C8E7FF201",   /* DLC1 Understone etc. */
    TK_ID,                                         /* DLC2 Traitor's Keep */
};

static int dir_exists(const wchar_t *p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }

static char *read_all(const wchar_t *p, size_t *n)
{
    FILE *f = _wfopen(p, L"rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc(sz + 1);
    *n = fread(b, 1, sz, f);
    b[*n] = 0;
    fclose(f);
    return b;
}
static int write_all(const wchar_t *p, const char *b, size_t n)
{
    FILE *f = _wfopen(p, L"wb");
    if (!f) return 0;
    int ok = fwrite(b, 1, n, f) == n;
    fclose(f);
    return ok;
}

/* run a command and wait; returns exit code (or -1) */
static int run_wait(const wchar_t *exe, const wchar_t *args)
{
    wchar_t cmd[2048];
    swprintf(cmd, 2048, L"\"%ls\" %ls", exe, args);
    STARTUPINFOW si = { .cb = sizeof si, .dwFlags = STARTF_USESHOWWINDOW, .wShowWindow = SW_HIDE };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, 20 * 60 * 1000);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return (int)code;
}

/* ---- GFWL ---- */
static const xbl_file_t k_msi[2] = {
    { L"Games for Windows LIVE 3.5.92", L"http://download.gfwl.xboxlive.com/content/gfwl-public/redists/production/xliveredist.msi",
      L"cef636119489445101f65f99752cb2b62175a02f40c59061c029fd208428b4d0", 21598208ULL, L"xliveredist.msi" },
    { L"Games for Windows Marketplace 3.5.67", L"http://download.gfwl.xboxlive.com/content/gfwl-public/redists/production/gfwlclient.msi",
      L"3e266881582884e59029397e1fc0eb3bf93185d60ccb5b9f1d484cd475a076e6", 3375104ULL, L"gfwlclient.msi" },
};
static const xbl_file_t k_lics = {
    L"Traitor's Keep license files", L"https://drive.usercontent.google.com/download?id=181cFXO8aoUGh4lmDzu_ExG_7e1RMY8tU&export=download",
    L"b954f0bb8e97451c4185aadc6d0df2f559eb06a044b2a05c30f37b6933f28c85", 1042ULL, L"tk_lics.zip" };

/* Uninstall every installed copy of an MSI product by its display name (both registry views),
 * whatever its version - the guide's "uninstall both" step, which also repairs a broken GFWL. */
static void gfwl_remove_all(const wchar_t *name, xbl_status_fn st)
{
    wchar_t codes[8][64], vers[8][64];
    int n_codes = 0;
    REGSAM views[2] = { KEY_WOW64_32KEY, KEY_WOW64_64KEY };
    for (int v = 0; v < 2; v++) {
        HKEY root;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | views[v], &root)) continue;
        wchar_t sub[256]; DWORD i = 0, n;
        while (n = 256, RegEnumKeyExW(root, i++, sub, &n, NULL, NULL, NULL, NULL) == 0) {
            HKEY k; wchar_t dn[256] = L"", dv[64] = L""; DWORD cb;
            if (RegOpenKeyExW(root, sub, 0, KEY_READ | views[v], &k)) continue;
            cb = sizeof dn; RegQueryValueExW(k, L"DisplayName", NULL, NULL, (BYTE *)dn, &cb);
            cb = sizeof dv; RegQueryValueExW(k, L"DisplayVersion", NULL, NULL, (BYTE *)dv, &cb);
            RegCloseKey(k);
            if (wcscmp(dn, name) || sub[0] != L'{' || n_codes >= 8) continue;
            int dup = 0;
            for (int j = 0; j < n_codes; j++) if (!_wcsicmp(codes[j], sub)) dup = 1;
            if (dup) continue;
            lstrcpynW(codes[n_codes], sub, 64);
            lstrcpynW(vers[n_codes], dv, 64);
            n_codes++;
        }
        RegCloseKey(root);
    }
    for (int j = 0; j < n_codes; j++) {
        wchar_t args[300];
        swprintf(args, 300, L"/x %ls /qn /norestart", codes[j]);
        st(L"Uninstalling %ls %ls...", name, vers[j]);
        run_wait(L"msiexec.exe", args);
    }
}

/* ---- merged DLC folder ---- */
static int link_or_copy(const wchar_t *from, const wchar_t *to)
{
    DeleteFileW(to);
    if (CreateHardLinkW(to, from, NULL)) return 1;
    return CopyFileW(from, to, FALSE);
}

static int build_merged(const wchar_t *game, xbl_status_fn st)
{
    wchar_t root[MAX_PATH], content[MAX_PATH], p[MAX_PATH], q[MAX_PATH];
    swprintf(root, MAX_PATH, L"%ls\\XboxLiveMode\\" TK_ID, game);
    swprintf(content, MAX_PATH, L"%ls\\Content", root);
    SHCreateDirectoryExW(NULL, content, NULL);
    for (int i = 0; i < 3; i++) {
        wchar_t pack[MAX_PATH], pat[MAX_PATH];
        swprintf(pack, MAX_PATH, L"%ls\\DLC\\%ls", game, k_pack_ids[i]);
        if (!dir_exists(pack)) { st(L"ERROR: DLC %ls is missing - install the DLC first.", k_pack_ids[i]); return 0; }
        /* top-level files (content.xbx, content.cat, png): the last pack's win, like the guide */
        for (int sub = 0; sub < 2; sub++) {
            const wchar_t *dir = sub ? L"\\Content" : L"";
            swprintf(pat, MAX_PATH, L"%ls%ls\\*", pack, dir);
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(pat, &fd);
            if (h == INVALID_HANDLE_VALUE) continue;
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                if (!_wcsicmp(fd.cFileName, L"installed.txt") || !_wcsicmp(fd.cFileName, L"package_collection_info.xmb") ||
                    !_wcsicmp(fd.cFileName, L"dlc2paid.bnk.dat")) continue;
                swprintf(p, MAX_PATH, L"%ls%ls\\%ls", pack, dir, fd.cFileName);
                swprintf(q, MAX_PATH, L"%ls%ls\\%ls", root, dir, fd.cFileName);
                if (!link_or_copy(p, q)) { st(L"ERROR: could not place %ls", fd.cFileName); FindClose(h); return 0; }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    /* package_collection_info.xmb = DLC1's header + every pack's packages, premium masks 0 */
    static const wchar_t *order[3] = { L"B7D5FE3D720F347CE446785C3F55488C8E7FF201", TK_ID, L"60340833BA99AD081835DF2F3EF01F3F247D0DC3" };
    size_t cap = 1 << 16, len = 0;
    char *out = calloc(1, cap);
    char *tail = NULL;
    for (int i = 0; i < 3; i++) {
        size_t n; char *t;
        swprintf(p, MAX_PATH, L"%ls\\DLC\\%ls\\Content\\package_collection_info.xmb", game, order[i]);
        if (!(t = read_all(p, &n))) { st(L"ERROR: %ls is missing", p); free(out); return 0; }
        char *first = strstr(t, "\t<package>"), *end = strstr(t, "</packageCollection>");
        if (!first || !end) { free(t); free(out); st(L"ERROR: unexpected DLC package list"); return 0; }
        if (i == 0) { memcpy(out, t, first - t); len = first - t; tail = _strdup(end); }
        memcpy(out + len, first, end - first); len += end - first;
        free(t);
    }
    memcpy(out + len, tail, strlen(tail)); len += strlen(tail);
    free(tail);
    out[len] = 0;
    for (char *m = out; (m = strstr(m, "<premiumContentMask>")); ) {
        m += 20;
        while (*m == '\r' || *m == '\n' || *m == '\t' || *m == ' ') m++;
        while (*m >= '0' && *m <= '9') { char *e = m + 1; while (*e >= '0' && *e <= '9') e++;
            if (e - m > 1) { memmove(m + 1, e, strlen(e) + 1); len -= (e - m - 1); }
            *m = '0'; break; }
    }
    swprintf(q, MAX_PATH, L"%ls\\package_collection_info.xmb", content);
    int ok = write_all(q, out, len);
    free(out);
    /* dlc2paid.bnk.dat with its premium mask 0 */
    size_t n; char *t;
    swprintf(p, MAX_PATH, L"%ls\\DLC\\" TK_ID L"\\Content\\dlc2paid.bnk.dat", game);
    if (!(t = read_all(p, &n))) { st(L"ERROR: dlc2paid.bnk.dat missing"); return 0; }
    char *m = strstr(t, "<premiumContentMask>");
    if (m) { m += 20; while (*m && (*m < '0' || *m > '9')) m++; if (*m) *m = '0'; }
    swprintf(q, MAX_PATH, L"%ls\\dlc2paid.bnk.dat", content);
    ok = ok && write_all(q, t, n);
    free(t);
    return ok;
}

/* GFWL's DLC folder -> our merged folder (junction; leaves a real folder alone) */
static int make_junction(const wchar_t *game, xbl_status_fn st)
{
    wchar_t *la = NULL, link[MAX_PATH], parent[MAX_PATH], target[MAX_PATH], args[MAX_PATH * 3];
    if (SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &la) != S_OK) return 0;
    swprintf(parent, MAX_PATH, L"%ls\\Microsoft\\Xlive\\DLC\\4D53090A\\00000002", la);
    swprintf(link, MAX_PATH, L"%ls\\" TK_ID, parent);
    CoTaskMemFree(la);
    swprintf(target, MAX_PATH, L"%ls\\XboxLiveMode\\" TK_ID, game);
    SHCreateDirectoryExW(NULL, parent, NULL);
    DWORD a = GetFileAttributesW(link);
    if (a != INVALID_FILE_ATTRIBUTES) {
        if (a & FILE_ATTRIBUTE_REPARSE_POINT) RemoveDirectoryW(link);   /* ours from before */
        else { st(L"Xbox Live mode: you already have your own DLC folder for GFWL - kept as it is"); return 1; }
    }
    swprintf(args, MAX_PATH * 3, L"/c mklink /J \"%ls\" \"%ls\"", link, target);
    return run_wait(L"cmd.exe", args) == 0;
}

/* the merged DLC folder + license files (no admin needed) */
int xbl_prepare_dlc(const wchar_t *game, xbl_download_fn dl, xbl_status_fn st)
{
    wchar_t tmp[MAX_PATH], f[MAX_PATH], dir[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wcscat(tmp, L"Fable3Restored");
    SHCreateDirectoryExW(NULL, tmp, NULL);
    st(L"Xbox Live mode: preparing the DLC folder for GFWL...");
    if (!build_merged(game, st)) return 0;
    swprintf(f, MAX_PATH, L"%ls\\%ls", tmp, k_lics.file);
    if (!dl(&k_lics, f)) return 0;
    swprintf(dir, MAX_PATH, L"%ls\\XboxLiveMode\\" TK_ID, game);
    wchar_t args[MAX_PATH * 3];
    swprintf(args, MAX_PATH * 3, L"-xf \"%ls\" -C \"%ls\"", f, dir);
    if (run_wait(L"tar.exe", args) != 0) { st(L"ERROR: could not unpack the license files"); return 0; }
    DeleteFileW(f);
    wchar_t lic[MAX_PATH], tpl[MAX_PATH];
    swprintf(lic, MAX_PATH, L"%ls\\0009000000CFE25F.LIC", dir);
    swprintf(tpl, MAX_PATH, L"%ls\\license_template.lic", dir);
    CopyFileW(lic, tpl, FALSE);
    return 1;
}

int xbl_setup(const wchar_t *game, xbl_download_fn dl, xbl_status_fn st)
{
    wchar_t tmp[MAX_PATH], f[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wcscat(tmp, L"Fable3Restored");
    SHCreateDirectoryExW(NULL, tmp, NULL);
    /* 1. GFWL, exactly like the guide: download the new one first, uninstall both old
     *    programs (any version), then install the new ones */
    static const wchar_t *names[2] = { L"Microsoft Games for Windows - LIVE Redistributable", L"Microsoft Games for Windows Marketplace" };
    for (int i = 0; i < 2; i++) {
        swprintf(f, MAX_PATH, L"%ls\\%ls", tmp, k_msi[i].file);
        if (!dl(&k_msi[i], f)) return 0;
    }
    for (int i = 0; i < 2; i++) gfwl_remove_all(names[i], st);
    for (int i = 0; i < 2; i++) {
        swprintf(f, MAX_PATH, L"%ls\\%ls", tmp, k_msi[i].file);
        st(L"Installing %ls...", k_msi[i].name);
        wchar_t args[MAX_PATH + 40];
        swprintf(args, MAX_PATH + 40, L"/i \"%ls\" /qn /norestart", f);
        int r = run_wait(L"msiexec.exe", args);
        DeleteFileW(f);
        if (r != 0 && r != 3010) { st(L"ERROR: %ls setup failed (code %d)", k_msi[i].name, r); return 0; }
    }
    /* 2. merged DLC folder + licenses */
    if (!xbl_prepare_dlc(game, dl, st)) return 0;
    if (!make_junction(game, st)) { st(L"ERROR: could not link the DLC folder for GFWL"); return 0; }
    /* 3. mark it available for the launcher */
    wchar_t ini[MAX_PATH];
    swprintf(ini, MAX_PATH, L"%ls\\Fable3Restored.ini", game);
    WritePrivateProfileStringW(L"XboxLive", L"Available", L"1", ini);
    st(L"Xbox Live mode is ready (choose it in Options and Co-op, or the Start menu entry)");
    return 1;
}

void xbl_remove(const wchar_t *game)
{
    wchar_t *la = NULL, link[MAX_PATH], dir[MAX_PATH + 2] = { 0 };
    if (SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &la) == S_OK) {
        swprintf(link, MAX_PATH, L"%ls\\Microsoft\\Xlive\\DLC\\4D53090A\\00000002\\" TK_ID, la);
        CoTaskMemFree(la);
        DWORD a = GetFileAttributesW(link);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT)) RemoveDirectoryW(link);
    }
    swprintf(dir, MAX_PATH, L"%ls\\XboxLiveMode", game);
    SHFILEOPSTRUCTW op = { .wFunc = FO_DELETE, .pFrom = dir, .fFlags = FOF_NO_UI };
    if (dir_exists(dir)) SHFileOperationW(&op);
    /* compatibility flags the launcher may have left */
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers", 0, KEY_WRITE, &k) == 0) {
        static const wchar_t *exes[3] = { L"FableLauncher_original.exe", L"F3Secu.exe", L"Fable3.exe" };
        for (int i = 0; i < 3; i++) { wchar_t v[MAX_PATH]; swprintf(v, MAX_PATH, L"%ls\\%ls", game, exes[i]); RegDeleteValueW(k, v); }
        RegCloseKey(k);
    }
}
