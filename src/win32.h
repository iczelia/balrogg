/*  Copyright (C) 2026 Kamila Szewczyk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.  */

#ifndef BLR_WIN32_H
#define BLR_WIN32_H

#include "common.h"
#ifdef BLR_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef BLR_WIN_LEGACY
wchar_t * blr_win_wide(const char * s);
char * blr_win_utf8(const wchar_t * s);
wchar_t * blr_win_path(const char * s);
#endif
HANDLE blr_win_open(const char * path, DWORD access, DWORD share,
                    DWORD creation, DWORD attrs);
DWORD blr_win_attrs(const char * path);
char * blr_win_image(void);
BOOL blr_win_spawn(const char * image, const char * command,
                    PROCESS_INFORMATION * pi);
#endif

#endif
