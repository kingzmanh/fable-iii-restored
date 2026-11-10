/* Fable III Restored - our xlive.dll replacement. Shared declarations.
 * Types follow the public Games for Windows - LIVE / XDK API layouts. */
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#define XAPI __stdcall

typedef uint64_t XUID;

#define XUSER_MAX_COUNT 4
#define XUSER_NAME_SIZE 16

/* XUSER_SIGNIN_STATE */
#define eXUserSigninState_NotSignedIn       0
#define eXUserSigninState_SignedInLocally   1
#define eXUserSigninState_SignedInToLive    2

#define XUSER_INFO_FLAG_LIVE_ENABLED  0x00000001
#define XUSER_INFO_FLAG_GUEST         0x00000002

/* XUSER_PRIVILEGE values (all granted) */

typedef struct XOVERLAPPED XOVERLAPPED;
typedef void (WINAPI *XOVERLAPPED_COMPLETION_ROUTINE)(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, XOVERLAPPED *pOverlapped);
struct XOVERLAPPED {
    ULONG_PTR InternalLow;
    ULONG_PTR InternalHigh;
    ULONG_PTR InternalContext;
    HANDLE    hEvent;
    XOVERLAPPED_COMPLETION_ROUTINE pCompletionRoutine;
    DWORD_PTR dwCompletionContext;
    DWORD     dwExtendedError;
};

typedef struct {
    XUID  xuid;
    DWORD dwInfoFlags;
    DWORD UserSigninState;
    DWORD dwGuestNumber;
    DWORD dwSponsorUserIndex;
    CHAR  szUserName[XUSER_NAME_SIZE];
} XUSER_SIGNIN_INFO;

/* XUSER_DATA types */
#define XUSER_DATA_TYPE_CONTEXT   0
#define XUSER_DATA_TYPE_INT32     1
#define XUSER_DATA_TYPE_INT64     2
#define XUSER_DATA_TYPE_DOUBLE    3
#define XUSER_DATA_TYPE_UNICODE   4
#define XUSER_DATA_TYPE_FLOAT     5
#define XUSER_DATA_TYPE_BINARY    6
#define XUSER_DATA_TYPE_DATETIME  7
#define XUSER_DATA_TYPE_NULL      0xFF

typedef struct {
    BYTE type;
    union {
        LONG     nData;
        LONGLONG i64Data;
        double   dblData;
        struct { DWORD cbData; LPWSTR pwszData; } string;
        float    fData;
        struct { DWORD cbData; PBYTE pbData; } binary;
        FILETIME ftData;
    };
} XUSER_DATA;

#define XSOURCE_NO_VALUE   0
#define XSOURCE_DEFAULT    1
#define XSOURCE_TITLE      2
#define XSOURCE_PERMISSION_DENIED 3

typedef struct {
    DWORD source;
    union { DWORD dwUserIndex; XUID xuid; } user;
    DWORD dwSettingId;
    XUSER_DATA data;
} XUSER_PROFILE_SETTING;

typedef struct {
    DWORD dwSettingsLen;
    XUSER_PROFILE_SETTING *pSettings;
} XUSER_READ_PROFILE_SETTING_RESULT;

#define XPROFILE_SETTING_TYPE(id)     (((id) >> 28) & 0xF)
#define XPROFILE_SETTING_SIZE(id)     (((id) >> 16) & 0xFFF)
#define XPROFILE_SETTING_ID(id)       ((id) & 0x3FFF)
#define XPROFILE_TITLE_SPECIFIC1      0x63E83FFF
#define XPROFILE_TITLE_SPECIFIC2      0x63E83FFE
#define XPROFILE_TITLE_SPECIFIC3      0x63E83FFD

typedef struct { BYTE ab[8]; } XNKID;
typedef struct { BYTE ab[16]; } XNKEY;

typedef struct {
    IN_ADDR ina;
    IN_ADDR inaOnline;
    WORD    wPortOnline;
    BYTE    abEnet[6];
    BYTE    abOnline[20];
} XNADDR;

typedef struct {
    XNKID  sessionID;
    XNADDR hostAddress;
    XNKEY  keyExchangeKey;
} XSESSION_INFO;

typedef struct {
    DWORD dwContextId;
    DWORD dwValue;
} XUSER_CONTEXT;

typedef struct {
    DWORD      dwPropertyId;
    XUSER_DATA value;
} XUSER_PROPERTY;

typedef struct {
    XSESSION_INFO info;
    DWORD dwOpenPublicSlots;
    DWORD dwOpenPrivateSlots;
    DWORD dwFilledPublicSlots;
    DWORD dwFilledPrivateSlots;
    DWORD cProperties;
    DWORD cContexts;
    XUSER_PROPERTY *pProperties;
    XUSER_CONTEXT  *pContexts;
} XSESSION_SEARCHRESULT;

