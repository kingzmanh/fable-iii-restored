/* Fable III Restored - Nvidia driver profile for Fable3.exe (the Steam guide's FPS fix):
 * Vertical Sync = On, Triple Buffering = On. Done through NVAPI's driver settings (DRS)
 * interface, the same store the Nvidia Control Panel writes. Needs admin (the installer is).
 * Without an Nvidia driver every call fails quietly and nothing is changed. */
#include <windows.h>
#include <stdio.h>
#include "nvidia.h"

typedef int NvStatus;
typedef void *NvSession, *NvProfile;
typedef void *(__cdecl *nvapi_QueryInterface_t)(unsigned id);

#define NVAPI_OK 0
#define NV_STR 2048
#define VSYNCMODE_ID          0x00A879CF
#define VSYNCMODE_FORCEON     0x47814940
#define TRIPLE_BUFFER_ID      0x20FDD1F9
#define TRIPLE_BUFFER_ON      1
#define NVDRS_DWORD_TYPE      0

typedef struct {
    unsigned version;
    unsigned isPredefined;
    WCHAR appName[NV_STR];
    WCHAR userFriendlyName[NV_STR];
    WCHAR launcher[NV_STR];
} NVDRS_APPLICATION_V1;

typedef struct {
    unsigned version;
    WCHAR profileName[NV_STR];
    unsigned gpuSupport;
    unsigned isPredefined;
    unsigned numOfApps;
    unsigned numOfSettings;
} NVDRS_PROFILE_V1;

typedef struct {
    unsigned version;
    WCHAR settingName[NV_STR];
    unsigned settingId;
    unsigned settingType;
    unsigned settingLocation;
    unsigned isCurrentPredefined;
    unsigned isPredefinedValid;
    union { unsigned u32; BYTE bin[4100]; } predefined;
    union { unsigned u32; BYTE bin[4100]; } current;
} NVDRS_SETTING_V1;

#define VER(t, v) ((unsigned)sizeof(t) | ((v) << 16))

static nvapi_QueryInterface_t qi;
#define NV(name, id, ...) typedef NvStatus (__cdecl *name##_t)(__VA_ARGS__); static name##_t name;
NV(NvAPI_Initialize, 0x0150E828, void)
NV(NvAPI_DRS_CreateSession, 0x0694D52E, NvSession *)
NV(NvAPI_DRS_DestroySession, 0xDAD9CFF8, NvSession)
NV(NvAPI_DRS_LoadSettings, 0x375DBD6B, NvSession)
NV(NvAPI_DRS_SaveSettings, 0xFCBC7E14, NvSession)
NV(NvAPI_DRS_FindApplicationByName, 0xEEE566B2, NvSession, const WCHAR *, NvProfile *, NVDRS_APPLICATION_V1 *)
NV(NvAPI_DRS_FindProfileByName, 0x7E4A9A0B, NvSession, const WCHAR *, NvProfile *)
NV(NvAPI_DRS_CreateProfile, 0xCC176068, NvSession, NVDRS_PROFILE_V1 *, NvProfile *)
NV(NvAPI_DRS_CreateApplication, 0x4347A9DE, NvSession, NvProfile, NVDRS_APPLICATION_V1 *)
NV(NvAPI_DRS_SetSetting, 0x577DD202, NvSession, NvProfile, NVDRS_SETTING_V1 *)
NV(NvAPI_DRS_GetSetting, 0x73BF8338, NvSession, NvProfile, unsigned, NVDRS_SETTING_V1 *)
NV(NvAPI_DRS_RestoreProfileDefaultSetting, 0x53F0381E, NvSession, NvProfile, unsigned)

static int load(void)
{
    static int done = -1;
    if (done >= 0) return done;
    done = 0;
    HMODULE m = LoadLibraryW(L"nvapi.dll");   /* 32-bit installer -> 32-bit nvapi.dll */
    if (!m) return 0;
    qi = (nvapi_QueryInterface_t)(void *)GetProcAddress(m, "nvapi_QueryInterface");
    if (!qi) return 0;
#define GET(name, id) name = (name##_t)qi(id); if (!name) return 0;
    GET(NvAPI_Initialize, 0x0150E828) GET(NvAPI_DRS_CreateSession, 0x0694D52E) GET(NvAPI_DRS_DestroySession, 0xDAD9CFF8)
    GET(NvAPI_DRS_LoadSettings, 0x375DBD6B) GET(NvAPI_DRS_SaveSettings, 0xFCBC7E14)
    GET(NvAPI_DRS_FindApplicationByName, 0xEEE566B2) GET(NvAPI_DRS_FindProfileByName, 0x7E4A9A0B)
    GET(NvAPI_DRS_CreateProfile, 0xCC176068) GET(NvAPI_DRS_CreateApplication, 0x4347A9DE)
    GET(NvAPI_DRS_SetSetting, 0x577DD202) GET(NvAPI_DRS_GetSetting, 0x73BF8338)
    GET(NvAPI_DRS_RestoreProfileDefaultSetting, 0x53F0381E)
    if (NvAPI_Initialize() != NVAPI_OK) return 0;
    done = 1;
    return 1;
}

