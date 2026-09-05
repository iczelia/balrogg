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

#ifndef BLR_FILE_H
#define BLR_FILE_H
#include "common.h"

#define BLR_IO_CHUNK ((sz) 1 << 20)
typedef struct { sz off, len; } bf_extent;
typedef struct blr_file {
  void * handle;
  const u8 * memory;
  u8 * buf;
  sz len, at, used;
  int writable, dirty, live;
  struct blr_file * parent;
  bf_extent * ext;
  sz next, cext;
  u32 stream;
} blr_file;

blr_file * bf_open(const char * path, int write);
blr_file * bf_stream(blr_file * parent, u32 id);
blr_file * bf_view(blr_file * parent);
blr_file * bf_memory(const u8 * data, sz len);

void bf_extent_add(blr_file * f, sz off, sz len);
void bf_resize(blr_file * f, sz len);
void bf_dropcache(blr_file * f);
void bf_move(blr_file * f, sz to, sz from, sz n);
void bf_close(blr_file * f);
void bf_flush(blr_file * f);
void bf_window(blr_file * f, sz at);
void bf_read(blr_file * f, sz at, void * out, sz n);
void bf_write(blr_file * f, sz at, const void * in, sz n);
void bf_copy(blr_file * to, blr_file * from, sz at, sz n);
void bf_readmeta(blr_file * f, sz at, void * out, sz n);

static INLINE u8 bf_get(blr_file * f, sz at) {
  FATAL_IF_HOT(at >= f->len)("file read beyond end");
  if (f->memory) return f->memory[at];
  if (!f->live || at < f->at || at - f->at >= f->used) bf_window(f, at);
  return f->buf[at - f->at];
}
static INLINE void bf_put(blr_file * f, u8 b) {
  FATAL_IF_HOT(f->len == SIZE_MAX)("file is too large");
  if (!f->live || f->len - f->at >= BLR_IO_CHUNK) bf_window(f, f->len);
  f->buf[f->used++] = b;  f->len++;  f->dirty = 1;
}

#endif
