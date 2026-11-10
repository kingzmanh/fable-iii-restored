/* Fable III Restored - launcher (installed as FableLauncher.exe, so Steam's Play button uses it).
 *   FableLauncher.exe              start the game
 *   FableLauncher.exe --options    options + co-op window (Start menu "Fable III Restored - Options & Co-op")
 *   FableLauncher.exe --host       start the game as co-op host
 *   FableLauncher.exe --join <ip>  start the game and join the host at <ip>
 * Holding Shift while starting also opens the options window.
 * Settings live in Fable3Restored.ini next to the game; the game's own video file is
 * %APPDATA%\Lionhead Studios\Fable 3\VideoOptions.xml. */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>

static wchar_t g_dir[MAX_PATH], g_ini[MAX_PATH];

static void ini_get(const wchar_t *s, const wchar_t *k, const wchar_t *d, wchar_t *o, int n) { GetPrivateProfileStringW(s, k, d, o, n, g_ini); }
static void ini_set(const wchar_t *s, const wchar_t *k, const wchar_t *v) { WritePrivateProfileStringW(s, k, v, g_ini); }
static int ini_int(const wchar_t *s, const wchar_t *k, int d) { return (int)GetPrivateProfileIntW(s, k, d, g_ini); }

/* ---------------- display: write the game's VideoOptions.xml ----------------
 * The game only offers resolutions from its own list and resets custom ones when options
 * change, so we write the chosen resolution and mode before every start. */
static void video_options_path(wchar_t *out)
{
    wchar_t *ad = NULL;
    out[0] = 0;
    if (SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, NULL, &ad) == S_OK) {
        swprintf(out, MAX_PATH, L"%ls\\Lionhead Studios\\Fable 3", ad);
        SHCreateDirectoryExW(NULL, out, NULL);
        wcscat(out, L"\\VideoOptions.xml");
        CoTaskMemFree(ad);
    }
}

static void desktop_size(int *w, int *h)
{
    DEVMODEW dm = { .dmSize = sizeof dm };
    EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm);
    *w = (int)dm.dmPelsWidth; *h = (int)dm.dmPelsHeight;
}

/* replace <Tag type="..">value</Tag> in an xml text (ASCII) */
static void xml_set(char *xml, size_t cap, const char *tag, const char *val)
{
    char open[64]; snprintf(open, sizeof open, "<%s ", tag);
    char *a = strstr(xml, open);
    if (!a) return;
    char *v = strchr(a, '>');
    if (!v) return;
    v++;
    char *e = strstr(v, "</");
    if (!e) return;
    size_t tail = strlen(e);
    size_t nl = strlen(val);
    if ((size_t)(v - xml) + nl + tail + 1 > cap) return;
    memmove(v + nl, e, tail + 1);
    memcpy(v, val, nl);
}

static void apply_video_options(void)
{
    wchar_t mode[32], res[32];
    ini_get(L"Display", L"Mode", L"borderless", mode, 32);
    ini_get(L"Display", L"Resolution", L"auto", res, 32);
    int w = 0, h = 0;
    if (_wcsicmp(res, L"auto") == 0 || swscanf(res, L"%dx%d", &w, &h) != 2 || w < 640 || h < 480) desktop_size(&w, &h);
    if (_wcsicmp(res, L"game") == 0) return;  /* leave the game's own choice alone */

    wchar_t path[MAX_PATH];
    video_options_path(path);
    if (!path[0]) return;
    static char xml[16384];
    FILE *f = _wfopen(path, L"rb");
    size_t n = f ? fread(xml, 1, sizeof xml - 1, f) : 0;
    if (f) fclose(f);
    xml[n] = 0;
    if (!strstr(xml, "<VideoOptions>")) {
        strcpy(xml,
            "<?xml version=\"1.0\"?>\r\n<VideoOptions>\r\n"
            "\t<Version type=\"uint\">3</Version>\r\n\t<AdapterOrdinal type=\"uint\">0</AdapterOrdinal>\r\n"
            "\t<ScreenResolutionX type=\"int\">0</ScreenResolutionX>\r\n\t<ScreenResolutionY type=\"int\">0</ScreenResolutionY>\r\n"
            "\t<Fullscreen type=\"bool\">True</Fullscreen>\r\n\t<VSync type=\"bool\">True</VSync>\r\n"
            "\t<TextureDetail type=\"int\">4</TextureDetail>\r\n\t<ModelDetail type=\"int\">4</ModelDetail>\r\n"
            "\t<WaterDetail type=\"int\">4</WaterDetail>\r\n\t<ShadowDetail type=\"int\">4</ShadowDetail>\r\n"
            "\t<EffectsDetail type=\"int\">4</EffectsDetail>\r\n\t<EnvironmentDetail type=\"int\">4</EnvironmentDetail>\r\n"
            "\t<TreeDetail type=\"int\">4</TreeDetail>\r\n\t<DrawDistance type=\"int\">4</DrawDistance>\r\n"
            "\t<Calibrated type=\"bool\">True</Calibrated>\r\n</VideoOptions>\r\n");
    }
    char v[32];
    snprintf(v, sizeof v, "%d", w); xml_set(xml, sizeof xml, "ScreenResolutionX", v);
    snprintf(v, sizeof v, "%d", h); xml_set(xml, sizeof xml, "ScreenResolutionY", v);
    /* borderless and windowed both run the game windowed; our xlive.dll restyles the window */
    xml_set(xml, sizeof xml, "Fullscreen", _wcsicmp(mode, L"fullscreen") == 0 ? "True" : "False");
    f = _wfopen(path, L"wb");
    if (f) { fwrite(xml, 1, strlen(xml), f); fclose(f); }
}

