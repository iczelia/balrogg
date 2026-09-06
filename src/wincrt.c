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

/*  A freestanding C runtime for native x86 Windows builds.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if !defined(BLR_WIN_CRT)
#error "wincrt.c requires the native Windows runtime"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "common.h"
#include "win32.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <wchar.h>
#ifdef BLR_PROFILE
#include <direct.h>
#include <math.h>
#endif

#pragma GCC diagnostic ignored "-Wattributes"

#define KEEP __attribute__((used))

int blr_main(int argc, char ** argv);
NORETURN void __cdecl blr_entry(void);

/*  Heap.  */
KEEP void * malloc(size_t n) {
  return HeapAlloc(GetProcessHeap(), 0, n ? n : 1);
}

KEEP void * calloc(size_t n, size_t size) {
  if (n && size > (size_t) -1 / n) return NULL;
  return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                    n && size ? n * size : 1);
}

KEEP void * realloc(void * p, size_t n) {
  if (!p) return malloc(n);
  return HeapReAlloc(GetProcessHeap(), 0, p, n ? n : 1);
}

KEEP void free(void * p) {
  if (p) HeapFree(GetProcessHeap(), 0, p);
}

/*  Memory and strings.  */
KEEP void * memcpy(void * d, const void * s, size_t n) {
  void * out = d;
  __asm__ volatile ("cld\n\trep movsb"
                    : "+D" (d), "+S" (s), "+c" (n) : : "memory");
  return out;
}

KEEP void * memmove(void * d, const void * s, size_t n) {
  void * out = d;
  if (!n) return out;
  if ((const unsigned char *) d < (const unsigned char *) s ||
      (const unsigned char *) d >= (const unsigned char *) s + n) {
    __asm__ volatile ("cld\n\trep movsb"
                      : "+D" (d), "+S" (s), "+c" (n) : : "memory");
  } else {
    d = (unsigned char *) d + n - 1;
    s = (const unsigned char *) s + n - 1;
    __asm__ volatile ("std\n\trep movsb\n\tcld"
                      : "+D" (d), "+S" (s), "+c" (n) : : "memory");
  }
  return out;
}

KEEP void * memset(void * d, int c, size_t n) {
  void * out = d;
  __asm__ volatile ("cld\n\trep stosb"
                    : "+D" (d), "+c" (n)
                    : "a" ((unsigned char) c) : "memory");
  return out;
}

KEEP int memcmp(const void * a, const void * b, size_t n) {
  const unsigned char * x = a, * y = b;
  if (!n) return 0;
  __asm__ volatile ("cld\n\trepe cmpsb"
                    : "+S" (x), "+D" (y), "+c" (n) : : "memory", "cc");
  return (int) x[-1] - (int) y[-1];
}

KEEP void * memchr(const void * s, int c, size_t n) {
  const unsigned char * p = s;
  int found;
  if (!n) return NULL;
  __asm__ volatile ("cld\n\trepne scasb"
                    : "+D" (p), "+c" (n), "=@ccz" (found)
                    : "a" ((unsigned char) c) : "memory");
  return found ? (void *) (p - 1) : NULL;
}

KEEP size_t strlen(const char * s) {
  const char * p = s;
  size_t n = (size_t) -1;
  __asm__ volatile ("cld\n\trepne scasb"
                    : "+D" (p), "+c" (n) : "a" (0) : "memory", "cc");
  return ~n - 1;
}

KEEP int strcmp(const char * a, const char * b) {
  while (*a && *a == *b) { a++;  b++; }
  return (int) (unsigned char) *a - (int) (unsigned char) *b;
}

KEEP int strncmp(const char * a, const char * b, size_t n) {
  while (n && *a && *a == *b) { a++;  b++;  n--; }
  if (!n) return 0;
  return (int) (unsigned char) *a - (int) (unsigned char) *b;
}

KEEP char * strcpy(char * d, const char * s) {
  memcpy(d, s, strlen(s) + 1);
  return d;
}

KEEP char * strcat(char * d, const char * s) {
  strcpy(d + strlen(d), s);
  return d;
}

KEEP char * strchr(const char * s, int c) {
  do { if (*s == (char) c) return (char *) s; } while (*s++);
  return NULL;
}

