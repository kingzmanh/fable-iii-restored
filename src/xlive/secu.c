/* Fable III Restored - SecuROM launcher coupling.
 * The retail game expects to be started by SecuROM's F3Secu.exe, whose product-activation
 * wizard no longer works. Two things tie Fable3.exe to it (see docs/RE_NOTES.md):
 *  1. A named shared block "-=[SMS_Fable3.exe_SMS]=-" (0x6C4 bytes copied into the game's
 *     launch-info struct at startup); without it the game quits with "Wrong executable".
 *     core.c creates it.
 *  2. Seven "spot checks" run when entering certain areas. Each stores a pass/fail pair and
 *     is read through a tiny accessor `return A == B;` - six are exposed to the game scripts
 *     as SpotCheckSuccessful_1..6, the seventh is read by the loading code. Without the
 *     launcher they all fail and quests break later. We make the accessors return true.
 * Each accessor is verified byte-for-byte (with ASLR relocation) before it is changed, so any
 * other build of Fable3.exe is left untouched. */
#include "xl.h"

static const DWORD k_accessors[7] = { 0x616470, 0x616490, 0x6164B0, 0x6164D0, 0x6164F0, 0x616510, 0x616530 };
static const DWORD k_pairs[7]     = { 0x1C86C18, 0x1C86C20, 0x1C86C28, 0x1C86C30, 0x1C86C38, 0x1C86C40, 0x1C86C48 };

void xl_secu_attach(void)
{
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    DWORD delta = (DWORD)(ULONG_PTR)base - 0x400000;
    int patched = 0, skipped = 0;
    for (int i = 0; i < 7; i++) {
        BYTE *f = base + (k_accessors[i] - 0x400000);
        /* 8B 0D <A>  33 C0  3B 0D <B>  0F 94 C0  C3 */
        BYTE want[18] = { 0x8B, 0x0D, 0, 0, 0, 0, 0x33, 0xC0, 0x3B, 0x0D, 0, 0, 0, 0, 0x0F, 0x94, 0xC0, 0xC3 };
        DWORD a = k_pairs[i] + delta, b = k_pairs[i] + 4 + delta;
        memcpy(want + 2, &a, 4);
        memcpy(want + 10, &b, 4);
        if (IsBadReadPtr(f, 18) || memcmp(f, want, 18)) { skipped++; continue; }
        static const BYTE ret1[6] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };  /* mov eax,1 ; ret */
        DWORD old;
        if (VirtualProtect(f, sizeof ret1, PAGE_EXECUTE_READWRITE, &old)) {
            memcpy(f, ret1, sizeof ret1);
            VirtualProtect(f, sizeof ret1, old, &old);
            FlushInstructionCache(GetCurrentProcess(), f, sizeof ret1);
            patched++;
        }
    }
    xl_log("spot checks: %d accessors set to pass, %d not matched (different game build?)", patched, skipped);
}
