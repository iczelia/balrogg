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

#include "file.h"
#ifdef BLR_WIN32
#include "win32.h"
#else
#include <sys/types.h>
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

static blr_file * new_file(void * handle, int write, sz len) {
  blr_file * f = xcalloc(1, sizeof *f);
  f->handle = handle;  f->writable = write;  f->len = len;
  return f;
}

blr_file * bf_open(const char * path, int write) {
#ifdef BLR_WIN32
  HANDLE h = blr_win_open(path,
                          write ? GENERIC_WRITE | GENERIC_READ : GENERIC_READ,
                          write ? 0 : FILE_SHARE_READ,
                          write ? CREATE_ALWAYS : OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL);
  DWORD hi = 0, lo;
  unsigned long long len;
  if (h == INVALID_HANDLE_VALUE) FATAL_CODE(BLR_EXIT_IO, "cannot %s %s", write ? "create" : "open", path);
  SetLastError(NO_ERROR);
  lo = GetFileSize(h, &hi);
  if (lo == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
    FATAL_CODE(BLR_EXIT_IO, "cannot size %s", path);
  len = ((unsigned long long) hi << 32) | lo;
  FATAL_UNLESS(len <= SIZE_MAX, "%s is too large for this build", path);
  return new_file(h, write, (sz) len);
#else
  FILE * h = fopen(path, write ? "w+b" : "rb");
  off_t len;
  if (!h) FATAL_CODE(BLR_EXIT_IO, "cannot %s %s", write ? "create" : "open", path);
  if (fseeko(h, 0, SEEK_END) || (len = ftello(h)) < 0)
    FATAL_CODE(BLR_EXIT_IO, "cannot size %s", path);
  FATAL_UNLESS((uintmax_t) len <= SIZE_MAX, "%s is too large for this build", path);
  return new_file(h, write, (sz) len);
#endif
}

/* Chunk streams borrow their parent's handle. Full chunks are appended once;
   final partial chunks are sealed by their owner's finish operation. */
blr_file * bf_memory(const u8 * data, sz len) {
  blr_file * f = new_file(NULL, 0, len);
  f->memory = data;  return f;
}

blr_file * bf_view(blr_file * parent) {
  blr_file * f = new_file(NULL, 0, 0);
  f->parent = parent;  return f;
}

blr_file * bf_stream(blr_file * parent, u32 id) {
  blr_file * f = bf_view(parent);
  f->writable = 1;  f->stream = id;  return f;
}

void bf_extent_add(blr_file * f, sz off, sz len) {
  FATAL_UNLESS(len && len <= BLR_IO_CHUNK && len <= SIZE_MAX - f->len,
               "invalid chunk length");
  if (f->next == f->cext) {
    FATAL_UNLESS(f->cext <= SIZE_MAX / 2 / sizeof *f->ext, "too many chunks");
    f->cext = f->cext ? f->cext * 2 : 4;
    f->ext = xrealloc(f->ext, f->cext * sizeof *f->ext);
  }
  f->ext[f->next].off = off;  f->ext[f->next++].len = len;
  f->len += len;
}

void bf_resize(blr_file * f, sz len) {
  FATAL_UNLESS(!f->parent && f->writable, "invalid resize");
  bf_flush(f);  seek_file(f, len);
#ifdef BLR_WIN32
  if (!SetEndOfFile(f->handle)) FATAL_CODE(BLR_EXIT_IO, "cannot truncate file");
#else
  if (fflush((FILE *) f->handle) || ftruncate(fileno((FILE *) f->handle), (off_t) len))
    FATAL_CODE(BLR_EXIT_IO, "cannot truncate file");
#endif
  f->len = len;  f->live = 0;
}

void bf_dropcache(blr_file * f) {
  bf_flush(f);  free(f->buf);  f->buf = NULL;  f->live = 0;
}

/* Overlapping moves use one reusable window and the safe copy direction. */
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
  free(b);
}