KEEP char * strrchr(const char * s, int c) {
  const char * r = NULL;
  do { if (*s == (char) c) r = s; } while (*s++);
  return (char *) r;
}

KEEP char * strstr(const char * h, const char * n) {
  size_t m = strlen(n);
  if (!m) return (char *) h;
  for (; *h; h++)
    if (*h == *n && !strncmp(h, n, m)) return (char *) h;
  return NULL;
}

KEEP size_t wcslen(const wchar_t * s) {
  const wchar_t * p = s;
  size_t n = (size_t) -1;
  __asm__ volatile ("cld\n\trepne scasw"
                    : "+D" (p), "+c" (n) : "a" (0) : "memory", "cc");
  return ~n - 1;
}

KEEP int wcscmp(const wchar_t * a, const wchar_t * b) {
  int r;
  __asm__ volatile ("cld\n"
                    "1:\tlodsw\n\tscasw\n\tjne 2f\n"
                    "\ttestw %%ax, %%ax\n\tjne 1b\n"
                    "\txorl %%eax, %%eax\n\tjmp 3f\n"
                    "2:\tsbbl %%eax, %%eax\n\torl $1, %%eax\n"
                    "3:"
                    : "=&a" (r), "+S" (a), "+D" (b) : : "memory", "cc");
  return r;
}

KEEP wchar_t * wcschr(const wchar_t * s, wchar_t c) {
  wchar_t v;
  __asm__ volatile ("cld\n"
                    "1:\tlodsw\n\tcmpw %w2, %%ax\n\tje 2f\n"
                    "\ttestw %%ax, %%ax\n\tjne 1b\n"
                    "2:"
                    : "=&a" (v), "+&S" (s) : "r" (c) : "memory", "cc");
  return v == c ? (wchar_t *) s - 1 : NULL;
}

KEEP wchar_t * wcsrchr(const wchar_t * s, wchar_t c) {
  const wchar_t * r;
  wchar_t v;
  __asm__ volatile ("cld\n\tmov $0, %0\n"
                    "1:\tlodsw\n\tcmpw %w3, %%ax\n\tjne 2f\n"
                    "\tlea -2(%1), %0\n"
                    "2:\ttestw %%ax, %%ax\n\tjne 1b"
                    : "=&r" (r), "+&S" (s), "=&a" (v)
                    : "r" (c) : "memory", "cc");
  return (wchar_t *) r;
}

static int blr_errno;

KEEP int * _errno(void) { return &blr_errno; }

#ifdef BLR_PROFILE
static void (* exit_fn[32])(void);
static unsigned exit_n;

KEEP int atexit(void (* fn)(void)) {
  if (exit_n == 32) return -1;
  exit_fn[exit_n++] = fn;
  return 0;
}

KEEP int _mkdir(const char * path) {
  BOOL ok;
#ifdef BLR_WIN_LEGACY
  ok = CreateDirectoryA(path, NULL);
#else
  wchar_t * w = blr_win_path(path);
  if (!w) { errno = EINVAL;  return -1; }
  ok = CreateDirectoryW(w, NULL);
  { DWORD error = GetLastError();  free(w);  SetLastError(error); }
#endif
  if (ok) return 0;
  errno = GetLastError() == ERROR_ALREADY_EXISTS ? EEXIST : EIO;
  return -1;
}

KEEP double log(double x) {
  double y;
  __asm__ volatile ("fldln2\n\tfldl %1\n\tfyl2x\n\tfstpl %0"
                    : "=m" (y) : "m" (x) : "st", "st(1)");
  return y;
}
#endif

typedef struct {
  HANDLE h;
  unsigned char * buf;
  size_t n, cap;
  int err, owned;
#if !defined(BLR_WIN_LEGACY)
  int console, need;
  u32 point, min;
#endif
} bfile;

#define BFILE(f) ((bfile *) (void *) (f))

static bfile std_files[3];
static int std_ready;

static void std_init(void) {
  static const DWORD which[3] = {
    STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE
  };
  int i;
  if (std_ready) return;
  std_ready = 1;
  Fi(3,
    std_files[i].h = GetStdHandle(which[i]);
    std_files[i].buf = NULL;  std_files[i].n = 0;
    std_files[i].cap = i == 1 ? 4096 : 0;  /*  stdout buffered, stderr not  */
    std_files[i].err = 0;  std_files[i].owned = 0);
#if !defined(BLR_WIN_LEGACY)
  Fi(3,
    DWORD mode;
    std_files[i].console = GetConsoleMode(std_files[i].h, &mode) != 0);
#endif
}

