/* Fable III Restored - the player's profile id (XUID), shared by xlive.dll and the installer.
 * Stable per Windows user per PC, so saves stay in one folder and co-op partners differ.
 * Fable3Restored.ini [Profile] XUID=<hex> overrides it (e.g. to keep an old profile's saves). */
#pragma once
#include <windows.h>
#include <stdint.h>

static uint64_t f3r_fnv64(const void *p, size_t n, uint64_t h)
{
    const BYTE *b = (const BYTE *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001B3ULL; }
    return h;
}

static uint64_t f3r_default_xuid(void)
{
    uint64_t h = 0xCBF29CE484222325ULL;
    wchar_t user[256]; DWORD n = 256;
    if (GetUserNameW(user, &n)) h = f3r_fnv64(user, n * 2, h);
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &k) == 0) {
        wchar_t guid[64]; DWORD cb = sizeof guid;
        if (RegQueryValueExW(k, L"MachineGuid", NULL, NULL, (BYTE *)guid, &cb) == 0) h = f3r_fnv64(guid, cb, h);
        RegCloseKey(k);
    }
    return 0x0009000000000000ULL | (h & 0x0000FFFFFFFFFFFFULL);
}