typedef struct {
    DWORD dwSearchResults;
    XSESSION_SEARCHRESULT *pResults;
} XSESSION_SEARCHRESULT_HEADER;

typedef struct {
    DWORD dwUserIndex;
    DWORD dwAchievementId;
} XUSER_ACHIEVEMENT;

typedef struct {
    DWORD dwUserIndex;
    BOOL  fFromGameInvite;
    DWORD dwTitleID;
    XSESSION_INFO hostInfo;
    XUID  xuidInviter;
} XINVITE_INFO;

/* Notifications */
#define XN_SYS_FIRST            0x00000009
#define XN_SYS_UI               0x00000009
#define XN_SYS_SIGNINCHANGED    0x0000000A
#define XN_SYS_STORAGEDEVICESCHANGED 0x0000000B
#define XN_SYS_PROFILESETTINGCHANGED 0x0000000E
#define XN_SYS_MUTELISTCHANGED  0x00000011
#define XN_SYS_INPUTDEVICESCHANGED 0x00000012
#define XN_SYS_INPUTDEVICECONFIGCHANGED 0x00000013
#define XN_LIVE_CONNECTIONCHANGED 0x02000001
#define XN_LIVE_INVITE_ACCEPTED 0x02000002
#define XN_LIVE_LINK_STATE_CHANGED 0x02000003
#define XN_LIVE_CONTENT_INSTALLED 0x02000007
#define XN_LIVE_MEMBERSHIP_PURCHASED 0x02000008
#define XN_SYS_PRESENCECHANGED  0x00000001  /* placeholder area id */

#define XNOTIFY_SYSTEM     0x00000001
#define XNOTIFY_LIVE       0x00000002
#define XNOTIFY_FRIENDS    0x00000004
#define XNOTIFY_CUSTOM     0x00000008
#define XNOTIFY_XMP        0x00000020
#define XNOTIFY_MSGR       0x00000040
#define XNOTIFY_PARTY      0x00000080
#define XNOTIFY_AREA(id)   (1u << (((id) >> 25) & 0x3F))

#define XONLINE_S_LOGON_CONNECTION_ESTABLISHED  0x001510F0

/* Results */
#define ERROR_IO_INCOMPLETE_ 996
#define XONLINE_E_SESSION_NOT_FOUND 0x80155206

/* ---------- core.c ---------- */
void  xl_log(const char *fmt, ...);
void  xl_init_once(void);
const wchar_t *xl_root_dir(void);          /* folder of Fable3.exe */
const wchar_t *xl_data_dir(void);          /* our per-user data folder (profiles, achievements) */
XUID  xl_xuid(void);
const char *xl_user_name(void);
int   xl_cfg_int(const char *section, const char *key, int def);
void  xl_cfg_str(const char *section, const char *key, const char *def, char *out, int cch);

/* complete an overlapped (or return result for sync calls) */
DWORD xl_complete(XOVERLAPPED *ov, DWORD result, DWORD extended);

/* generic handles */
typedef struct xl_handle xl_handle;
typedef DWORD (*xl_enum_fn)(xl_handle *h, void *buf, DWORD cb, DWORD *pcItems);
struct xl_handle {
    DWORD      magic;
    int        kind;           /* 1 = enumerator, 2 = listener, 3 = session, 4 = content access, 5 = protected ctx */
    xl_enum_fn enumerate;
    void      *data;
    DWORD      count, pos, item_size;
    void     (*destroy)(xl_handle *h);
};
#define XL_MAGIC 0x584C5246  /* 'FRLX' */
HANDLE     xl_handle_new(int kind);
xl_handle *xl_handle_get(HANDLE h);

/* notify.c */
void xl_notify_push(DWORD id, ULONG_PTR param);

/* exports used internally */
DWORD XAPI XCloseHandle(HANDLE hObject);
int   XAPI XNetRandom(BYTE *pb, UINT cb);
int   XAPI XNetCreateKey(XNKID *pxnkid, XNKEY *pxnkey);

_Static_assert(sizeof(XOVERLAPPED) == 28, "XOVERLAPPED");
_Static_assert(sizeof(XUSER_SIGNIN_INFO) == 40, "XUSER_SIGNIN_INFO");
_Static_assert(sizeof(XNADDR) == 36, "XNADDR");
_Static_assert(sizeof(XSESSION_INFO) == 60, "XSESSION_INFO");
_Static_assert(sizeof(XUSER_DATA) == 16, "XUSER_DATA");
_Static_assert(sizeof(XUSER_PROFILE_SETTING) == 40, "XUSER_PROFILE_SETTING");