/* the profile Fable3.exe belongs to (Nvidia ships one), or a new one of ours */
static NvProfile find_profile(NvSession s, int create, wchar_t *name_out)
{
    NvProfile p = NULL;
    static NVDRS_APPLICATION_V1 app;
    memset(&app, 0, sizeof app);
    app.version = VER(NVDRS_APPLICATION_V1, 1);
    if (NvAPI_DRS_FindApplicationByName(s, L"fable3.exe", &p, &app) == NVAPI_OK && p) {
        if (name_out) wcscpy(name_out, L"Nvidia's Fable III profile");
        return p;
    }
    if (!create) return NULL;
    if (NvAPI_DRS_FindProfileByName(s, L"Fable III Restored", &p) != NVAPI_OK) {
        static NVDRS_PROFILE_V1 pr;
        memset(&pr, 0, sizeof pr);
        pr.version = VER(NVDRS_PROFILE_V1, 1);
        wcscpy(pr.profileName, L"Fable III Restored");
        if (NvAPI_DRS_CreateProfile(s, &pr, &p) != NVAPI_OK) return NULL;
    }
    memset(&app, 0, sizeof app);
    app.version = VER(NVDRS_APPLICATION_V1, 1);
    wcscpy(app.appName, L"fable3.exe");
    wcscpy(app.userFriendlyName, L"Fable III");
    NvAPI_DRS_CreateApplication(s, p, &app);
    if (name_out) wcscpy(name_out, L"new profile \"Fable III Restored\"");
    return p;
}

static int set_dword(NvSession s, NvProfile p, unsigned id, unsigned v)
{
    static NVDRS_SETTING_V1 st;
    memset(&st, 0, sizeof st);
    st.version = VER(NVDRS_SETTING_V1, 1);
    st.settingId = id;
    st.settingType = NVDRS_DWORD_TYPE;
    st.current.u32 = v;
    return NvAPI_DRS_SetSetting(s, p, &st) == NVAPI_OK;
}

int nv_apply_fps_fix(wchar_t *msg, int cch)
{
    if (!load()) { lstrcpynW(msg, L"no Nvidia driver - skipped (the VSync fix in xlive.dll covers all GPUs)", cch); return 0; }
    NvSession s = NULL;
    if (NvAPI_DRS_CreateSession(&s) != NVAPI_OK) { lstrcpynW(msg, L"Nvidia settings could not be opened", cch); return 0; }
    int ok = 0;
    wchar_t where[64] = L"";
    if (NvAPI_DRS_LoadSettings(s) == NVAPI_OK) {
        NvProfile p = find_profile(s, 1, where);
        if (p && set_dword(s, p, VSYNCMODE_ID, VSYNCMODE_FORCEON) && set_dword(s, p, TRIPLE_BUFFER_ID, TRIPLE_BUFFER_ON))
            ok = NvAPI_DRS_SaveSettings(s) == NVAPI_OK;
    }
    NvAPI_DRS_DestroySession(s);
    if (ok) swprintf(msg, cch, L"Vertical Sync = On, Triple Buffering = On (%ls)", where);
    else lstrcpynW(msg, L"could not save the Nvidia settings", cch);
    return ok;
}

int nv_remove_fps_fix(void)
{
    if (!load()) return 0;
    NvSession s = NULL;
    if (NvAPI_DRS_CreateSession(&s) != NVAPI_OK) return 0;
    int ok = 0;
    if (NvAPI_DRS_LoadSettings(s) == NVAPI_OK) {
        NvProfile p = find_profile(s, 0, NULL);
        if (p) {
            NvAPI_DRS_RestoreProfileDefaultSetting(s, p, VSYNCMODE_ID);
            NvAPI_DRS_RestoreProfileDefaultSetting(s, p, TRIPLE_BUFFER_ID);
            ok = NvAPI_DRS_SaveSettings(s) == NVAPI_OK;
        }
    }
    NvAPI_DRS_DestroySession(s);
    return ok;
}

/* read back what the driver has for Fable3.exe (tests) */
int nv_read_fps_fix(unsigned *vsync, unsigned *triple, wchar_t *where, int cch)
{
    *vsync = *triple = 0xFFFFFFFF;
    if (!load()) return 0;
    NvSession s = NULL;
    if (NvAPI_DRS_CreateSession(&s) != NVAPI_OK) return 0;
    int ok = 0;
    if (NvAPI_DRS_LoadSettings(s) == NVAPI_OK) {
        wchar_t w[64] = L"(no profile for fable3.exe)";
        NvProfile p = find_profile(s, 0, w);
        static NVDRS_SETTING_V1 st;
        if (p) {
            memset(&st, 0, sizeof st); st.version = VER(NVDRS_SETTING_V1, 1);
            if (NvAPI_DRS_GetSetting(s, p, VSYNCMODE_ID, &st) == NVAPI_OK) *vsync = st.current.u32;
            memset(&st, 0, sizeof st); st.version = VER(NVDRS_SETTING_V1, 1);
            if (NvAPI_DRS_GetSetting(s, p, TRIPLE_BUFFER_ID, &st) == NVAPI_OK) *triple = st.current.u32;
        }
        lstrcpynW(where, w, cch);
        ok = 1;
    }
    NvAPI_DRS_DestroySession(s);
    return ok;
}