KEEP FILE * __acrt_iob_func(unsigned i) {
  std_init();
  return (FILE *) (void *) &std_files[i < 3 ? i : 2];
}

#if !defined(BLR_WIN_LEGACY)
static int console_units(bfile * f, const wchar_t * p, DWORD n) {
  while (n) {
    DWORD got;
    if (!WriteConsoleW(f->h, p, n, &got, NULL) || !got) {
      f->err = 1;  return 0;
    }
    p += got;  n -= got;
  }
  return 1;
}

static int console_write(bfile * f, const unsigned char * p, size_t n) {
  wchar_t out[512];
  size_t used = 0;
  while (n) {
    u32 cp;
    unsigned b = *p;
    if (f->need) {
      if ((b & 0xC0) == 0x80) {
        p++;  n--;
        f->point = (f->point << 6) | (b & 63);
        if (--f->need) continue;
        cp = f->point;
        if (cp < f->min || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
      } else { f->need = 0;  cp = 0xFFFD; }
    } else {
      p++;  n--;
      if (b < 0x80) cp = b;
      else if (b >= 0xC2 && b <= 0xF4) {
        f->need = b < 0xE0 ? 1 : b < 0xF0 ? 2 : 3;
        f->min = f->need == 1 ? 0x80 : f->need == 2 ? 0x800 : 0x10000;
        f->point = b & (0x7F >> f->need);
        continue;
      } else cp = 0xFFFD;
    }
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out[used++] = (wchar_t) (0xD800 | (cp >> 10));
      out[used++] = (wchar_t) (0xDC00 | (cp & 1023));
    } else out[used++] = (wchar_t) cp;
    if (used >= 510) {
      if (!console_units(f, out, (DWORD) used)) return 0;
      used = 0;
    }
  }
  return console_units(f, out, (DWORD) used);
}
#endif

static int raw_write(bfile * f, const void * p, size_t n) {
  const unsigned char * b = p;
  DWORD got;
#if !defined(BLR_WIN_LEGACY)
  if (f->console) return console_write(f, b, n);
#endif
  while (n) {
    DWORD want = n > 0x40000000 ? 0x40000000 : (DWORD) n;
    if (f->h == INVALID_HANDLE_VALUE || !f->h ||
        !WriteFile(f->h, b, want, &got, NULL) || !got) {
      f->err = 1;  return 0;
    }
    b += got;  n -= got;
  }
  return 1;
}

static int flush(bfile * f) {
  int ok = 1;
  if (f->n) { ok = raw_write(f, f->buf, f->n);  f->n = 0; }
  return ok;
}

static int flush_final(bfile * f) {
  int ok = flush(f);
#if !defined(BLR_WIN_LEGACY)
  if (f->console && f->need) {
    wchar_t replacement = 0xFFFD;
    f->need = 0;
    if (!console_units(f, &replacement, 1)) ok = 0;
  }
#endif
  return ok;
}

static size_t put(bfile * f, const void * p, size_t n) {
  if (!f->cap) return raw_write(f, p, n) ? n : 0;
  if (!f->buf) f->buf = malloc(f->cap);
  if (!f->buf) { f->err = 1;  return 0; }
  if (f->n + n > f->cap && !flush(f)) return 0;
  if (n >= f->cap) return raw_write(f, p, n) ? n : 0;
  memcpy(f->buf + f->n, p, n);  f->n += n;
  return n;
}

KEEP FILE * fopen(const char * path, const char * mode) {
  int wr = mode[0] == 'w';
  HANDLE h = blr_win_open(path, wr ? GENERIC_WRITE : GENERIC_READ,
                          wr ? 0 : FILE_SHARE_READ,
                          wr ? CREATE_ALWAYS : OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL);
  bfile * f;
  if (h == INVALID_HANDLE_VALUE) return NULL;
  f = calloc(1, sizeof *f);
  if (!f) { CloseHandle(h);  return NULL; }
  f->h = h;  f->buf = NULL;  f->n = 0;  f->cap = wr ? 65536 : 0;
  f->err = 0;  f->owned = 1;
#if !defined(BLR_WIN_LEGACY)
  { DWORD console_mode;
    f->console = GetConsoleMode(h, &console_mode) != 0; }
#endif
  return (FILE *) (void *) f;
}

