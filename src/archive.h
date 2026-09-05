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
#include "file.h"

/* Header + version + flags + tune length + tune, followed by chunks.
   Each chunk is a little-endian u32 stream ID (1-based), u32 length, and
   payload. A zero u32 ID terminates the archive. All but the last chunk of
   each stream are 1 MiB. Coders and probability models span chunk boundaries. */
#define ARC_MAGIC     "BALROGG"
#define ARC_MAGLEN    (sizeof ARC_MAGIC - 1)
#define ARC_HDRLEN    (ARC_MAGLEN + 2)
#define ARC_VER       3             /*  interleaved bounded chunks  */
#define ARC_TUNEMAX   8

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
  int owned;
  blr_file * file;  sz off;   /*  file slice when data is NULL  */
} arc_stream;

typedef struct { u32 stream; sz off, len; } arc_chunk;

typedef struct {
  u8 version;                 /* current format; other versions are rejected */
  u8 flags;
  u8 tune[ARC_TUNEMAX];
  u8 ntune;                   /* zero means default tune */
  arc_stream * s;
  sz n, cap;
  blr_file * backing;         /* owned memory view, if any */
  blr_file * output;          /* borrowed direct output */
  arc_chunk * chunks;
  sz nchunks, cchunks;
} archive;

void arc_init(archive * a, u8 flags);
void arc_begin(archive * a, blr_file * output);
blr_file * arc_newstream(archive * a);
void arc_finish(archive * a);
void arc_free(archive * a);
/*  Append a copy of a stream.  */
void arc_push(archive * a, const u8 * data, sz len);
/*  Append a stream, taking ownership of its allocation.  */
void arc_take(archive * a, u8 * data, sz len);
/* Parse without copying payloads; buf must live until arc_free. */
void arc_parse(archive * a, const u8 * buf, sz len);
/*  Return a newly allocated image.  */
u8 * arc_emit(const archive * a, sz * len);
sz arc_size(const archive * a);
void arc_write(const archive * a, const char * path);
void arc_read(archive * a, blr_file * file);
void arc_slice(archive * a, blr_file * file, sz off, sz len);

#endif
