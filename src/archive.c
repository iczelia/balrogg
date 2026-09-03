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

/*  The first byte's low two bits count extra little-endian bytes. The payload
    is the assembled word shifted right by two.  */
sz arc_varint_len(u32 v) {
  return v >= 0x400000UL ? 4 : v >= 0x4000UL ? 3 : v >= 0x40UL ? 2 : 1;
}

sz arc_varint_put(u8 * p, u32 v) {
  sz i, n = arc_varint_len(v) - 1;
  u32 w;
  FATAL_UNLESS(v <= ARC_VMAX, "varint %lu out of range", (unsigned long) v);
  w = (v << 2) | (u32) n;
  Fi(n + 1, p[i] = (u8) (w >> (8 * i)));
  return n + 1;
}

u32 arc_varint_get(const u8 * p, sz len, sz * pos) {
  sz i, at = *pos, n;
  u32 w = 0;
  FATAL_UNLESS(at < len, "truncated varint at %lu", (unsigned long) at);
  n = p[at] & 3;
  FATAL_UNLESS(n < len - at, "truncated varint at %lu", (unsigned long) at);
  Fi(n + 1, w |= (u32) p[at + i] << (8 * i));
  *pos = at + n + 1;
  return w >> 2;
}

void arc_init(archive * a, u8 flags) {
  a->flags = flags;  a->n = 0;  a->cap = 4;  a->ntune = 0;
  memset(a->tune, 0, sizeof a->tune);
  a->s = xmalloc(a->cap * sizeof *a->s);
}

void arc_free(archive * a) {
  sz i;
  Fi(a->n, free(a->s[i].data));
  free(a->s);  a->s = NULL;  a->n = a->cap = 0;
}

void arc_push(archive * a, const u8 * data, sz len) {
  FATAL_UNLESS(len > 0 && len <= ARC_VMAX, "invalid stream length %lu",
               (unsigned long) len);
  if (a->n == a->cap) {
    FATAL_UNLESS(a->cap <= SIZE_MAX / 2 / sizeof *a->s,
                 "too many archive streams");
    a->cap *= 2;  a->s = xrealloc(a->s, a->cap * sizeof *a->s);
  }
  a->s[a->n].data = xmalloc(len);
  a->s[a->n].len = len;
  memcpy(a->s[a->n].data, data, len);
  a->n++;
}

void arc_parse(archive * a, const u8 * buf, sz len) {
  sz pos = ARC_HDRLEN;
  u8 ver;
  FATAL_UNLESS(len >= ARC_HDRLEN && !memcmp(buf, ARC_MAGIC, ARC_MAGLEN),
               "not a balrogg archive");
  ver = buf[ARC_MAGLEN];
  FATAL_UNLESS(ver <= ARC_VER, "unsupported archive version %u", (unsigned) ver);
  arc_init(a, buf[ARC_MAGLEN + 1]);
  if (ver >= 1) {
    FATAL_UNLESS(pos < len, "truncated tune blob");
    a->ntune = buf[pos++];
    FATAL_UNLESS(a->ntune <= ARC_TUNEMAX && a->ntune <= len - pos,
                 "invalid tune length %u", (unsigned) a->ntune);
    memcpy(a->tune, buf + pos, a->ntune);
    pos += a->ntune;
  }
  for (;;) {
    u32 sl = arc_varint_get(buf, len, &pos);
    if (!sl) break;
    FATAL_UNLESS(sl <= len - pos, "stream %lu exceeds the archive",
                 (unsigned long) a->n);
    arc_push(a, buf + pos, sl);
    pos += sl;
  }
  FATAL_UNLESS(pos == len, "%lu trailing archive bytes", (unsigned long) (len - pos));
}

u8 * arc_emit(const archive * a, sz * len) {
  sz i, n = ARC_HDRLEN + 1, at;
  u8 * b;
  FATAL_UNLESS(a->ntune <= ARC_TUNEMAX, "invalid tune length %u",
               (unsigned) a->ntune);
  if (a->ntune) n += 1 + a->ntune;
  Fi(a->n, {
    sz add;
    FATAL_UNLESS(a->s[i].len > 0 && a->s[i].len <= ARC_VMAX,
                 "invalid stream length %lu", (unsigned long) a->s[i].len);
    add = arc_varint_len((u32) a->s[i].len) + a->s[i].len;
    FATAL_UNLESS(add <= SIZE_MAX - n, "archive is too large");
    n += add;
  });
  b = xmalloc(n);
  memcpy(b, ARC_MAGIC, ARC_MAGLEN);
  b[ARC_MAGLEN] = (u8) (a->ntune ? ARC_VER : 0);
  b[ARC_MAGLEN + 1] = a->flags;  at = ARC_HDRLEN;
  if (a->ntune) { b[at++] = a->ntune;  memcpy(b + at, a->tune, a->ntune);
                  at += a->ntune; }
  Fi(a->n, at += arc_varint_put(b + at, (u32) a->s[i].len);
           memcpy(b + at, a->s[i].data, a->s[i].len);  at += a->s[i].len);
  at += arc_varint_put(b + at, 0);
  if (at != n) FATAL_CODE(BLR_EXIT_INTERNAL, "internal: archive emit size mismatch");
  *len = n;  return b;
}
