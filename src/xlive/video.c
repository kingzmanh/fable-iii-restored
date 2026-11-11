/* Fable III Restored - skip the Microsoft / Lionhead logo videos ([Game] SkipIntro=1).
 * The startup code plays data\art\videos\microsoft_logo.bik and lionhead_logo.bik through
 * Bink (FUN_0061bfe0 -> player -> BinkOpen at 0x19DF5xx). The game loads each video into
 * memory first (BinkOpen flag 0x04000000 = from memory), so we recognise the two logos by
 * their Bink header (exact size + frame count + 1280x720). When BinkOpen returns NULL the
 * player simply ends and loading carries on. Story cutscenes and game data are untouched. */
#include "xl.h"

void patch_iat(HMODULE mod, const char *dll, const char *fn, void *hook, void **orig);

static void *(WINAPI *r_BinkOpen)(const void *src, DWORD flags);
static void (WINAPI *p_BinkGoto)(void *bink, DWORD frame, int flags);

static const struct { DWORD size, frames; } k_logos[] = {
    { 5750452, 264 },    /* microsoft_logo.bik */
    { 15838416, 274 },   /* lionhead_logo.bik */
};

static void *WINAPI f_BinkOpen(const void *src, DWORD flags)
{
    if ((flags & 0x04000000) && src && !IsBadReadPtr(src, 0x20)) {
        const DWORD *h = src;
        if ((h[0] & 0x00FFFFFF) == 0x4B4942 /* "BIK" */ && h[5] == 1280 && h[6] == 720)
            for (unsigned i = 0; i < sizeof k_logos / sizeof *k_logos; i++)
                if (h[1] + 8 == k_logos[i].size && h[2] == k_logos[i].frames) {
                    /* open it normally, then jump to the last frame: the player sees a video
                     * that has just ended (returning NULL here crashes the game later) */
                    void *b = r_BinkOpen(src, flags);
                    if (b && p_BinkGoto) p_BinkGoto(b, k_logos[i].frames, 0);
                    xl_log("intro: skipped logo video %u", i);
                    return b;
                }
    }
    return r_BinkOpen(src, flags);
}

void xl_video_attach(void)
{
    if (!xl_cfg_int("Game", "SkipIntro", 1)) return;
    HMODULE bink = GetModuleHandleW(L"binkw32.dll");
    p_BinkGoto = bink ? (void *)GetProcAddress(bink, "_BinkGoto@12") : NULL;
    if (!p_BinkGoto) { xl_log("intro: BinkGoto not found, logos stay"); return; }
    patch_iat(GetModuleHandleW(NULL), "binkw32.dll", "_BinkOpen@8", (void *)f_BinkOpen, (void **)&r_BinkOpen);
    xl_log("intro: logo videos will be skipped");
}