/* ---------------- run the game ---------------- */
static int run_game(void)
{
    apply_video_options();
    wchar_t exe[MAX_PATH], cmd[MAX_PATH + 16];
    swprintf(exe, MAX_PATH, L"%ls\\Fable3.exe", g_dir);
    for (;;) {
        STARTUPINFOW si = { .cb = sizeof si };
        PROCESS_INFORMATION pi;
        swprintf(cmd, MAX_PATH + 16, L"\"%ls\"", exe);
        if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, g_dir, &si, &pi)) {
            wchar_t m[512];
            swprintf(m, 512, L"Could not start Fable3.exe (error %lu).\n\nIs Fable III Restored installed into the Fable III folder?", GetLastError());
            MessageBoxW(NULL, m, L"Fable III Restored", MB_ICONERROR);
            return 1;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        if (code != 0x123) return 0;  /* 0x123 = the game asks to be restarted (e.g. new DLC/language) */
    }
}

/* ---------------- options / co-op window ---------------- */
enum { ID_NAME = 100, ID_MODE, ID_RES, ID_VSYNC, ID_JOINIP, ID_PLAY, ID_HOST, ID_JOIN, ID_SAVE, ID_PORT, ID_SKIP };
static HWND g_wnd, g_name, g_modeb, g_res, g_vsync, g_ip, g_port, g_skip;
static HFONT g_font;
static int g_action;   /* 0 close, 1 play, 2 host, 3 join */