KEEP int fclose(FILE * fp) {
  bfile * f = BFILE(fp);
  int bad = !flush_final(f) || f->err;
  if (!CloseHandle(f->h)) bad = 1;
  free(f->buf);
  if (f->owned) free(f);
  return bad ? EOF : 0;
}

KEEP size_t fread(void * p, size_t size, size_t count, FILE * fp) {
  bfile * f = BFILE(fp);
  unsigned char * b = p;
  size_t want = size * count, done = 0;
  DWORD got;
  while (done < want) {
    DWORD chunk = want - done > 0x40000000 ? 0x40000000 : (DWORD) (want - done);
    if (!ReadFile(f->h, b + done, chunk, &got, NULL)) { f->err = 1;  break; }
    if (!got) break;
    done += got;
  }
  return size ? done / size : 0;
}

KEEP size_t fwrite(const void * p, size_t size, size_t count, FILE * fp) {
  size_t n = size * count;
  if (!n) return 0;
  return put(BFILE(fp), p, n) == n ? count : 0;
}

KEEP int fputc(int c, FILE * fp) {
  unsigned char b = (unsigned char) c;
  return put(BFILE(fp), &b, 1) ? (int) b : EOF;
}

KEEP int putchar(int c) { return fputc(c, stdout); }

KEEP int fputs(const char * s, FILE * fp) {
  size_t n = strlen(s);
  return !n || put(BFILE(fp), s, n) == n ? 0 : EOF;
}

KEEP int puts(const char * s) {
  return fputs(s, stdout) == EOF || fputc('\n', stdout) == EOF ? EOF : 0;
}

KEEP int fflush(FILE * fp) {
  if (!fp) {
    int a, b;
    std_init();
    a = flush_final(&std_files[1]);  b = flush_final(&std_files[2]);
    return a && b ? 0 : EOF;
  }
  return flush_final(BFILE(fp)) ? 0 : EOF;
}

KEEP int ferror(FILE * fp) { return BFILE(fp)->err; }

/*  Formatting: flags, width and precision (both may be *), the h, hh, l, ll
    and z lengths, and the d, i, u, x, X, o, c, s, p and %% conversions.  */
typedef struct {
  char * s;  size_t cap, n;         /*  a string target, when s is set  */
  bfile * f;                        /*  or a stream  */
} sink;

static void emit(sink * k, const char * p, size_t n) {
  if (k->s) {
    size_t room = k->n < k->cap ? k->cap - k->n : 0;
    if (n < room) room = n;
    if (room) memcpy(k->s + k->n, p, room);
  } else if (n) put(k->f, p, n);
  k->n += n;
}

static void pad(sink * k, char c, size_t n) {
  while (n--) emit(k, &c, 1);
}

