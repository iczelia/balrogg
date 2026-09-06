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

#include "win32.h"
#include <wchar.h>

#ifndef BLR_WIN_LEGACY
wchar_t * blr_win_wide(const char * s) {
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
  wchar_t * w;
  if (!n) return NULL;
  w = malloc((sz) n * sizeof *w);
  if (!w) { SetLastError(ERROR_NOT_ENOUGH_MEMORY);  return NULL; }
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n)) {
    DWORD error = GetLastError();
    free(w);  SetLastError(error);  return NULL;
  }
  return w;
}

char * blr_win_utf8(const wchar_t * w) {
  int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, -1,
                             NULL, 0, NULL, NULL);
  char * s;
  if (!n) return NULL;
  s = malloc((sz) n);
  if (!s) { SetLastError(ERROR_NOT_ENOUGH_MEMORY);  return NULL; }
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, -1,
                          s, n, NULL, NULL)) {
    DWORD error = GetLastError();
    free(s);  SetLastError(error);  return NULL;
  }
  return s;
}

wchar_t * blr_win_path(const char * s) {
  wchar_t * raw = blr_win_wide(s), * full, * out;
  DWORD need, got, error;
  sz len;
  if (!raw) return NULL;
  len = wcslen(raw);
  if (len >= 4 &&
      ((raw[0] == L'\\' && raw[1] == L'\\' &&
        (raw[2] == L'?' || raw[2] == L'.') && raw[3] == L'\\') ||
       (raw[0] == L'\\' && raw[1] == L'?' &&
        raw[2] == L'?' && raw[3] == L'\\'))) {
    free(raw);  SetLastError(ERROR_INVALID_NAME);  return NULL;
  }
  need = GetFullPathNameW(raw, 0, NULL, NULL);
  if (!need || need > 32760) {
    error = need ? ERROR_FILENAME_EXCED_RANGE : GetLastError();
    free(raw);  SetLastError(error);  return NULL;
  }
  full = malloc(((sz) need + 1) * sizeof *full);
  if (!full) {
    free(raw);  SetLastError(ERROR_NOT_ENOUGH_MEMORY);  return NULL;
  }
  got = GetFullPathNameW(raw, need + 1, full, NULL);
  error = GetLastError();  free(raw);
  if (!got || got > need) {
    free(full);
    SetLastError(got ? ERROR_FILENAME_EXCED_RANGE : error);
    return NULL;
  }
  out = malloc(((sz) got + 7) * sizeof *out);
  if (!out) {
    free(full);  SetLastError(ERROR_NOT_ENOUGH_MEMORY);  return NULL;
  }
  if (got >= 2 && full[0] == L'\\' && full[1] == L'\\') {
    memcpy(out, L"\\\\?\\UNC\\", 8 * sizeof *out);
    memcpy(out + 8, full + 2, ((sz) got - 1) * sizeof *out);
  } else {
    memcpy(out, L"\\\\?\\", 4 * sizeof *out);
    memcpy(out + 4, full, ((sz) got + 1) * sizeof *out);
  }
  free(full);
  return out;
}
#endif

HANDLE blr_win_open(const char * path, DWORD access, DWORD share,
                    DWORD creation, DWORD attrs) {
#ifdef BLR_WIN_LEGACY
  return CreateFileA(path, access, share, NULL, creation, attrs, NULL);
#else
  wchar_t * w = blr_win_path(path);
  HANDLE h;
  DWORD error;
  if (!w) return INVALID_HANDLE_VALUE;
  h = CreateFileW(w, access, share, NULL, creation, attrs, NULL);
  error = GetLastError();  free(w);  SetLastError(error);
  return h;
#endif
}

DWORD blr_win_attrs(const char * path) {
#ifdef BLR_WIN_LEGACY
  return GetFileAttributesA(path);
#else
  wchar_t * w = blr_win_path(path);
  DWORD attrs, error;
  if (!w) return INVALID_FILE_ATTRIBUTES;
  attrs = GetFileAttributesW(w);
  error = GetLastError();  free(w);  SetLastError(error);
  return attrs;
#endif
}

char * blr_win_image(void) {
#ifdef BLR_WIN_LEGACY
  char buf[MAX_PATH], * out;
  DWORD n = GetModuleFileNameA(NULL, buf, sizeof buf);
  if (!n || n >= sizeof buf) return NULL;
  out = malloc((sz) n + 1);
  if (out) memcpy(out, buf, (sz) n + 1);
  return out;
#else
  DWORD cap = 256;
  while (cap <= 32768) {
    wchar_t * w = malloc((sz) cap * sizeof *w);
    DWORD got;
    char * out;
    if (!w) return NULL;
    got = GetModuleFileNameW(NULL, w, cap);
    if (got && got < cap) { out = blr_win_utf8(w);  free(w);  return out; }
    free(w);
    if (!got) return NULL;
    cap *= 2;
  }
  SetLastError(ERROR_FILENAME_EXCED_RANGE);
  return NULL;
#endif
}

BOOL blr_win_spawn(const char * image, const char * command,
                    PROCESS_INFORMATION * pi) {
  BOOL ok;
  DWORD error;
#ifdef BLR_WIN_LEGACY
  STARTUPINFOA si;
  char * cmd = malloc(strlen(command) + 1);
  if (!cmd) return FALSE;
  strcpy(cmd, command);
  memset(&si, 0, sizeof si);  si.cb = sizeof si;
  ok = CreateProcessA(image, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, pi);
  error = GetLastError();  free(cmd);
#else
  STARTUPINFOW si;
  wchar_t * app = blr_win_wide(image), * cmd;
  if (!app) return FALSE;
  cmd = blr_win_wide(command);
  if (!cmd) {
    error = GetLastError();  free(app);
    SetLastError(error);  return FALSE;
  }
  memset(&si, 0, sizeof si);  si.cb = sizeof si;
  ok = CreateProcessW(app, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, pi);
  error = GetLastError();  free(app);  free(cmd);
#endif
  SetLastError(error);
  return ok;
}
