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

#include "it/it.h"

sz it_tab(const u8 * h) {
  return IT_HDR + rd16le(h + 0x20) +
         4 * ((sz) rd16le(h + 0x22) + rd16le(h + 0x24) + rd16le(h + 0x26));
}

int it_kind(const u8 * b, sz n) {
  return n >= IT_HDR && !memcmp(b, "IMPM", 4) && it_tab(b) <= n;
}

void it_lay_init(it_lay * L, const u8 * h, sz n) {
  u32 i;
  memset(L, 0, sizeof *L);
  L->n = n;  L->tab = it_tab(h);
  L->on = rd16le(h + 0x20);  L->inn = rd16le(h + 0x22);
  L->sn = rd16le(h + 0x24);  L->pn = rd16le(h + 0x26);
  L->ne = L->inn + 2 * L->sn + L->pn + 1;
  L->e = xcalloc(L->ne, sizeof *L->e);
  L->ord = xmalloc(L->ne * sizeof *L->ord);
  Fi(L->ne,
    it_ent * e = L->e + i;
    if (i < L->inn) { e->k = IK_INS;  e->i = i; }
    else if (i < L->inn + L->sn) { e->k = IK_SHD;  e->i = i - L->inn; }
    else if (i < L->inn + L->sn + L->pn) {
      e->k = IK_PAT;  e->i = i - L->inn - L->sn;
    } else if (i + 1 < L->ne) {
      e->k = IK_SMP;  e->i = i - L->inn - L->sn - L->pn;
    } else e->k = IK_MSG);
}

void it_lay_free(it_lay * L) {
  u32 i;
  if (!L->e) return;
  Fi(L->ne, xfree(L->e[i].d));
  xfree(L->e);  xfree(L->ord);
  L->e = NULL;  L->ord = NULL;
}

int it_smp_has(const u8 * s) { return (s[18] & 1) && rd32le(s + 48); }

uint64_t it_smp_raw(const u8 * s) {
  return (uint64_t) rd32le(s + 48) * ((s[18] >> 2 & 1) + 1) *
         ((s[18] >> 1 & 1) + 1);
}

uint64_t it_smp_comp(const u8 * b, sz n, sz at, const u8 * s) {
  uint64_t per = s[18] & 2 ? 0x4000 : 0x8000, ch = (s[18] >> 2 & 1) + 1,
           e = ((uint64_t) rd32le(s + 48) + per - 1) / per * ch,
           j, q = 0, m = at < n ? n - at : 0;
  for (j = 0; j < e && q + 2 <= m; j++) {
    uint64_t l = rd16le(b + at + q);
    q += 2;  q += MIN(l, m - q);
  }
  return q;
}

static const it_ent * SE;

static int byat(const void * x, const void * y) {
  const it_ent * a = SE + *(const u32 *) x, * b = SE + *(const u32 *) y;
  if (a->at != b->at) return a->at < b->at ? -1 : 1;
  if (a->len != b->len) return a->len > b->len ? -1 : 1;
  return *(const u32 *) x < *(const u32 *) y ? -1 : 1;
}

static void cand(it_lay * L, u32 j, uint64_t off, uint64_t len, int clip) {
  it_ent * e = L->e + j;
  e->v = (u32) off;
  if (!off || off < L->tab || off >= L->n || !len) return;
  if (off + len > L->n) {
    if (!clip) return;
    len = L->n - off;
  }
  e->at = (sz) off;  e->len = (sz) len;  e->st = IS_PRI;
}

static u32 find(const it_lay * L, sz off) {
  u32 lo = 0, hi = L->np;
  while (lo < hi) {
    u32 mid = (lo + hi) / 2;
    if (L->e[L->ord[mid]].at < off) lo = mid + 1; else hi = mid;
  }
  return lo < L->np && L->e[L->ord[lo]].at == off ? L->ord[lo] : L->ne;
}

void it_parse(it_lay * L, const u8 * b) {
  const sz n = L->n, t0 = IT_HDR + L->on;
  u32 i, j, nc = 0, * c = xmalloc(L->ne * sizeof *c);
  sz pos = L->tab;
  Fi(L->inn + L->sn + L->pn,
    it_ent * e = L->e + i;
    uint64_t off = rd32le(b + t0 + 4 * (sz) i), len;
    e->st = IS_ZERO;
    len = e->k == IK_INS ? IT_INS : e->k == IK_SHD ? IT_SHD
        : off + IT_PATH <= n ? IT_PATH + (uint64_t) rd16le(b + off) : 0;
    cand(L, i, off, len, 0));
  Fi(L->sn,
    const it_ent * h = L->e + it_eshd(L, i);
    u32 k = it_esmp(L, i);
    const u8 * s = b + h->at;
    if (h->st != IS_PRI || !it_smp_has(s)) continue;
    L->e[k].st = IS_ZERO;
    cand(L, k, rd32le(s + 72), s[18] & 8 ? it_smp_comp(b, n, rd32le(s + 72), s)
                                         : it_smp_raw(s), !(s[18] & 8)));
  if (rd16le(b + 0x36)) {
    L->e[L->ne - 1].st = IS_ZERO;
    cand(L, L->ne - 1, rd32le(b + 0x38), rd16le(b + 0x36), 0);
  }
  Fi(L->ne, if (L->e[i].st == IS_PRI) c[nc++] = i);
  SE = L->e;
  qsort(c, nc, sizeof *c, byat);
  Fi(nc,
    it_ent * e = L->e + c[i];
    if (e->at < pos) { e->st = IS_ZERO;  continue; }
    pos = e->at + e->len);
  Fi(L->sn,
    if (L->e[it_eshd(L, i)].st != IS_PRI) L->e[it_esmp(L, i)].st = IS_NONE);
  L->np = 0;
  Fi(nc, if (L->e[c[i]].st == IS_PRI) L->ord[L->np++] = c[i]);
  Fi(L->ne,
    it_ent * e = L->e + i;
    if (e->st != IS_ZERO || !e->v) continue;
    j = find(L, e->v);
    if (j < L->ne) { e->st = IS_ALIAS;  e->v = j; }
    else e->st = IS_RAW);
  L->clip = 0;
  if (L->np) {
    const it_ent * e = L->e + L->ord[L->np - 1];
    const u8 * s = b + L->e[it_eshd(L, e->i)].at;
    if (e->k == IK_SMP && !(s[18] & 8) && e->at + it_smp_raw(s) > n)
      L->clip = e->len;
  }
  xfree(c);
}

static u32 offv(const it_lay * L, const it_ent * e) {
  switch (e->st) {
  case IS_PRI: return (u32) e->at;
  case IS_ALIAS: return (u32) L->e[e->v].at;
  case IS_RAW: return e->v;
  }
  return 0;
}

void it_offsets(const it_lay * L, u8 * h) {
  const sz t0 = IT_HDR + L->on;
  u32 i;
  Fi(L->inn + L->sn + L->pn, wr32le(h + t0 + 4 * (sz) i, offv(L, L->e + i)));
  Fi(L->sn,
    const it_ent * e = L->e + it_esmp(L, i), * s = L->e + it_eshd(L, i);
    if (e->st != IS_NONE) wr32le(s->d + 72, offv(L, e)));
  if (L->e[L->ne - 1].st != IS_NONE)
    wr32le(h + 0x38, offv(L, L->e + L->ne - 1));
}
