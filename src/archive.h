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

#ifndef BLR_ARCHIVE_H
#define BLR_ARCHIVE_H

#include "common.h"

/*  Header, optional tune, and zero-terminated stream list.  */
#define ARC_MAGIC     "BALROGG"
#define ARC_MAGLEN    (sizeof ARC_MAGIC - 1)
#define ARC_HDRLEN    (ARC_MAGLEN + 2)
#define ARC_VER       1             /*  supports a tune blob  */
#define ARC_TUNEMAX   8
#define ARC_VARINT_MAX 4
#define ARC_VMAX      0x3FFFFFFFUL   /*  30 payload bits.  */

/*  Flags hold pool size in bits 0 through 2, solid mode in bit 3, Opus mode in
    bit 4, and codec effort in bits 5 through 7.  */
#define ARC_SOLID(f)  (((f) >> 3) & 1)
#define ARC_BLOCK(f)  (1024UL << (((f) & 7) + 4))
#define ARC_OPUS      0x10
/*  Opus PVQ depth or Vorbis residue stages.  */
#define ARC_LEVEL(f)  (((f) >> 5) & 7)

typedef struct {
  u8 * data;
  sz len;
} arc_stream;

typedef struct {
  u8 flags;
  u8 tune[ARC_TUNEMAX];
  u8 ntune;                   /*  0 emits a version 0 header  */
  arc_stream * s;
  sz n, cap;
} archive;

void arc_init(archive * a, u8 flags);
void arc_free(archive * a);
/*  Append a copy of a stream.  */
void arc_push(archive * a, const u8 * data, sz len);
/*  Parse in place, rejecting malformed archives.  */
void arc_parse(archive * a, const u8 * buf, sz len);
/*  Return a newly allocated image.  */
u8 * arc_emit(const archive * a, sz * len);

sz arc_varint_len(u32 v);
sz arc_varint_put(u8 * p, u32 v);
u32 arc_varint_get(const u8 * p, sz len, sz * pos);

#endif
