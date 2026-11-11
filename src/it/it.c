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
#include "mod/mod.h"
#include "fmt/arc.h"

#define ITA_HDR (ARC_MODHDR + ARC_SUMLEN)
enum { ITE_STORE, ITE_CM };

static int peek(const char * path, u8 * b, sz max, uint64_t * size) {
  FILE * f = fopen(path, "rb");
  sz n;
  int64_t e;
  if (!f) return -1;
  n = fread(b, 1, max, f);
  e = !blr_fseek(f, 0, SEEK_END) ? blr_ftell(f) : -1;
  fclose(f);
  if (size) *size = e < 0 ? n : (uint64_t) e;
  return (int) n;
}

int it_sniff(const char * path) {
  u8 b[IT_HDR];
  uint64_t size;
  int n = peek(path, b, sizeof b, &size);
  return n == IT_HDR && !memcmp(b, "IMPM", 4) && it_tab(b) <= size;
}

int it_is_archive(const char * path) {
  u8 b[ARC_HDRLEN];
  int n = peek(path, b, sizeof b, NULL);
  return n > 0 && arc_is_it(b, (sz) n);
}

static void put_head(u8 * h, int eng, int scfg, uint64_t n) {
  memcpy(h, ARC_MAGIC, ARC_MAGLEN);
  h[ARC_MAGLEN] = ARC_VERSION;  h[ARC_MAGLEN + 1] = ARC_KIT;
  h[ARC_MAGLEN + 2] = (u8) eng;  h[ARC_MAGLEN + 3] = (u8) (scfg - MOD_SCFG0);
  wr64le(h + ARC_MAGLEN + 4, n);  wr32le(h + ARC_MODHDR, arc_sum_get());
}

static void emit(const char * out, const u8 * h, const u8 * b, sz n) {
  FILE * f = bf_fopen(out, "wb");
  int bad = 0;
  if (!f) FATAL_CODE(BLR_EXIT_IO, "cannot create %s", out);
  if (h && bf_fwrite(h, ITA_HDR, f) != ITA_HDR) bad = 1;
  if (!bad && n && bf_fwrite(b, n, f) != n) bad = 1;
  if (bf_fclose(f)) bad = 1;
  if (bad) FATAL_CODE(BLR_EXIT_IO, "write error on %s", out);
}

static const u8 LEVELS[BLR_NLEV][3] = {
  { 2, 1, 1 }, { 4, 1, 1 }, { 6, 1, 1 }, { 6, 2, 1 }, { 9, 2, 1 },
  { 9, 10, 1 }, { 42, 10, 4 }, { 33, 10, 4 }, { 33, 11, 4 }, { 36, 11, 4 },
  { 36, 12, 5 }, { 36, 15, 5 }, { 38, 15, 5 }, { 38, 16, 5 }, { 40, 16, 5 },
  { 40, 17, 2 }, { 41, 17, 2 }, { 41, 18, 2 }, { 41, 18, 2 }, { 41, 18, 2 }
};

void it_level(int level, it_cfg * c) {
  const char * d = getenv("BLR_MOD_SCFG"), * e = getenv("BLR_IT_S16"),
             * f = getenv("BLR_IT_ST");
  const u8 * l = LEVELS[CLAMP(level, 1, BLR_NLEV) - 1];
  c->s8 = d ? atoi(d) : MOD_SCFG0 + l[0];
  c->s16 = e ? atoi(e) : IT_S16C0 + l[1];
  c->st = f ? atoi(f) : IT_STC0 + l[2];
}

int it_pack(const char * in, const char * out, const it_cfg * g) {
  sz n, cl = 0;
  u8 * b = slurp(in, &n), * c = NULL, h[ITA_HDR];
  int ok = n <= IT_MAXN && it_kind(b, n) && it_cfg_ok(g);
  if (ok) {
    cl = it_code(b, n, g, &c);
    ok = cl < n;
  }
  put_head(h, ok ? ITE_CM : ITE_STORE, g->s8, n);
  emit(out, h, ok ? c : b, ok ? cl : n);
  xfree(b);  xfree(c);
  return BLR_EXIT_OK;
}

int it_unpack(const char * in, const char * out) {
  sz al;
  u8 * a = slurp(in, &al), * o;
  uint64_t n;
  int eng, scfg;
  if (al < ITA_HDR || !arc_is_it(a, al)) {
    xfree(a);  FATAL("%s: not an IT archive", in);
  }
  eng = a[ARC_MAGLEN + 2];  scfg = MOD_SCFG0 + a[ARC_MAGLEN + 3];
  n = rd64le(a + ARC_MAGLEN + 4);
  if (eng == ITE_STORE) {
    FATAL_UNLESS(n == al - ITA_HDR, "%s: stored length mismatch", in);
    emit(out, NULL, a + ITA_HDR, (sz) n);
    xfree(a);
    return BLR_EXIT_OK;
  }
  FATAL_UNLESS(eng == ITE_CM,
               "%s: unknown IT engine %d/%d", in, eng, scfg);
  FATAL_UNLESS(n <= IT_MAXN, "%s: corrupt IT archive", in);
  o = xcalloc((sz) n + 1, 1);
  if (it_decode(a + ITA_HDR, al - ITA_HDR, (sz) n, scfg, o)) {
    xfree(o);  xfree(a);
    FATAL("%s: corrupt IT archive", in);
  }
  emit(out, NULL, o, (sz) n);
  xfree(o);  xfree(a);
  return BLR_EXIT_OK;
}