static int format(sink * k, const char * fmt, va_list ap) {
  char num[32];
  for (;;) {
    const char * run = fmt;
    int left = 0, zero = 0, plus = 0, space = 0, alt = 0, len = 0, prec = -1;
    int upper = 0, neg = 0;
    size_t width = 0, digits, body, total;
    unsigned long long v = 0;
    unsigned base = 10;
    const char * text;
    while (*fmt && *fmt != '%') fmt++;
    emit(k, run, (size_t) (fmt - run));
    if (!*fmt) break;
    fmt++;
    for (;; fmt++) {
      if (*fmt == '-') left = 1;
      else if (*fmt == '0') zero = 1;
      else if (*fmt == '+') plus = 1;
      else if (*fmt == ' ') space = 1;
      else if (*fmt == '#') alt = 1;
      else break;
    }
    if (*fmt == '*') {
      int w = va_arg(ap, int);
      if (w < 0) { left = 1;  w = -w; }
      width = (size_t) w;  fmt++;
    } else while (*fmt >= '0' && *fmt <= '9')
      width = width * 10 + (size_t) (*fmt++ - '0');
    if (*fmt == '.') {
      fmt++;  prec = 0;
      if (*fmt == '*') { prec = va_arg(ap, int);  fmt++; }
      else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
    }
    while (*fmt == 'h' || *fmt == 'l' || *fmt == 'z' ||
           *fmt == 'j' || *fmt == 't') {
      if (*fmt == 'l') len++;
      else if (*fmt == 'h') len--;
      else if (*fmt == 'z' || *fmt == 't') len = 3;
      else len = 2;
      fmt++;
    }
    switch (*fmt) {
#ifdef BLR_PROFILE
    case 'f': {
      double x = va_arg(ap, double);
      unsigned long long whole, frac, scale = 1;
      char fixed[48], * p = fixed + sizeof fixed;
      int j, places = prec < 0 ? 6 : prec;
      if (places > 9) places = 9;
      neg = x < 0;
      if (neg) x = -x;
      if (!(x >= 0) || x >= 18446744073709551616.0) {
        text = x != x ? "nan" : "inf";  body = 3;  goto string;
      }
      for (j = 0; j < places; j++) scale *= 10;
      whole = (unsigned long long) x;
      frac = (unsigned long long) ((x - (double) whole) * scale + 0.5);
      if (frac == scale) { whole++;  frac = 0; }
      for (j = 0; j < places; j++) {
        *--p = (char) ('0' + frac % 10);  frac /= 10;
      }
      if (places || alt) *--p = '.';
      do { *--p = (char) ('0' + whole % 10);  whole /= 10; } while (whole);
      if (neg || plus || space) *--p = neg ? '-' : plus ? '+' : ' ';
      body = (size_t) (fixed + sizeof fixed - p);
      if (!left && width > body) pad(k, zero ? '0' : ' ', width - body);
      emit(k, p, body);
      if (left && width > body) pad(k, ' ', width - body);
      fmt++;
      continue;
    }
#endif
    case '%': emit(k, "%", 1);  fmt++;  continue;
    case 'c':
      num[0] = (char) va_arg(ap, int);  text = num;  body = 1;  goto string;
    case 's':
      text = va_arg(ap, const char *);
      if (!text) text = "(null)";
      body = strlen(text);
      if (prec >= 0 && (size_t) prec < body) body = (size_t) prec;
    string:
      if (!left && width > body) pad(k, ' ', width - body);
      emit(k, text, body);
      if (left && width > body) pad(k, ' ', width - body);
      fmt++;
      continue;
    case 'p':
      v = (unsigned long long) (uintptr_t) va_arg(ap, void *);
      base = 16;  alt = 1;  break;
    case 'd': case 'i': {
      long long x = len == 3 ? (long long) va_arg(ap, intptr_t)
                    : len == 2 ? va_arg(ap, long long)
                    : len == 1 ? va_arg(ap, long)
                    : va_arg(ap, int);
      if (len == -1) x = (short) x;
      if (len <= -2) x = (signed char) x;
      neg = x < 0;
      v = neg ? 0ULL - (unsigned long long) x : (unsigned long long) x;
      break;
    }
    case 'X': upper = 1;  /*  fall through  */
    case 'x': base = 16;  goto unsigned_arg;
    case 'o': base = 8;   /*  fall through  */
    case 'u':
    unsigned_arg:
      v = len == 3 ? (unsigned long long) va_arg(ap, size_t)
          : len == 2 ? va_arg(ap, unsigned long long)
          : len == 1 ? va_arg(ap, unsigned long)
          : va_arg(ap, unsigned);
      if (len == -1) v = (unsigned short) v;
      if (len <= -2) v = (unsigned char) v;
      break;
    default:
      /*  An unknown conversion is printed as is.  */
      emit(k, "%", 1);
      continue;
    }
    fmt++;
    /*  Digits into num, least significant first.  */
    digits = 0;
    if (v || prec != 0) {
      do {
        unsigned d = (unsigned) (v % base);
        num[sizeof num - 1 - digits++] =
          (char) (d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
        v /= base;
      } while (v && digits < sizeof num - 4);
    }
    body = digits;
    if (prec > 0 && (size_t) prec > body) body = (size_t) prec;
    total = body + (neg || plus || space ? 1 : 0) + (alt && base == 16 ? 2 : 0);
    if (!left && !(zero && prec < 0) && width > total)
      pad(k, ' ', width - total);
    if (neg) emit(k, "-", 1);
    else if (plus) emit(k, "+", 1);
    else if (space) emit(k, " ", 1);
    if (alt && base == 16) emit(k, upper ? "0X" : "0x", 2);
    if (!left && zero && prec < 0 && width > total) pad(k, '0', width - total);
    if (body > digits) pad(k, '0', body - digits);
    emit(k, num + sizeof num - digits, digits);
    if (left && width > total) pad(k, ' ', width - total);
  }
  return k->n > INT_MAX ? INT_MAX : (int) k->n;
}

KEEP int __mingw_vsnprintf(char * s, size_t n, const char * fmt, va_list ap) {
  sink k;
  int r;
  k.s = n ? s : NULL;  k.cap = n ? n - 1 : 0;  k.n = 0;  k.f = NULL;
  if (!n) { char dummy;  k.s = &dummy;  k.cap = 0; }
  r = format(&k, fmt, ap);
  if (n) s[k.n < k.cap ? k.n : k.cap] = 0;
  return r;
}

KEEP int __mingw_vsprintf(char * s, const char * fmt, va_list ap) {
  return __mingw_vsnprintf(s, (size_t) -1, fmt, ap);
}

KEEP int __mingw_vfprintf(FILE * fp, const char * fmt, va_list ap) {
  sink k;
  int n;
  k.s = NULL;  k.cap = 0;  k.n = 0;  k.f = BFILE(fp);
  n = format(&k, fmt, ap);
  return k.f->err ? -1 : n;
}

KEEP int __mingw_vprintf(const char * fmt, va_list ap) {
  return __mingw_vfprintf(stdout, fmt, ap);
}

KEEP int __mingw_snprintf(char * s, size_t n, const char * fmt, ...) {
  va_list ap;
  int r;
  va_start(ap, fmt);  r = __mingw_vsnprintf(s, n, fmt, ap);  va_end(ap);
  return r;
}

KEEP int __mingw_sprintf(char * s, const char * fmt, ...) {
  va_list ap;
  int r;
  va_start(ap, fmt);  r = __mingw_vsprintf(s, fmt, ap);  va_end(ap);
  return r;
}

KEEP int __mingw_fprintf(FILE * fp, const char * fmt, ...) {
  va_list ap;
  int r;
  va_start(ap, fmt);  r = __mingw_vfprintf(fp, fmt, ap);  va_end(ap);
  return r;
}

KEEP int __mingw_printf(const char * fmt, ...) {
  va_list ap;
  int r;
  va_start(ap, fmt);  r = __mingw_vfprintf(stdout, fmt, ap);  va_end(ap);
  return r;
}

/*  stdlib.  */
KEEP unsigned long strtoul(const char * s, char ** end, int base) {
  const char * p = s, * start;
  unsigned long v = 0, cut;
  int neg = 0, any = 0, over = 0, d;
  unsigned lim;
  while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
  if (*p == '+' || *p == '-') neg = *p++ == '-';
  if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')
      && ((p[2] >= '0' && p[2] <= '9') ||
          ((p[2] | 32) >= 'a' && (p[2] | 32) <= 'f')))
    { p += 2;  base = 16; }
  if (base == 0) base = p[0] == '0' ? 8 : 10;
  start = p;
  cut = ULONG_MAX / (unsigned long) base;
  lim = (unsigned) (ULONG_MAX % (unsigned long) base);
  for (;; p++) {
    if (*p >= '0' && *p <= '9') d = *p - '0';
    else if ((*p | 32) >= 'a' && (*p | 32) <= 'z') d = (*p | 32) - 'a' + 10;
    else break;
    if (d >= base) break;
    any = 1;
    if (over || v > cut || (v == cut && (unsigned) d > lim)) {
      over = 1;  continue;
    }
    v = v * (unsigned long) base + (unsigned long) d;
  }
  if (end) *end = (char *) (any ? p : s);
  if (!any) p = start;
  if (over) { blr_errno = ERANGE;  return ULONG_MAX; }
  return neg ? 0UL - v : v;
}

