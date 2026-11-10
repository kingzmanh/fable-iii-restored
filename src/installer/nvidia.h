#pragma once
#include <wchar.h>
int nv_apply_fps_fix(wchar_t *msg, int cch);
int nv_remove_fps_fix(void);
int nv_read_fps_fix(unsigned *vsync, unsigned *triple, wchar_t *where, int cch);
