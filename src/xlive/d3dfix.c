/* Fable III Restored - Direct3D 9 fixes applied inside the game process.
 *  - CreateDevice: Windows returns D3DERR_DEVICELOST while another program holds exclusive
 *    fullscreen (or the screen is locked); the game then shows "Direct3D failed to initialise"
 *    and crashes. We wait and retry instead.
 *  - [Display] VSync=1: present once per refresh (the game's own vsync halves the frame rate).
 *  - Fullscreen uses the desktop's refresh rate instead of 60 Hz.
 *  - [Display] Mode=borderless: the game runs windowed and we make its window a borderless
 *    window covering the monitor. */
#define COBJMACROS
#include "xl.h"
#include <d3d9.h>

void patch_iat(HMODULE mod, const char *dll, const char *fn, void *hook, void **orig);
int  xl_test_mode(void);

static int g_borderless, g_vsync;
static HWND g_game_wnd;

static void fix_params(D3DPRESENT_PARAMETERS *pp, HWND w)
{
    if (!pp) return;
    if (g_vsync >= 0) pp->PresentationInterval = g_vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    /* VSync with two buffers drops to half the refresh rate (30 fps) whenever a frame misses a
     * refresh; a third buffer keeps it at the full rate (the classic "triple buffering" fix). */
    if (g_vsync > 0 && xl_cfg_int("Display", "TripleBuffering", 1) && pp->BackBufferCount < 2) {
        pp->BackBufferCount = 2;
        if (pp->SwapEffect == D3DSWAPEFFECT_COPY) pp->SwapEffect = D3DSWAPEFFECT_DISCARD;  /* copy allows one buffer only */
    }
    if (!pp->Windowed) {
        DEVMODEW dm = { .dmSize = sizeof dm };
        if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1 &&
            dm.dmPelsWidth == pp->BackBufferWidth && dm.dmPelsHeight == pp->BackBufferHeight)
            pp->FullScreen_RefreshRateInHz = dm.dmDisplayFrequency;
    }
    if (w) g_game_wnd = w;
}

static void make_borderless(void)
{
    HWND w = g_game_wnd;
    if (!g_borderless || !w || xl_test_mode()) return;
    HMONITOR mon = MonitorFromWindow(w, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { .cbSize = sizeof mi };
    GetMonitorInfoW(mon, &mi);
    SetWindowLongW(w, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowLongW(w, GWL_EXSTYLE, GetWindowLongW(w, GWL_EXSTYLE) & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME));
    SetWindowPos(w, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

static HRESULT (WINAPI *r_Reset)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
static HRESULT WINAPI f_Reset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp)
{
    D3DPRESENT_PARAMETERS orig = *pp;
    fix_params(pp, NULL);
    HRESULT hr = r_Reset(dev, pp);
    if (hr == D3DERR_INVALIDCALL && pp->BackBufferCount != orig.BackBufferCount) {   /* driver refused 3 buffers */
        *pp = orig; pp->PresentationInterval = g_vsync > 0 ? D3DPRESENT_INTERVAL_ONE : pp->PresentationInterval;
        hr = r_Reset(dev, pp);
    }
    xl_log("Reset(%ux%u, windowed %d, interval %X) -> %08lX", pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed, pp->PresentationInterval, hr);
    if (SUCCEEDED(hr) && pp->Windowed) make_borderless();
    return hr;
}

static HRESULT (WINAPI *r_CreateDevice)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
static HRESULT WINAPI f_CreateDevice(IDirect3D9 *d, UINT ad, D3DDEVTYPE t, HWND w, DWORD fl, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out)
{
    HRESULT hr;
    D3DPRESENT_PARAMETERS orig = *pp;
    fix_params(pp, pp && pp->hDeviceWindow ? pp->hDeviceWindow : w);
    for (int tries = 0;; tries++) {
        hr = r_CreateDevice(d, ad, t, w, fl, pp, out);
        if (hr == D3DERR_INVALIDCALL && pp->BackBufferCount != orig.BackBufferCount) {   /* driver refused 3 buffers */
            xl_log("CreateDevice: triple buffering refused, using the game's settings");
            *pp = orig; pp->PresentationInterval = g_vsync > 0 ? D3DPRESENT_INTERVAL_ONE : pp->PresentationInterval;
            hr = r_CreateDevice(d, ad, t, w, fl, pp, out);
        }
        if (hr != D3DERR_DEVICELOST || tries >= 240) break;
        if (tries == 0) xl_log("CreateDevice: device lost (another program has exclusive fullscreen?) - waiting");
        Sleep(250);
    }
    xl_log("CreateDevice(adapter %u, %ux%u, windowed %d, interval %X, buffers %u, %u Hz) -> %08lX",
           ad, pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed, pp->PresentationInterval, pp->BackBufferCount, pp->FullScreen_RefreshRateInHz, hr);
    if (SUCCEEDED(hr) && out && *out) {
        if (!r_Reset) {
            void **vt = *(void ***)*out; DWORD old;
            VirtualProtect(&vt[16], 4, PAGE_READWRITE, &old);
            r_Reset = vt[16]; vt[16] = (void *)f_Reset;
            VirtualProtect(&vt[16], 4, old, &old);
        }
        if (pp->Windowed) make_borderless();
    }
    return hr;
}

static IDirect3D9 *(WINAPI *r_Direct3DCreate9)(UINT);
static IDirect3D9 *WINAPI f_Direct3DCreate9(UINT v)
{
    IDirect3D9 *d = r_Direct3DCreate9(v);
    if (d && !r_CreateDevice) {
        void **vt = *(void ***)d; DWORD old;
        VirtualProtect(&vt[16], 4, PAGE_READWRITE, &old);
        r_CreateDevice = vt[16]; vt[16] = (void *)f_CreateDevice;
        VirtualProtect(&vt[16], 4, old, &old);
    }
    return d;
}

void xl_d3dfix_attach(void)
{
    char mode[32];
    xl_cfg_str("Display", "Mode", "borderless", mode, sizeof mode);
    g_borderless = !_stricmp(mode, "borderless");
    g_vsync = xl_cfg_int("Display", "VSync", -1);
    patch_iat(GetModuleHandleW(NULL), "d3d9.dll", "Direct3DCreate9", (void *)f_Direct3DCreate9, (void **)&r_Direct3DCreate9);
}
