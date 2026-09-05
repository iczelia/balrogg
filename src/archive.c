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

#include "archive.h"

void arc_init(archive * a, u8 flags) {
  a->version = ARC_VER;
  a->backing = a->output = NULL;
  a->chunks = NULL;  a->nchunks = a->cchunks = 0;
  a->flags = flags;  a->n = 0;  a->cap = 4;  a->ntune = 0;
  memset(a->tune, 0, sizeof a->tune);
  a->s = xmalloc(a->cap * sizeof *a->s);
}

void arc_free(archive * a) {
  sz i;
  Fi(a->n, free(a->s[i].data); if (a->s[i].owned) bf_close(a->s[i].file));
  free(a->chunks); a->chunks = NULL;
  bf_close(a->backing);  a->backing = NULL;
  free(a->s);  a->s = NULL;  a->n = a->cap = 0;
}

void arc_push(archive * a, const u8 * data, sz len) {
  u8 * copy;
  FATAL_UNLESS(len > 0, "invalid stream length %lu",
               (unsigned long) len);
  copy = xmalloc(len);
  memcpy(copy, data, len);
  arc_take(a, copy, len);
}

void arc_take(archive * a, u8 * data, sz len) {
  FATAL_UNLESS(len > 0, "invalid stream length %lu",
               (unsigned long) len);
  if (a->n == a->cap) {
    FATAL_UNLESS(a->cap <= SIZE_MAX / 2 / sizeof *a->s,
                 "too many archive streams");
    a->cap *= 2;  a->s = xrealloc(a->s, a->cap * sizeof *a->s);
  }
  a->s[a->n].data = data;  a->s[a->n].owned = 0;
  a->s[a->n].file = NULL;  a->s[a->n].off = 0;
  a->s[a->n].len = len;
  a->n++;
}

void arc_parse(archive * a, const u8 * buf, sz len) {
  blr_file * f = bf_memory(buf, len);
  arc_read(a, f); a->backing = f;
}

sz arc_size(const archive * a) {
  sz i, n = ARC_HDRLEN + 1 + a->ntune + 4;
  FATAL_UNLESS(a->version == ARC_VER, "unsupported archive version %u", a->version);
  FATAL_UNLESS(a->ntune <= ARC_TUNEMAX, "invalid tune length %u", a->ntune);
  Fi(a->n,
    sz len = a->s[i].len, chunks = 1 + (len - 1) / BLR_IO_CHUNK;
    FATAL_UNLESS(len && chunks <= (SIZE_MAX - n) / 8 &&
                 len <= SIZE_MAX - n - 8 * chunks, "archive is too large");
    n += len + 8 * chunks);
  return n;
}

/* Parsed archives retain their physical order. Newly generated archives may
   also be emitted with each logical stream's chunks grouped together. */
static int chunk_next(const archive * a, sz * index, arc_chunk * q) {
  sz i, at = *index;
  if (a->nchunks) {
    if (at == a->nchunks) return 0;
    *q = a->chunks[at]; (*index)++; return 1;
  }
  Fi(a->n,
    sz count = 1 + (a->s[i].len - 1) / BLR_IO_CHUNK;
    if (at < count) {
      q->stream = (u32) i + 1; q->off = at * BLR_IO_CHUNK;
      q->len = MIN(BLR_IO_CHUNK, a->s[i].len - q->off);
      (*index)++; return 1;
    }
    at -= count);
  return 0;
}

u8 * arc_emit(const archive * a, sz * len) {
  sz n = arc_size(a), at = ARC_HDRLEN, c = 0, j;
  arc_chunk q;
  u8 * b = xmalloc(n);
  memcpy(b, ARC_MAGIC, ARC_MAGLEN);
  b[ARC_MAGLEN] = ARC_VER; b[ARC_MAGLEN + 1] = a->flags;
  b[at++] = a->ntune; memcpy(b + at, a->tune, a->ntune); at += a->ntune;
  while (chunk_next(a, &c, &q)) {
    const arc_stream * st = a->s + q.stream - 1;
    Fj(4, b[at + j] = (u8) (q.stream >> (8 * j));
          b[at + 4 + j] = (u8) (q.len >> (8 * j)));
    at += 8;
    if (st->file) bf_read(st->file, st->off + q.off, b + at, q.len);
    else memcpy(b + at, st->data + q.off, q.len);
    at += q.len;
  }
  memset(b + at, 0, 4); at += 4;
  FATAL_UNLESS(at == n, "internal: chunk archive size mismatch");
  *len = n; return b;
}

void arc_slice(archive * a, blr_file * file, sz off, sz len) {
  arc_take(a, NULL, len);
  a->s[a->n - 1].file = file;  a->s[a->n - 1].off = off;
}

void arc_begin(archive * a, blr_file * output) {
  u8 h[ARC_HDRLEN + 1 + ARC_TUNEMAX];
  sz n = ARC_HDRLEN;
  arc_size(a); a->output = output;
  memcpy(h, ARC_MAGIC, ARC_MAGLEN);
  h[ARC_MAGLEN] = ARC_VER;  h[ARC_MAGLEN + 1] = a->flags;
  h[n++] = a->ntune;  memcpy(h + n, a->tune, a->ntune);  n += a->ntune;
  bf_write(output, output->len, h, n);
}