static HWND add(const wchar_t *cls, const wchar_t *txt, DWORD st, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | st, x, y, w, h, g_wnd, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void save_options(void)
{
    wchar_t b[128];
    GetWindowTextW(g_name, b, 128); ini_set(L"Profile", L"Name", b);
    static const wchar_t *modes[] = { L"borderless", L"fullscreen", L"windowed" };
    int m = (int)SendMessageW(g_modeb, CB_GETCURSEL, 0, 0);
    ini_set(L"Display", L"Mode", modes[m < 0 ? 0 : m]);
    GetWindowTextW(g_res, b, 128); ini_set(L"Display", L"Resolution", b[0] ? b : L"auto");
    ini_set(L"Display", L"VSync", SendMessageW(g_vsync, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0");
    ini_set(L"Game", L"SkipIntro", SendMessageW(g_skip, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0");
    GetWindowTextW(g_ip, b, 128); ini_set(L"Coop", L"LastJoinIP", b);
    GetWindowTextW(g_port, b, 128); ini_set(L"Network", L"Port", b);
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_PLAY: save_options(); g_action = 1; DestroyWindow(h); return 0;
        case ID_HOST: save_options(); g_action = 2; DestroyWindow(h); return 0;
        case ID_JOIN: {
            wchar_t ip[128]; GetWindowTextW(g_ip, ip, 128);
            if (!ip[0]) { MessageBoxW(h, L"Type the host's IP address (or name) first.", L"Join co-op", MB_ICONINFORMATION); return 0; }
            save_options(); g_action = 3; DestroyWindow(h); return 0;
        }
        case ID_SAVE: save_options(); DestroyWindow(h); return 0;
        }
        break;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)wp, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static int options_window(void)
{
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    NONCLIENTMETRICSW ncm = { .cbSize = sizeof ncm };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    WNDCLASSW wc = { .lpfnWndProc = wndproc, .hInstance = GetModuleHandleW(NULL), .lpszClassName = L"F3RLauncher",
                     .hbrBackground = GetSysColorBrush(COLOR_WINDOW), .hCursor = LoadCursorW(NULL, IDC_ARROW),
                     .hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1)) };
    RegisterClassW(&wc);
    RECT r = { 0, 0, 440, 400 };
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU, FALSE);
    g_wnd = CreateWindowW(L"F3RLauncher", L"Fable III Restored", WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                          CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    wchar_t b[128];
    int y = 16;
    add(L"STATIC", L"Hero name (shown to your co-op partner)", 0, 16, y, 300, 18, 0); y += 20;
    ini_get(L"Profile", L"Name", L"", b, 128);
    if (!b[0]) { DWORD n = 128; GetUserNameW(b, &n); }
    g_name = add(L"EDIT", b, WS_BORDER | ES_AUTOHSCROLL, 16, y, 200, 22, ID_NAME); SendMessageW(g_name, EM_LIMITTEXT, 15, 0); y += 34;

    add(L"STATIC", L"Display", 0, 16, y, 200, 18, 0);
    add(L"STATIC", L"Resolution (auto = your desktop, or e.g. 3440x1440)", 0, 180, y, 250, 18, 0); y += 20;
    g_modeb = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 16, y, 150, 120, ID_MODE);
    SendMessageW(g_modeb, CB_ADDSTRING, 0, (LPARAM)L"Borderless window");
    SendMessageW(g_modeb, CB_ADDSTRING, 0, (LPARAM)L"Fullscreen");
    SendMessageW(g_modeb, CB_ADDSTRING, 0, (LPARAM)L"Window");
    ini_get(L"Display", L"Mode", L"borderless", b, 128);
    SendMessageW(g_modeb, CB_SETCURSEL, !_wcsicmp(b, L"fullscreen") ? 1 : !_wcsicmp(b, L"windowed") ? 2 : 0, 0);
    ini_get(L"Display", L"Resolution", L"auto", b, 128);
    g_res = add(L"EDIT", b, WS_BORDER | ES_AUTOHSCROLL, 180, y, 120, 22, ID_RES); y += 32;
    g_vsync = add(L"BUTTON", L"VSync at full refresh rate", BS_AUTOCHECKBOX, 16, y, 200, 20, ID_VSYNC);
    SendMessageW(g_vsync, BM_SETCHECK, ini_int(L"Display", L"VSync", 1) ? BST_CHECKED : BST_UNCHECKED, 0);
    g_skip = add(L"BUTTON", L"Skip intro logos", BS_AUTOCHECKBOX, 230, y, 180, 20, ID_SKIP);
    SendMessageW(g_skip, BM_SETCHECK, ini_int(L"Game", L"SkipIntro", 1) ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 36;

    add(L"STATIC", L"Online co-op", 0, 16, y, 200, 18, 0); y += 20;
    add(L"STATIC", L"Host: start with \"Host co-op\", then open your world to co-op in game.\r\n"
                   L"Your partner joins with your IP. Over the internet forward UDP ports\r\n"
                   L"below on the host's router, or use a LAN / VPN (ZeroTier, Radmin).", 0, 16, y, 410, 52, 0); y += 58;
    add(L"STATIC", L"Host IP to join", 0, 16, y, 150, 18, 0);
    add(L"STATIC", L"Game port (UDP)", 0, 180, y, 120, 18, 0); y += 20;
    ini_get(L"Coop", L"LastJoinIP", L"", b, 128);
    g_ip = add(L"EDIT", b, WS_BORDER | ES_AUTOHSCROLL, 16, y, 150, 22, ID_JOINIP);
    ini_get(L"Network", L"Port", L"3074", b, 128);
    g_port = add(L"EDIT", b, WS_BORDER | ES_NUMBER, 180, y, 70, 22, ID_PORT);
    add(L"STATIC", L"(+ 27421 for finding the host)", 0, 258, y + 3, 170, 18, 0); y += 40;

    add(L"BUTTON", L"Play", BS_DEFPUSHBUTTON, 16, y, 96, 30, ID_PLAY);
    add(L"BUTTON", L"Host co-op", 0, 120, y, 96, 30, ID_HOST);
    add(L"BUTTON", L"Join co-op", 0, 224, y, 96, 30, ID_JOIN);
    add(L"BUTTON", L"Save && close", 0, 328, y, 96, 30, ID_SAVE);
    ShowWindow(g_wnd, SW_SHOW);
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (IsDialogMessageW(g_wnd, &m)) continue;
        TranslateMessage(&m); DispatchMessageW(&m);
    }
    return g_action;
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cmdline, int show)
{
    (void)hi; (void)hp; (void)show;
    GetModuleFileNameW(NULL, g_dir, MAX_PATH);
    *wcsrchr(g_dir, L'\\') = 0;
    swprintf(g_ini, MAX_PATH, L"%ls\\Fable3Restored.ini", g_dir);

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int action = 1;
    const wchar_t *join = NULL;
    for (int i = 1; i < argc; i++) {
        if (!_wcsicmp(argv[i], L"--options")) action = 0;
        else if (!_wcsicmp(argv[i], L"--host")) action = 2;
        else if (!_wcsicmp(argv[i], L"--join") && i + 1 < argc) { action = 3; join = argv[++i]; }
    }
    (void)cmdline;
    if (action == 0 || (GetAsyncKeyState(VK_SHIFT) & 0x8000)) action = options_window();
    if (action == 0) return 0;

    /* co-op mode for this session only */
    if (action == 3) {
        wchar_t ip[128];
        if (join) lstrcpynW(ip, join, 128); else ini_get(L"Coop", L"LastJoinIP", L"", ip, 128);
        ini_set(L"Coop", L"JoinIP", ip);
    } else {
        ini_set(L"Coop", L"JoinIP", L"");
    }
    ini_set(L"Coop", L"Host", action == 2 ? L"1" : L"0");
    int r = run_game();
    ini_set(L"Coop", L"JoinIP", L"");
    ini_set(L"Coop", L"Host", L"0");
    return r;
}
