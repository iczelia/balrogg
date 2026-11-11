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

#include "fmt/arc.h"

static u32 sum_crc;

void arc_sum_set(u32 crc) { sum_crc = crc; }

u32 arc_sum_get(void) { return sum_crc; }

int arc_is(const u8 * h, sz n) {
  return n >= ARC_HDRLEN && !memcmp(h, ARC_MAGIC, ARC_MAGLEN) &&
         h[ARC_MAGLEN] == ARC_VERSION;
}

int arc_is_mp3(const u8 * h, sz n) {
  return arc_is(h, n) && h[ARC_MAGLEN + 1] == ARC_KMP3;
}

int arc_is_mod(const u8 * h, sz n) {
  return arc_is(h, n) && h[ARC_MAGLEN + 1] == ARC_KMOD;
}

int arc_is_it(const u8 * h, sz n) {
  return arc_is(h, n) && h[ARC_MAGLEN + 1] == ARC_KIT;
}

int arc_sum_peek(const u8 * h, sz n, u32 * crc) {
  sz at;
  if (!arc_is(h, n)) return 0;
  if (arc_is_mp3(h, n)) at = ARC_MP3HDR;
  else if (arc_is_mod(h, n) || arc_is_it(h, n)) at = ARC_MODHDR;
  else at = ARC_HDRLEN + ARC_LENLEN;
  if (n < at + ARC_SUMLEN) return 0;
  *crc = rd32le(h + at);
  return 1;
}

void arc_init(archive * a, int opus, int cfg) {
  a->gaps = 0;  a->size = 0;
  a->crc = arc_sum_get();
  a->backing = a->output = NULL;
  a->chunks = NULL;  a->nchunks = a->cchunks = 0;
  a->opus = opus != 0;  a->cfg = (u8) cfg;  a->n = 0;  a->cap = 4;
  a->s = xmalloc(a->cap * sizeof *a->s);
}

void arc_free(archive * a) {
  sz i;
  Fi(a->n, xfree(a->s[i].data);  if (a->s[i].owned) bf_close(a->s[i].file));
  xfree(a->chunks);  a->chunks = NULL;
  bf_close(a->backing);  a->backing = NULL;
  xfree(a->s);  a->s = NULL;  a->n = a->cap = 0;
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
  a->s = xgrow(a->s, &a->cap, a->n + 1, sizeof *a->s, 4);
  a->s[a->n].data = data;  a->s[a->n].owned = 0;
  a->s[a->n].file = NULL;  a->s[a->n].off = 0;
  a->s[a->n].len = len;
  a->n++;
}

void arc_parse(archive * a, const u8 * buf, sz len) {
  blr_file * f = bf_memory(buf, len);
  arc_read(a, f);  a->backing = f;
}

sz arc_size(const archive * a) {
  sz i, n = ARC_OGGHDR + 4;
  Fi(a->n,
    sz len = a->s[i].len, chunks = 1 + (len - 1) / BLR_IO_CHUNK;
    FATAL_UNLESS(len && chunks <= (SIZE_MAX - n) / 8 &&
                 len <= SIZE_MAX - n - 8 * chunks, "archive is too large");
    n += len + 8 * chunks);
  return n;
}

static int chunk_next(const archive * a, sz * index, arc_chunk * q) {
  sz i, at = *index;
  if (a->nchunks) {
    if (at == a->nchunks) return 0;
    *q = a->chunks[at];  (*index)++;  return 1;
  }
  Fi(a->n,
    sz count = 1 + (a->s[i].len - 1) / BLR_IO_CHUNK;
    if (at < count) {
      q->stream = (u32) i + 1;  q->off = at * BLR_IO_CHUNK;
      q->len = MIN(BLR_IO_CHUNK, a->s[i].len - q->off);
      (*index)++;  return 1;
    }
    at -= count);
  return 0;
}

static sz header(const archive * a, u8 * h) {
  memcpy(h, ARC_MAGIC, ARC_MAGLEN);
  h[ARC_MAGLEN] = ARC_VERSION;
  h[ARC_MAGLEN + 1] = (u8) ((a->gaps ? ARC_KGAPS : 0) |
                            (a->opus ? ARC_KOPUS : 0));
  h[ARC_MAGLEN + 2] = a->cfg;
  wr64le(h + ARC_HDRLEN, a->size);  wr32le(h + ARC_HDRLEN + ARC_LENLEN, a->crc);
  return ARC_OGGHDR;
}