blr_file * arc_newstream(archive * a) {
  blr_file * f;
  FATAL_UNLESS(a->n < 0xFFFFFFFFUL, "too many archive streams");
  f = bf_stream(a->output, (u32) a->n + 1);
  arc_slice(a, f, 0, 1);  a->s[a->n - 1].owned = 1;
  return f;
}

void arc_finish(archive * a) {
  sz i;
  static const u8 end[4] = { 0, 0, 0, 0 };
  Fi(a->n,
    bf_flush(a->s[i].file);  a->s[i].len = a->s[i].file->len;
    FATAL_UNLESS(a->s[i].len, "empty archive stream"));
  bf_write(a->output, a->output->len, end, sizeof end);
}

static void chunk_read(archive * a, blr_file * file, sz pos) {
  for (;;) {
    u8 h[8];
    u32 id = 0, len = 0;
    sz i, old;
    FATAL_UNLESS(pos <= file->len && file->len - pos >= 4, "truncated chunk list");
    bf_readmeta(file, pos, h, 4);  pos += 4;
    Fi(4, id |= (u32) h[i] << (8 * i));
    if (!id) break;
    FATAL_UNLESS(id <= file->len / 9 && file->len - pos >= 4, "invalid chunk stream");
    bf_readmeta(file, pos, h + 4, 4);  pos += 4;
    Fi(4, len |= (u32) h[i + 4] << (8 * i));
    FATAL_UNLESS(len && len <= BLR_IO_CHUNK && len <= file->len - pos,
                 "invalid chunk length");
    while (a->n < id) {
      blr_file * f = bf_view(file);
      arc_slice(a, f, 0, 1);  a->s[a->n - 1].owned = 1;
    }
    old = a->s[id - 1].file->len;
    FATAL_UNLESS(old % BLR_IO_CHUNK == 0, "chunk follows a final partial chunk");
    bf_extent_add(a->s[id - 1].file, pos, len);
    a->s[id - 1].len = old + len;
    if (a->nchunks == a->cchunks) {
      FATAL_UNLESS(a->cchunks <= SIZE_MAX / 2 / sizeof *a->chunks, "too many chunks");
      a->cchunks = a->cchunks ? a->cchunks * 2 : 8;
      a->chunks = xrealloc(a->chunks, a->cchunks * sizeof *a->chunks);
    }
    a->chunks[a->nchunks].stream = id;
    a->chunks[a->nchunks].off = old;
    a->chunks[a->nchunks++].len = len;
    pos += len;
  }
  FATAL_UNLESS(pos == file->len, "trailing archive bytes");
  { sz i; Fi(a->n, FATAL_UNLESS(a->s[i].file->len, "missing archive stream")); }
}

void arc_write(const archive * a, const char * path) {
  u8 h[ARC_HDRLEN + 1 + ARC_TUNEMAX], ch[8];
  sz n = ARC_HDRLEN, c = 0, j;
  arc_chunk q;
  blr_file * out;
  arc_size(a);
  out = bf_open(path, 1);
  memcpy(h, ARC_MAGIC, ARC_MAGLEN);
  h[ARC_MAGLEN] = ARC_VER; h[ARC_MAGLEN + 1] = a->flags;
  h[n++] = a->ntune; memcpy(h + n, a->tune, a->ntune); n += a->ntune;
  bf_write(out, 0, h, n);
  while (chunk_next(a, &c, &q)) {
    const arc_stream * st = a->s + q.stream - 1;
    Fj(4, ch[j] = (u8) (q.stream >> (8 * j));
          ch[j + 4] = (u8) (q.len >> (8 * j)));
    bf_write(out, out->len, ch, sizeof ch);
    if (st->file) bf_copy(out, st->file, st->off + q.off, q.len);
    else bf_write(out, out->len, st->data + q.off, q.len);
  }
  memset(ch, 0, 4); bf_write(out, out->len, ch, 4); bf_close(out);
}

void arc_read(archive * a, blr_file * file) {
  u8 h[ARC_HDRLEN];
  sz pos = ARC_HDRLEN;
  FATAL_UNLESS(file->len >= ARC_HDRLEN, "not a balrogg archive");
  bf_read(file, 0, h, sizeof h);
  FATAL_UNLESS(!memcmp(h, ARC_MAGIC, ARC_MAGLEN), "not a balrogg archive");
  FATAL_UNLESS(h[ARC_MAGLEN] == ARC_VER, "unsupported archive version %u", h[ARC_MAGLEN]);
  arc_init(a, h[ARC_MAGLEN + 1]);
  FATAL_UNLESS(pos < file->len, "truncated tune blob");
  a->ntune = bf_get(file, pos++);
  FATAL_UNLESS(a->ntune <= ARC_TUNEMAX && a->ntune <= file->len - pos,
               "invalid tune length %u", a->ntune);
  bf_read(file, pos, a->tune, a->ntune); pos += a->ntune;
  chunk_read(a, file, pos);
}
