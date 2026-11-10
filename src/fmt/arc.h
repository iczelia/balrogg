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

#ifndef BLR_ARCHIVE_H
#define BLR_ARCHIVE_H

#include "sys/common.h"
#include "sys/file.h"

#define ARC_MAGIC     "BALROGG"
#define ARC_MAGLEN    (sizeof ARC_MAGIC - 1)
#define ARC_VERSION   0
#define ARC_HDRLEN    (ARC_MAGLEN + 3)
#define ARC_SUMLEN    4
#define ARC_LENLEN    8
#define ARC_KMP3      0x01
#define ARC_KGAPS     0x02
#define ARC_KOPUS     0x04
#define ARC_KMOD      0x08
#define ARC_OGGHDR    (ARC_HDRLEN + ARC_LENLEN + ARC_SUMLEN)
#define ARC_MP3HDR    (ARC_MAGLEN + 4 + 8)
#define ARC_MODHDR    ARC_MP3HDR

typedef struct {
  u8 * data;
  sz len;
  int owned;
  blr_file * file;  sz off;
} arc_stream;

typedef struct { u32 stream;  sz off, len; } arc_chunk;

typedef struct {
  u8 gaps;
  uint64_t size;
  u32 crc;
  u8 opus;
  u8 cfg;
  arc_stream * s;
  sz n, cap;
  blr_file * backing;
  blr_file * output;
  arc_chunk * chunks;
  sz nchunks, cchunks;
} archive;

void arc_sum_set(u32 crc);
u32 arc_sum_get(void);
int arc_sum_peek(const u8 * h, sz n, u32 * crc);
int arc_is(const u8 * h, sz n);
int arc_is_mp3(const u8 * h, sz n);
int arc_is_mod(const u8 * h, sz n);

void arc_init(archive * a, int opus, int cfg);
void arc_begin(archive * a, blr_file * output);
blr_file * arc_newstream(archive * a);
void arc_finish(archive * a);
void arc_free(archive * a);
void arc_push(archive * a, const u8 * data, sz len);
void arc_take(archive * a, u8 * data, sz len);
void arc_parse(archive * a, const u8 * buf, sz len);
u8 * arc_emit(const archive * a, sz * len);
sz arc_size(const archive * a);
void arc_write(const archive * a, const char * path);
void arc_read(archive * a, blr_file * file);
void arc_slice(archive * a, blr_file * file, sz off, sz len);

#endif
