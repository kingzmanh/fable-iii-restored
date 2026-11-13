#pragma once
#include <windows.h>
typedef struct { const wchar_t *name, *url, *sha; ULONGLONG size; const wchar_t *file; } xbl_file_t;
typedef int (*xbl_download_fn)(const xbl_file_t *f, const wchar_t *dst);   /* downloads + checks SHA-256 */
typedef void (*xbl_status_fn)(const wchar_t *fmt, ...);
int  xbl_prepare_dlc(const wchar_t *game, xbl_download_fn dl, xbl_status_fn st);
int  xbl_setup(const wchar_t *game, xbl_download_fn dl, xbl_status_fn st);
void xbl_remove(const wchar_t *game);
