/*  Copyright (C) 2026 Kamila Szewczyk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "sys/file.h"
#include "sys/crc.h"
#ifdef BLR_WIN32
#include "sys/win32.h"
#else
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef BLR_DOS
#define fseeko fseek
#define ftello ftell
#endif
#endif

static void seek_file(blr_file * f, sz at) {
#ifdef BLR_WIN32
  unsigned long long off = at;
  LONG hi = (LONG) (off >> 32);
  DWORD lo;
  SetLastError(NO_ERROR);
  lo = SetFilePointer(f->handle, (LONG) off, &hi, FILE_BEGIN);
  if (lo == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
    FATAL_CODE(BLR_EXIT_IO, "cannot seek file");
#else
  if (fseeko((FILE *) f->handle, (off_t) at, SEEK_SET))
    FATAL_CODE(BLR_EXIT_IO, "cannot seek file");
#endif
}

#define DOOM_N 8
static struct {
  const char * path;  void * handle;  int made, stdio;
} doom[DOOM_N];

static struct { const void * handle;  u32 crc;  sz len;  int on, bad; } osum;

void bf_doom(const char * path) {
  memset(doom, 0, sizeof doom);  doom[0].path = path;
  memset(&osum, 0, sizeof osum);
}

void bf_doom_temp(const char * path, int on) {
  int i;
  for (i = 1; i < DOOM_N; i++)
    if (on ? !doom[i].path : doom[i].path == path) {
      doom[i].path = on ? path : NULL;  doom[i].handle = NULL;
      doom[i].made = 0;
      return;
    }
}

static int doom_find(const char * path) {
  int i;
  Fi(DOOM_N, if (doom[i].path && !strcmp(doom[i].path, path)) return i);
  return -1;
}

static int regular(const char * path) {
#ifdef BLR_WIN32
  HANDLE h = blr_win_open(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          OPEN_EXISTING, 0);
  int r;
  if (h == INVALID_HANDLE_VALUE) return 0;
  r = GetFileType(h) == FILE_TYPE_DISK;
  CloseHandle(h);
  return r;
#else
  struct stat st;
  return !stat(path, &st) && S_ISREG(st.st_mode);
#endif
}

int bf_special(const char * path) {
#ifdef BLR_WIN32
  HANDLE h = blr_win_open(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          OPEN_EXISTING, 0);
  int r;
  if (h == INVALID_HANDLE_VALUE) return 0;
  r = GetFileType(h) != FILE_TYPE_DISK;
  CloseHandle(h);
  return r;
#else
  struct stat st;
  return !stat(path, &st) && !S_ISREG(st.st_mode);
#endif
}

static void doom_made(const char * path, void * handle, int stdio) {
  int i = doom_find(path);
  if (i >= 0) {
    doom[i].made = 1;  doom[i].handle = handle;  doom[i].stdio = stdio;
  }
  if (!i) {
    memset(&osum, 0, sizeof osum);
    if (!regular(path)) { osum.handle = handle;  osum.on = 1; }
  }
}

static void doom_closed(const void * handle) {
  int i;
  Fi(DOOM_N, if (doom[i].handle == handle) doom[i].handle = NULL);
  if (osum.handle == handle) osum.handle = NULL;
}

static void osum_add(const u8 * p, sz at, sz n) {
  if (at > osum.len) { osum.bad = 1;  return; }
  if (n <= osum.len - at) return;
  osum.crc = crc32c(osum.crc, p + (osum.len - at), n - (osum.len - at));
  osum.len = at + n;
}

FILE * bf_fopen(const char * path, const char * mode) {
  FILE * f = fopen(path, mode);
  if (f && *mode == 'w') doom_made(path, f, 1);
  return f;
}

#ifdef BLR_WIN32
static void sync_path(const char * path);
#else
static void sync_stream(FILE * f);
#endif

int bf_fclose(FILE * f) {
  int out = doom[0].handle == f, r;
#ifndef BLR_WIN32
  if (out) sync_stream(f);
#endif
  doom_closed(f);
  r = fclose(f);
#ifdef BLR_WIN32
  if (out && !r && regular(doom[0].path)) sync_path(doom[0].path);
#endif
  return r;
}

sz bf_fwrite(const void * p, sz n, FILE * f) {
  sz k = fwrite(p, 1, n, f);
  if (osum.handle == f) osum_add(p, osum.len, k);
  return k;
}

int bf_crc_output(u32 * crc) {
  *crc = osum.crc;
  return osum.on && !osum.bad;
}

void bf_doomed_remove(void) {
  int i;
  for (i = 0; i < DOOM_N; i++) {
    const char * path = doom[i].path;
    void * h = doom[i].handle;
    if (!path || !doom[i].made) continue;
    doom[i].path = NULL;  doom[i].handle = NULL;
    if (h && doom[i].stdio) fclose((FILE *) h);
#ifdef BLR_WIN32
    else if (h) CloseHandle(h);
#else
    else if (h) fclose((FILE *) h);
#endif
    if (regular(path)) remove(path);
  }
}

static blr_file * new_file(void * handle, int write, sz len) {
  blr_file * f = xcalloc(1, sizeof *f);
  f->handle = handle;  f->writable = write;  f->len = f->disklen = len;
  f->cap = SIZE_MAX;  f->nomap = blr_no_mmap;
#ifdef BLR_NO_MMAP
  f->nomap = 1;
#endif
  return f;
}

blr_file * bf_open(const char * path, int write) {
  blr_file * f;
#ifdef BLR_WIN32
  HANDLE h = blr_win_open(path,
                          write ? GENERIC_WRITE | GENERIC_READ : GENERIC_READ,
                          write ? 0 : FILE_SHARE_READ,
                          write ? CREATE_ALWAYS : OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL);
  DWORD hi = 0, lo;
  unsigned long long len;
  if (h == INVALID_HANDLE_VALUE)
    FATAL_CODE(BLR_EXIT_IO, "cannot %s %s", write ? "create" : "open", path);
  SetLastError(NO_ERROR);
  lo = GetFileSize(h, &hi);
  if (lo == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
    FATAL_CODE(BLR_EXIT_IO, "cannot size %s", path);
  len = ((unsigned long long) hi << 32) | lo;
  FATAL_UNLESS(len <= SIZE_MAX, "%s is too large for this build", path);
  f = new_file(h, write, (sz) len);
#else
  FILE * h = fopen(path, write ? "w+b" : "rb");
  off_t len;
  if (!h)
    FATAL_CODE(BLR_EXIT_IO, "cannot %s %s", write ? "create" : "open", path);
  if (fseeko(h, 0, SEEK_END) || (len = ftello(h)) < 0)
    FATAL_CODE(BLR_EXIT_IO, "cannot size %s", path);
  FATAL_UNLESS((uintmax_t) len <= SIZE_MAX,
               "%s is too large for this build", path);
  f = new_file(h, write, (sz) len);
#endif
  if (!write && bm_file(&f->map, f->handle, 0, f->len, 0)) f->memory = f->map.p;
  if (write) doom_made(path, f->handle, 0);
  return f;
}

blr_file * bf_memory(const u8 * data, sz len) {
  blr_file * f = new_file(NULL, 0, len);
  f->memory = data;  return f;
}

blr_file * bf_heap(void) {
  blr_file * f = new_file(NULL, 1, 0);
  f->hcap = BLR_IO_CHUNK;  f->heap = xcalloc(f->hcap, 1);
  return f;
}

blr_file * bf_view(blr_file * parent) {
  blr_file * f = new_file(NULL, 0, 0);
  f->parent = parent;  return f;
}

blr_file * bf_splice(blr_file * parent) {
  blr_file * f = bf_view(parent);
  f->xs = xmalloc(4 * sizeof *f->xs);  f->cext = 4;
  f->ext = xmalloc(4 * sizeof *f->ext);
  return f;
}

void bf_splice_add(blr_file * f, sz off, sz len) {
  if (!len) return;
  FATAL_UNLESS(len <= SIZE_MAX - f->len, "file is too large");
  if (f->next == f->cext) {
    FATAL_UNLESS(f->cext <= SIZE_MAX / 2 / sizeof *f->ext, "too many extents");
    f->cext *= 2;
    f->ext = xrealloc(f->ext, f->cext * sizeof *f->ext);
    f->xs = xrealloc(f->xs, f->cext * sizeof *f->xs);
  }
  f->ext[f->next].off = off;  f->ext[f->next].len = len;
  f->xs[f->next++] = f->len;
  f->len += len;
}

static void splice_read(blr_file * f, sz at, u8 * out, sz n) {
  sz lo = 0, hi = f->next;
  while (hi - lo > 1) {
    sz mid = lo + (hi - lo) / 2;
    if (f->xs[mid] <= at) lo = mid;
    else hi = mid;
  }
  while (n) {
    sz off = at - f->xs[lo], take = MIN(n, f->ext[lo].len - off);
    bf_read(f->parent, f->ext[lo].off + off, out, take);
    at += take;  out += take;  n -= take;  lo++;
  }
}

blr_file * bf_stream(blr_file * parent, u32 id) {
  blr_file * f = bf_view(parent);
  f->writable = 1;  f->stream = id;  return f;
}

void bf_extent_add(blr_file * f, sz off, sz len) {
  FATAL_UNLESS(len && len <= BLR_IO_CHUNK && len <= SIZE_MAX - f->len,
               "invalid chunk length");
  f->ext = xgrow(f->ext, &f->cext, f->next + 1, sizeof *f->ext, 4);
  f->ext[f->next].off = off;  f->ext[f->next++].len = len;
  f->len += len;
}

static void heap_reserve(blr_file * f, sz n) {
  sz old = f->hcap;
  f->heap = xgrow(f->heap, &f->hcap, n, 1, BLR_IO_CHUNK);
  memset(f->heap + old, 0, f->hcap - old);
}

static int size_file(blr_file * f, sz len) {
  if (f->heap) {
    if (len > f->disklen) {
      heap_reserve(f, len);
      memset(f->heap + f->disklen, 0, MIN(len, f->hcap) - f->disklen);
    }
    f->disklen = len;  return 1;
  }
#ifdef BLR_WIN32
  seek_file(f, len);
  if (!SetEndOfFile(f->handle)) return 0;
#else
  { off_t o = (off_t) len;
    if (o < 0 || (uintmax_t) o != len) return 0;
    if (fflush((FILE *) f->handle) ||
        ftruncate(fileno((FILE *) f->handle), o)) return 0; }
#endif
  f->disklen = len;  return 1;
}

static void drop_window(blr_file * f) {
  if (f->heap) f->buf = NULL;
  if (f->map.p) {
    bm_free(&f->map);  f->memory = NULL;  f->buf = NULL;
  }
  f->live = 0;
}

void bf_cap(blr_file * f, sz len) { f->cap = len; }

void bf_resize(blr_file * f, sz len) {
  FATAL_UNLESS(!f->parent && f->writable, "invalid resize");
  FATAL_UNLESS(len <= f->cap,
               "the output would exceed the length the archive records");
  if (f->handle && f->handle == osum.handle && len < osum.len) osum.bad = 1;
  bf_flush(f);  drop_window(f);
  if (!size_file(f, len)) FATAL_CODE(BLR_EXIT_IO, "cannot truncate file");
  f->len = len;
}

void bf_dropcache(blr_file * f) {
  bf_flush(f);  drop_window(f);
  if (!f->heap) xfree(f->buf);
  f->buf = NULL;
}

void bf_move(blr_file * f, sz to, sz from, sz n) {
  u8 * b = xmalloc(MIN(n, BLR_IO_CHUNK));
  sz left = n;
  while (left) {
    sz take = MIN(left, BLR_IO_CHUNK);
    sz off = to > from ? left - take : n - left;
    bf_read(f, from + off, b, take);
    bf_write(f, to + off, b, take);
    left -= take;
  }
  xfree(b);
}

void bf_flush(blr_file * f) {
  if (!f->dirty) return;
  if (f->parent) {
    u8 h[8];
    sz at = f->parent->len;
    FATAL_UNLESS(f->stream && f->at / BLR_IO_CHUNK == f->next,
                 "chunk stream is not append-only");
    wr32le(h, f->stream);  wr32le(h + 4, (u32) f->used);
    bf_write(f->parent, at, h, sizeof h);
    bf_write(f->parent, at + sizeof h, f->buf, f->used);
    f->len -= f->used;
    bf_extent_add(f, at + sizeof h, f->used);
    f->dirty = 0;  return;
  }
  if (f->heap) {
    f->disklen = MAX(f->disklen, f->at + f->used);
    f->dirty = 0;  return;
  }
  if (f->map.p) {
    if (!bm_flush(&f->map)) FATAL_CODE(BLR_EXIT_IO, "cannot flush mapped file");
    if (f->handle == osum.handle) osum.bad = 1;
    f->dirty = 0;  return;
  }
  seek_file(f, f->at);
#ifdef BLR_WIN32
  { DWORD n;
    if (!WriteFile(f->handle, f->buf, (DWORD) f->used, &n, NULL) ||
        n != f->used)
      FATAL_CODE(BLR_EXIT_IO, "cannot write file"); }
#else
  if (fwrite(f->buf, 1, f->used, (FILE *) f->handle) != f->used)
    FATAL_CODE(BLR_EXIT_IO, "cannot write file");
#endif
  if (f->handle == osum.handle) osum_add(f->buf, f->at, f->used);
  f->disklen = MAX(f->disklen, f->at + f->used);
  f->dirty = 0;
}

void bf_window(blr_file * f, sz at) {
  FATAL_UNLESS(at <= f->len, "file offset beyond end");
  bf_flush(f);  drop_window(f);
  f->at = at / BLR_IO_CHUNK * BLR_IO_CHUNK;
  f->used = MIN(BLR_IO_CHUNK, f->len - f->at);
  if (f->heap) {
    heap_reserve(f, f->at + BLR_IO_CHUNK);
    f->buf = f->heap + f->at;  f->live = 1;  return;
  }
  if (f->handle && !f->nomap) {
    sz n = f->writable ? MIN(BLR_IO_CHUNK, SIZE_MAX - f->at) : f->used;
    if ((!f->writable || f->at + n <= f->disklen || size_file(f, f->at + n))
        && bm_file(&f->map, f->handle, f->at, n, f->writable)) {
      xfree(f->buf);  f->buf = f->map.p;  f->live = 1;  return;
    }
    f->nomap = 1;
  }
  if (!f->buf) f->buf = xmalloc(BLR_IO_CHUNK);
  if (f->used) {
    if (f->xs) splice_read(f, f->at, f->buf, f->used);
    else if (f->parent) {
      sz idx = f->at / BLR_IO_CHUNK;
      FATAL_UNLESS(idx < f->next && f->used <= f->ext[idx].len,
                   "invalid chunk window");
      bf_read(f->parent, f->ext[idx].off, f->buf, f->used);
    } else {
      seek_file(f, f->at);
#ifdef BLR_WIN32
      { DWORD n;
        if (!ReadFile(f->handle, f->buf, (DWORD) f->used, &n, NULL) ||
            n != f->used)
          FATAL_CODE(BLR_EXIT_IO, "cannot read file"); }
#else
      if (fread(f->buf, 1, f->used, (FILE *) f->handle) != f->used)
        FATAL_CODE(BLR_EXIT_IO, "cannot read file");
#endif
    }
  }
  f->live = 1;
}

void bf_read(blr_file * f, sz at, void * out, sz n) {
  u8 * p = out;
  FATAL_UNLESS(at <= f->len && n <= f->len - at, "file read beyond end");
  if (f->memory) { memcpy(out, f->memory + at, n);  return; }
  if (f->xs) { splice_read(f, at, p, n);  return; }
  if (f->parent && !f->writable) {
    while (n) {
      sz idx = at / BLR_IO_CHUNK, off = at % BLR_IO_CHUNK;
      sz take = MIN(n, f->ext[idx].len - off);
      bf_read(f->parent, f->ext[idx].off + off, p, take);
      at += take;  p += take;  n -= take;
    }
    return;
  }
  while (n) {
    sz take;
    if (!f->live || at < f->at || at - f->at >= f->used) bf_window(f, at);
    take = MIN(n, f->used - (at - f->at));
    memcpy(p, f->buf + at - f->at, take);
    at += take;  p += take;  n -= take;
  }
}

void bf_write(blr_file * f, sz at, const void * in, sz n) {
  const u8 * p = in;
  FATAL_UNLESS(f->writable && at <= f->len && n <= SIZE_MAX - at,
               "invalid file write");
  FATAL_IF_HOT(n > f->cap || at > f->cap - n)
    ("the output would exceed the length the archive records");
  if (f->handle && f->handle == osum.handle && at < osum.len) osum.bad = 1;
  while (n) {
    sz take;
    if (!f->live || at < f->at || at - f->at >= BLR_IO_CHUNK) bf_window(f, at);
    take = MIN(n, BLR_IO_CHUNK - (at - f->at));
    memcpy(f->buf + at - f->at, p, take);
    at += take;  p += take;  n -= take;
    f->used = MAX(f->used, at - f->at);  f->len = MAX(f->len, at);
    f->dirty = 1;
  }
}

void bf_copy(blr_file * to, blr_file * from, sz at, sz n) {
  u8 * buf;
  FATAL_UNLESS(at <= from->len && n <= from->len - at, "file copy beyond end");
  if (from->memory) { bf_write(to, to->len, from->memory + at, n);  return; }
  buf = xmalloc(MIN(n, BLR_IO_CHUNK));
  while (n) {
    sz take = MIN(n, BLR_IO_CHUNK);
    bf_read(from, at, buf, take);  bf_write(to, to->len, buf, take);
    at += take;  n -= take;
  }
  xfree(buf);
}

#ifdef BLR_WIN32
static void sync_handle(HANDLE h) {
  if (!FlushFileBuffers(h)) {
    DWORD e = GetLastError();
    if (e != ERROR_ACCESS_DENIED && e != ERROR_PRIVILEGE_NOT_HELD &&
        e != ERROR_INVALID_FUNCTION && e != ERROR_NOT_SUPPORTED)
      FATAL_CODE(BLR_EXIT_IO, "cannot sync file");
  }
}

static void sync_path(const char * path) {
  HANDLE h = blr_win_open(path, GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, 0);
  if (h == INVALID_HANDLE_VALUE) return;
  sync_handle(h);
  CloseHandle(h);
}
#else
static void sync_stream(FILE * f) {
  if (fflush(f)) FATAL_CODE(BLR_EXIT_IO, "cannot flush file");
#ifdef HAVE_FSYNC
  { int r;
    do r = fsync(fileno(f));  while (r && errno == EINTR);
    if (r && errno != EACCES && errno != EPERM && errno != EINVAL &&
        errno != ENOSYS
#ifdef ENOTSUP
        && errno != ENOTSUP
#endif
       ) FATAL_CODE(BLR_EXIT_IO, "cannot sync file"); }
#endif
}
#endif

static void sync_file(blr_file * f) {
#ifdef BLR_WIN32
  sync_handle(f->handle);
#else
  sync_stream((FILE *) f->handle);
#endif
}

void bf_close(blr_file * f) {
  if (!f) return;
  bf_flush(f);  drop_window(f);
  if (f->handle) {
    doom_closed(f->handle);
    if (f->writable) {
      if (f->disklen != f->len && !size_file(f, f->len))
        FATAL_CODE(BLR_EXIT_IO, "cannot truncate file");
      sync_file(f);
    }
#ifdef BLR_WIN32
    if (!CloseHandle(f->handle)) FATAL_CODE(BLR_EXIT_IO, "cannot close file");
#else
    if (fclose((FILE *) f->handle))
      FATAL_CODE(BLR_EXIT_IO, "cannot close file");
#endif
  }
  if (f->heap) xfree(f->heap);
  else xfree(f->buf);
  xfree(f->ext);  xfree(f->xs);  xfree(f);
}

void bf_readmeta(blr_file * f, sz at, void * out, sz n) {
  FATAL_UNLESS(at <= f->len && n <= f->len - at && n <= BLR_IO_CHUNK,
               "invalid metadata read");
  if (f->parent || f->memory || f->map.p || f->heap) {
    bf_read(f, at, out, n);  return;
  }
  bf_flush(f);  seek_file(f, at);
#ifdef BLR_WIN32
  { DWORD got;
    if (!ReadFile(f->handle, out, (DWORD) n, &got, NULL) || got != n)
      FATAL_CODE(BLR_EXIT_IO, "cannot read file"); }
#else
  if (fread(out, 1, n, (FILE *) f->handle) != n)
    FATAL_CODE(BLR_EXIT_IO, "cannot read file");
#endif
}

int bf_crc_path(const char * path, u32 * crc) {
  blr_file * f;
  sz at;
  if (!regular(path)) return 0;
  f = bf_open(path, 0);
  *crc = 0;
  if (f->memory) *crc = crc32c(0, f->memory, f->len);
  else for (at = 0; at < f->len; at += f->used) {
    bf_window(f, at);  *crc = crc32c(*crc, f->buf, f->used);
  }
  bf_close(f);
  return 1;
}