u8 * arc_emit(const archive * a, sz * len) {
  sz n = arc_size(a), at, c = 0;
  arc_chunk q;
  u8 * b = xmalloc(n);
  at = header(a, b);
  while (chunk_next(a, &c, &q)) {
    const arc_stream * st = a->s + q.stream - 1;
    wr32le(b + at, q.stream);  wr32le(b + at + 4, (u32) q.len);
    at += 8;
    if (st->file) bf_read(st->file, st->off + q.off, b + at, q.len);
    else memcpy(b + at, st->data + q.off, q.len);
    at += q.len;
  }
  memset(b + at, 0, 4);  at += 4;
  FATAL_UNLESS(at == n, "internal: chunk archive size mismatch");
  *len = n;  return b;
}

void arc_slice(archive * a, blr_file * file, sz off, sz len) {
  arc_take(a, NULL, len);
  a->s[a->n - 1].file = file;  a->s[a->n - 1].off = off;
}

void arc_begin(archive * a, blr_file * output) {
  u8 h[ARC_OGGHDR];
  sz n;
  arc_size(a);  a->output = output;
  n = header(a, h);
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
  sz i;
  for (;;) {
    u8 h[8];
    u32 id, len;
    sz old;
    FATAL_UNLESS(pos <= file->len && file->len - pos >= 4,
                 "truncated chunk list");
    bf_readmeta(file, pos, h, 4);  pos += 4;
    id = rd32le(h);
    if (!id) break;
    FATAL_UNLESS(id <= file->len / 9 && file->len - pos >= 4,
                 "invalid chunk stream");
    bf_readmeta(file, pos, h + 4, 4);  pos += 4;
    len = rd32le(h + 4);
    FATAL_UNLESS(len && len <= BLR_IO_CHUNK && len <= file->len - pos,
                 "invalid chunk length");
    while (a->n < id) {
      blr_file * f = bf_view(file);
      arc_slice(a, f, 0, 1);  a->s[a->n - 1].owned = 1;
    }
    old = a->s[id - 1].file->len;
    FATAL_UNLESS(old % BLR_IO_CHUNK == 0,
                 "chunk follows a final partial chunk");
    bf_extent_add(a->s[id - 1].file, pos, len);
    a->s[id - 1].len = old + len;
    a->chunks = xgrow(a->chunks, &a->cchunks, a->nchunks + 1, sizeof *a->chunks, 8);
    a->chunks[a->nchunks].stream = id;
    a->chunks[a->nchunks].off = old;
    a->chunks[a->nchunks++].len = len;
    pos += len;
  }
  FATAL_UNLESS(pos == file->len, "trailing archive bytes");
  Fi(a->n, FATAL_UNLESS(a->s[i].file->len, "missing archive stream"));
}

void arc_write(const archive * a, const char * path) {
  u8 h[ARC_OGGHDR], ch[8];
  sz c = 0;
  arc_chunk q;
  blr_file * out;
  arc_size(a);
  out = bf_open(path, 1);
  bf_write(out, 0, h, header(a, h));
  while (chunk_next(a, &c, &q)) {
    const arc_stream * st = a->s + q.stream - 1;
    wr32le(ch, q.stream);  wr32le(ch + 4, (u32) q.len);
    bf_write(out, out->len, ch, sizeof ch);
    if (st->file) bf_copy(out, st->file, st->off + q.off, q.len);
    else bf_write(out, out->len, st->data + q.off, q.len);
  }
  memset(ch, 0, 4);  bf_write(out, out->len, ch, 4);  bf_close(out);
}

void arc_read(archive * a, blr_file * file) {
  u8 h[ARC_HDRLEN], c[ARC_LENLEN + ARC_SUMLEN];
  sz pos = ARC_HDRLEN;
  FATAL_UNLESS(file->len >= ARC_HDRLEN, "not a balrogg archive");
  bf_read(file, 0, h, sizeof h);
  FATAL_UNLESS(!memcmp(h, ARC_MAGIC, ARC_MAGLEN), "not a balrogg archive");
  FATAL_UNLESS(h[ARC_MAGLEN] == ARC_VERSION,
               "unsupported archive version %u", h[ARC_MAGLEN]);
  FATAL_UNLESS(!(h[ARC_MAGLEN + 1] & ~(ARC_KGAPS | ARC_KOPUS)),
               "not an Ogg archive (kind %u)", h[ARC_MAGLEN + 1]);
  arc_init(a, h[ARC_MAGLEN + 1] & ARC_KOPUS, h[ARC_MAGLEN + 2]);
  a->gaps = !!(h[ARC_MAGLEN + 1] & ARC_KGAPS);
  FATAL_UNLESS(file->len - pos >= ARC_LENLEN + ARC_SUMLEN,
               "truncated archive header");
  bf_read(file, pos, c, sizeof c);  pos += sizeof c;
  a->size = rd64le(c);
  FATAL_UNLESS((sz) a->size == a->size,
               "the original is too large for this build");
  a->crc = rd32le(c + ARC_LENLEN);
  chunk_read(a, file, pos);
}
