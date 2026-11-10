/* Fable III Restored - cloth/hair physics at a fixed time step.
 *
 * The engine cloth (hair, capes, the butler's feather...) is stepped once per rendered frame
 * by FUN_019c9060 (NEngine cloth manager tick):
 *     elapsed = (now - last) * scale;  clamp 0.2 s
 *     n = round(elapsed * 20 + 0.5);   dt = elapsed / n;   step every cloth n times with dt
 * so at 30 fps every step is 1/30 s - the console tuning. At 100+ fps the steps become tiny
 * and uneven, and the cloth jitters/shakes. We call the original with time that advances in
 * whole fixed steps: frames in between see 0 elapsed time (the engine then skips the step and
 * only does its per-frame bookkeeping), and a frame that crosses a step boundary sees exactly
 * k * step. The cloth is then simulated exactly as at its native rate, whatever the frame rate.
 *
 * [Display] ClothRate = steps per second (default 30, 0 = leave the game's behaviour alone).
 * File VAs: tick 0x19C9060 (__fastcall, ECX = manager), its only call 0x19D62A9,
 * now = double 0x2075CE0, last = double 0x2075D00, scale = float 0x20B74E4. */
#include "xl.h"

#define VA(a) ((BYTE *)g_base + ((a) - 0x400000))
static BYTE *g_base;
static void (__fastcall *g_tick)(void *mgr);
static double g_step, g_acc;
static double *g_now, *g_last;
static float *g_scale;

static void __fastcall cloth_tick(void *mgr)
{
    double last = *g_last, now = *g_now;
    float scale = *g_scale;
    if (last == 0.0 || scale <= 0.0f) { g_tick(mgr); return; }   /* first frame: the engine initialises */
    double e = (now - last) * scale;
    if (e < 0) e = 0;
    g_acc += e;
    if (g_acc > 0.2) g_acc = 0.2;                                  /* the engine's own clamp */
    int k = (int)(g_acc / g_step + 1e-9);
    if (k == 0) {
        *g_last = now;                                             /* no cloth step this frame */
    } else {
        double t = k * g_step;
        g_acc -= t;
        *g_last = now - t / scale;                                 /* engine sees exactly k steps */
    }
    g_tick(mgr);
}

void xl_cloth_attach(void)
{
    int rate = xl_cfg_int("Display", "ClothRate", 30);
    if (rate <= 0) { xl_log("cloth: game's own timing (ClothRate=0)"); return; }
    g_base = (BYTE *)GetModuleHandleW(NULL);
    DWORD delta = (DWORD)(ULONG_PTR)g_base - 0x400000;
    BYTE *call = VA(0x19D62A9);
    BYTE *fn = VA(0x19C9060);
    /* the tick must start with its known prologue and read the time globals we use */
    static const BYTE pro[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x3C };
    DWORD now_ref, last_ref, scale_ref;
    if (IsBadReadPtr(fn, 0x60) || memcmp(fn, pro, sizeof pro) || fn[0x1D] != 0xDD || fn[0x1E] != 0x05) goto bad;
    memcpy(&now_ref, fn + 0x1F, 4);
    memcpy(&last_ref, fn + 0x2B, 4);
    memcpy(&scale_ref, fn + 0x5A, 4);
    if (now_ref != 0x2075CE0 + delta || last_ref != 0x2075D00 + delta || scale_ref != 0x20B74E4 + delta) goto bad;
    if (call[0] != 0xE8 || (DWORD)(ULONG_PTR)(call + 5) + *(DWORD *)(call + 1) != (DWORD)(ULONG_PTR)fn) goto bad;

    g_now = (double *)VA(0x2075CE0);
    g_last = (double *)VA(0x2075D00);
    g_scale = (float *)VA(0x20B74E4);
    g_tick = (void (__fastcall *)(void *))fn;
    g_step = 1.0 / rate;
    DWORD rel = (DWORD)(ULONG_PTR)cloth_tick - (DWORD)(ULONG_PTR)(call + 5), old;
    VirtualProtect(call + 1, 4, PAGE_EXECUTE_READWRITE, &old);
    memcpy(call + 1, &rel, 4);
    VirtualProtect(call + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    xl_log("cloth: fixed %d steps per second", rate);
    return;
bad:
    xl_log("cloth: code not as expected (different game build?) - left alone");
}