static void swap_bytes(unsigned char * a, unsigned char * b, size_t n) {
  while (n--) { unsigned char t = *a;  *a++ = *b;  *b++ = t; }
}

KEEP void qsort(void * base, size_t n, size_t size,
                int (*cmp)(const void *, const void *)) {
  unsigned char * b = base;
  size_t i, root, child;
  if (n < 2) return;
  for (i = n / 2; i-- > 0;) {
    for (root = i; (child = 2 * root + 1) < n; root = child) {
      if (child + 1 < n && cmp(b + child * size, b + (child + 1) * size) < 0)
        child++;
      if (cmp(b + root * size, b + child * size) >= 0) break;
      swap_bytes(b + root * size, b + child * size, size);
    }
  }
  for (i = n - 1; i > 0; i--) {
    swap_bytes(b, b + i * size, size);
    for (root = 0; (child = 2 * root + 1) < i; root = child) {
      if (child + 1 < i && cmp(b + child * size, b + (child + 1) * size) < 0)
        child++;
      if (cmp(b + root * size, b + child * size) >= 0) break;
      swap_bytes(b + root * size, b + child * size, size);
    }
  }
}

KEEP char * getenv(const char * name) {
#if defined(BLR_WIN_LEGACY)
  static char slots[2][1024];
  static int next;
  char * s = slots[next];
  DWORD n = GetEnvironmentVariableA(name, s, sizeof slots[0]);
  if (!n || n >= sizeof slots[0]) return NULL;
  next ^= 1;
  return s;
#else
  static char * slots[2];
  static int next;
  wchar_t * key = blr_win_wide(name), * value;
  char * s;
  DWORD need, got;
  if (!key) return NULL;
  need = GetEnvironmentVariableW(key, NULL, 0);
  if (!need) { free(key);  return NULL; }
  value = malloc((sz) need * sizeof *value);
  if (!value) { free(key);  return NULL; }
  got = GetEnvironmentVariableW(key, value, need);
  free(key);
  if (!got || got >= need) { free(value);  return NULL; }
  s = blr_win_utf8(value);  free(value);
  if (!s) return NULL;
  free(slots[next]);  slots[next] = s;  next ^= 1;
  return s;
#endif
}