void bf_flush(blr_file * f) {
  if (!f->dirty) return;
  if (f->parent) {
    u8 h[8];  int i;
    sz at = f->parent->len;
    FATAL_UNLESS(f->stream && f->at / BLR_IO_CHUNK == f->next,
                 "chunk stream is not append-only");
    Fi(4, h[i] = (u8) (f->stream >> 8 * i);
          h[i + 4] = (u8) (f->used >> 8 * i));
    bf_write(f->parent, at, h, sizeof h);
    bf_write(f->parent, at + sizeof h, f->buf, f->used);
    /* Length already includes the live window. */
    f->len -= f->used;
    bf_extent_add(f, at + sizeof h, f->used);
    f->dirty = 0;  return;
  }
  seek_file(f, f->at);
#ifdef BLR_WIN32
  { DWORD n;
    if (!WriteFile(f->handle, f->buf, (DWORD) f->used, &n, NULL) || n != f->used)
      FATAL_CODE(BLR_EXIT_IO, "cannot write file"); }
#else
  if (fwrite(f->buf, 1, f->used, (FILE *) f->handle) != f->used)
    FATAL_CODE(BLR_EXIT_IO, "cannot write file");
#endif
  f->dirty = 0;
}

void bf_window(blr_file * f, sz at) {
  FATAL_UNLESS(at <= f->len, "file offset beyond end");
  bf_flush(f);
  f->at = at / BLR_IO_CHUNK * BLR_IO_CHUNK;
  f->used = MIN(BLR_IO_CHUNK, f->len - f->at);
  if (!f->buf) f->buf = xmalloc(BLR_IO_CHUNK);
  if (f->used) {
    if (f->parent) {
      sz idx = f->at / BLR_IO_CHUNK;
      FATAL_UNLESS(idx < f->next && f->used <= f->ext[idx].len, "invalid chunk window");
      bf_read(f->parent, f->ext[idx].off, f->buf, f->used);
    } else {
      seek_file(f, f->at);
#ifdef BLR_WIN32
      { DWORD n;
        if (!ReadFile(f->handle, f->buf, (DWORD) f->used, &n, NULL) || n != f->used)
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
  FATAL_UNLESS(f->writable && at <= f->len && n <= SIZE_MAX - at, "invalid file write");
  while (n) {
    sz take;
    if (!f->live || at < f->at || at - f->at >= BLR_IO_CHUNK) bf_window(f, at);
    take = MIN(n, BLR_IO_CHUNK - (at - f->at));
    memcpy(f->buf + at - f->at, p, take);
    at += take;  p += take;  n -= take;
    f->used = MAX(f->used, at - f->at);  f->len = MAX(f->len, at);  f->dirty = 1;
  }
}

void bf_copy(blr_file * to, blr_file * from, sz at, sz n) {
  u8 * buf = xmalloc(MIN(n, BLR_IO_CHUNK));
  while (n) {
    sz take = MIN(n, BLR_IO_CHUNK);
    bf_read(from, at, buf, take);  bf_write(to, to->len, buf, take);
    at += take;  n -= take;
  }
  free(buf);
}

void bf_close(blr_file * f) {
  if (!f) return;
  bf_flush(f);
  if (!f->parent && !f->memory) {
#ifdef BLR_WIN32
  if (!CloseHandle(f->handle)) FATAL_CODE(BLR_EXIT_IO, "cannot close file");
#else
  if (fclose((FILE *) f->handle)) FATAL_CODE(BLR_EXIT_IO, "cannot close file");
#endif
  }
  free(f->ext);  free(f->buf);  free(f);
}

/* Small framing reads should not pull a whole payload window into RAM. */
void bf_readmeta(blr_file * f, sz at, void * out, sz n) {
  FATAL_UNLESS(at <= f->len && n <= f->len - at && n <= BLR_IO_CHUNK,
               "invalid metadata read");
  if (f->parent || f->memory) { bf_read(f, at, out, n);  return; }
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
