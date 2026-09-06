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

/*  Exercise the native runtime, including ABI and console boundaries.  */

#include "win32.h"
#include <math.h>
#include <wchar.h>

int blr_main(int argc, char ** argv);

static int failed;

static void check(int ok, const char * what) {
  if (!ok) { fprintf(stderr, "wincrt: %s\n", what);  failed++; }
}

static void wide_strings(void) {
  SYSTEM_INFO si;
  wchar_t * page, * s;
  DWORD old;
  sz n, i;
  GetSystemInfo(&si);
  page = VirtualAlloc(NULL, 2 * si.dwPageSize,
                      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  check(page != NULL, "allocate guarded wide strings");
  if (!page) return;
  n = si.dwPageSize / sizeof *page;
  check(VirtualProtect(page + n, si.dwPageSize, PAGE_NOACCESS, &old),
        "protect the page after the string");
  for (i = 0; i + 1 < n; i++) page[i] = 0xD834;
  page[n - 1] = 0;
  s = page + n - 4;
  check(wcslen(page) == n - 1 && wcslen(s + 3) == 0,
        "wcslen stops at a page boundary");
  check(wcscmp(s, L"\xD834\xD834\xD834") == 0 && wcscmp(s, s) == 0,
        "wcscmp equal strings");
  check(wcscmp(s, L"\x7FFF") > 0 && wcscmp(L"\xFFFF", s) > 0 &&
        wcscmp(L"", s) < 0 && wcscmp(s, L"\xD834\xD834\xD834!") < 0,
        "wcscmp unsigned code units and prefixes");
  check(wcschr(s, 0xD834) == s && wcsrchr(s, 0xD834) == s + 2,
        "wide searches distinguish first and last matches");
  check(!wcschr(page, L'x') && !wcsrchr(page, L'x'),
        "wide searches stop at the terminator");
  check(wcschr(s, 0) == s + 3 && wcsrchr(s, 0) == s + 3 &&
        wcschr(s + 3, 0) == s + 3 && wcsrchr(s + 3, 0) == s + 3,
        "wide searches can find NUL, including an empty string");
  VirtualFree(page, 0, MEM_RELEASE);
}

static void formatting(void) {
  char buf[128];
  sz big = (sz) 0xFFFFFFFFU;
  int n;
#if UINTPTR_MAX > 0xFFFFFFFFU
  big += 2;
  snprintf(buf, sizeof buf, "%zu %td %lu", big, (ptrdiff_t) -9, 42UL);
  check(!strcmp(buf, "4294967297 -9 42"), "LLP64 format argument widths");
#else
  snprintf(buf, sizeof buf, "%zu %td %lu", big, (ptrdiff_t) -9, 42UL);
  check(!strcmp(buf, "4294967295 -9 42"), "ILP32 format argument widths");
#endif
  n = snprintf(buf, 5, "%s", "abcdef");
  check(n == 6 && !strcmp(buf, "abcd") &&
        snprintf(NULL, 0, "%s", "abcdef") == 6, "snprintf truncation");
  snprintf(buf, sizeof buf, "%lld %llu", -9223372036854775807LL - 1,
           18446744073709551615ULL);
  check(!strcmp(buf, "-9223372036854775808 18446744073709551615"),
        "64-bit signed and unsigned formatting");
  snprintf(buf, sizeof buf, "%7.2f %.0f %.3f", 1.25, 42.0, 0.125);
  check(!strcmp(buf, "   1.25 42 0.125"), "profiler fixed-point formatting");
  { volatile double x = 2.0;
    double error = log(x) - 0.69314718055994530942;
    check(error > -1e-15 && error < 1e-15, "profiler logarithm"); }
}

#ifndef BLR_WIN_LEGACY
static void unicode(void) {
  static const char utf8[] = "A\xC3\xA9\xF0\x9D\x84\x9E";
  static const wchar_t utf16[] = { L'A', 0xE9, 0xD834, 0xDD1E, 0 };
  wchar_t * w = blr_win_wide(utf8);
  char * s = blr_win_utf8(utf16);
  check(w && !wcscmp(w, utf16) && s && !strcmp(s, utf8),
        "UTF-8 and UTF-16 conversion including a surrogate pair");
  free(w);  free(s);
  w = blr_win_wide("\xC0\xAF");
  check(!w, "reject overlong UTF-8");  free(w);
  { const wchar_t lone[] = { 0xD800, 0 };
    s = blr_win_utf8(lone);
    check(!s, "reject an unpaired surrogate");  free(s); }
  SetEnvironmentVariableW(L"BLR_CRT_TEST", utf16);
  s = getenv("BLR_CRT_TEST");
  check(s && !strcmp(s, utf8), "getenv returns UTF-8");
  SetEnvironmentVariableW(L"BLR_CRT_TEST", NULL);
  w = blr_win_path("\\\\server\\share\\file.ogg");
  check(w && !wcscmp(w, L"\\\\?\\UNC\\server\\share\\file.ogg"),
        "extended-length UNC path conversion");
  free(w);
  w = blr_win_path("\\\\.\\NUL");
  check(!w, "reject explicit device namespaces");  free(w);
  { char * image = blr_win_image(), * cmd;
    PROCESS_INFORMATION pi;
    DWORD code = 0;
    check(image != NULL, "get the Unicode executable path");
    if (!image) return;
    cmd = malloc(strlen(image) + 32);
    check(cmd != NULL, "allocate a child command line");
    if (!cmd) { free(image);  return; }
    sprintf(cmd, "\"%s\" \"child \xF0\x9D\x84\x9E\"", image);
    if (blr_win_spawn(image, cmd, &pi)) {
      WaitForSingleObject(pi.hProcess, INFINITE);
      GetExitCodeProcess(pi.hProcess, &code);
      CloseHandle(pi.hThread);  CloseHandle(pi.hProcess);
    }
    check(code == 7, "spawn a child with a Unicode argument");
    free(cmd);  free(image);
  }
}

/*  Use an inactive console buffer so the check does not change the user's
    screen.  Calls to fputc deliberately split a four-byte UTF-8 sequence.  */
static void console(void) {
  HANDLE h, errors = GetStdHandle(STD_ERROR_HANDLE);
  wchar_t text[8], expected[8];
  DWORD got, count;
  COORD origin = { 0, 0 };
  if (!AllocConsole()) { FreeConsole();  AllocConsole(); }
  SetStdHandle(STD_ERROR_HANDLE, errors);
  h = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
  check(h != INVALID_HANDLE_VALUE, "create a console screen buffer");
  if (h == INVALID_HANDLE_VALUE) return;
  SetStdHandle(STD_OUTPUT_HANDLE, h);
  if (!WriteConsoleW(h, L"A\xD834\xDD1E" L"B", 4, &got, NULL) || got != 4 ||
      !ReadConsoleOutputCharacterW(h, expected, 8, origin, &count) || !count ||
      !FillConsoleOutputCharacterW(h, L' ', 8, origin, &got) || got != 8 ||
      !SetConsoleCursorPosition(h, origin)) {
    check(0, "write the UTF-16 console reference");  return;
  }
  fputs("A", stdout);
  fputc(0xF0, stdout);  fputc(0x9D, stdout);
  fputc(0x84, stdout);  fputc(0x9E, stdout);
  fputs("B", stdout);
  check(!fflush(stdout), "flush UTF-8 console output");
  check(ReadConsoleOutputCharacterW(h, text, 8, origin, &got) && got == count &&
        !memcmp(text, expected, got * sizeof *text),
        "console output matches WriteConsoleW");
  /*  stdout's 4096-byte buffer ends partway through the next code point.  */
  { char fill[4095];
    COORD size = { 4098, 64 }, last = { 4095, 0 };
    check(SetConsoleScreenBufferSize(h, size) &&
          SetConsoleCursorPosition(h, origin), "resize the test console");
    memset(fill, 'x', sizeof fill);
    fwrite(fill, 1, sizeof fill, stdout);
    fputc(0xC3, stdout);  fputc(0xA9, stdout);
    check(!fflush(stdout) &&
          ReadConsoleOutputCharacterW(h, text, 1, last, &got) &&
          got == 1 && text[0] == 0xE9,
          "a buffer flush preserves partial UTF-8"); }
  /*  The runtime owns the standard handle until exit flushes it.  */
}
#endif

int blr_main(int argc, char ** argv) {
#ifndef BLR_WIN_LEGACY
  if (argc == 2 && !strcmp(argv[1], "child \xF0\x9D\x84\x9E")) return 7;
  /*  Set the stdout handle before any stdio call initializes the runtime.  */
  console();
#endif
  wide_strings();
  formatting();
#ifndef BLR_WIN_LEGACY
  unicode();
#endif
  fprintf(stderr, "wincrt: %s\n", failed ? "failed" : "ok");
  return failed ? 1 : 0;
}