KEEP NORETURN void exit(int code) {
#ifdef BLR_PROFILE
  while (exit_n) exit_fn[--exit_n]();
#endif
  std_init();
  flush_final(&std_files[1]);
  flush_final(&std_files[2]);
  ExitProcess((UINT) code);
}

static int split(const char * cmd, char *** out) {
  char * buf = malloc(strlen(cmd) + 2), * w = buf;
  char ** argv;
  int argc = 0, cap = 16, quoted;
  const char * p = cmd;
  if (!buf) return -1;
  argv = malloc((size_t) cap * sizeof *argv);
  if (!argv) return -1;
  argv[argc++] = w;
  if (*p == '"') { p++;  while (*p && *p != '"') *w++ = *p++;  if (*p) p++; }
  else while (*p && *p != ' ' && *p != '\t') *w++ = *p++;
  *w++ = 0;
  for (;;) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) break;
    if (argc + 1 >= cap) {
      cap *= 2;  argv = realloc(argv, (size_t) cap * sizeof *argv);
      if (!argv) return -1;
    }
    argv[argc++] = w;
    quoted = 0;
    for (;;) {
      size_t bs = 0;
      while (*p == '\\') { bs++;  p++; }
      if (*p == '"') {
        while (bs >= 2) { *w++ = '\\';  bs -= 2; }
        if (bs) *w++ = '"';                    /*  an odd count escapes it  */
        else if (quoted && p[1] == '"') { *w++ = '"';  p++; }
        else quoted = !quoted;
        p++;
        continue;
      }
      while (bs--) *w++ = '\\';
      if (!*p || (!quoted && (*p == ' ' || *p == '\t'))) break;
      *w++ = *p++;
    }
    *w++ = 0;
  }
  argv[argc] = NULL;
  *out = argv;
  return argc;
}

KEEP NORETURN void __cdecl blr_entry(void) {
  char ** argv;
#if defined(BLR_WIN_LEGACY)
  int argc = split(GetCommandLineA(), &argv);
#else
  char * cmd = blr_win_utf8(GetCommandLineW());
  int argc;
  if (!cmd) ExitProcess(BLR_EXIT_INTERNAL);
  argc = split(cmd, &argv);
  free(cmd);
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif
  if (argc < 0) ExitProcess(BLR_EXIT_INTERNAL);
  exit(blr_main(argc, argv));
}
