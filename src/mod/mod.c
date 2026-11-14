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

#include "mod/mod.h"
#include "fmt/arc.h"

#define MDA_HDR (ARC_MODHDR + ARC_SUMLEN)
enum { MDE_STORE, MDE_CM };

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

int mod_sniff(const char * path) {
  u8 b[MOD_HDR31];
  uint64_t size;
  int n = peek(path, b, sizeof b, &size);
  return n > 0 && mod_kind(b, (sz) n, size) != 0;
}

int mod_is_archive(const char * path) {
  u8 b[ARC_HDRLEN];
  int n = peek(path, b, sizeof b, NULL);
  return n > 0 && arc_is_mod(b, (sz) n);
}

static const u8 LEVELS[BLR_NLEV] = {
  1, 1, 1, 1, 1, 25, 25, 25, 25, 25, 25, 25, 25, 23, 23, 23, 23, 23, 23, 23
}, SLEVELS[BLR_NLEV] = {
  2, 4, 6, 7, 9, 10, 11, 12, 42, 31, 33, 34, 34, 36, 37, 38, 40, 41, 41, 41
};

int mod_level_scfg(int level) {
  return MOD_SCFG0 + SLEVELS[CLAMP(level, 1, BLR_NLEV) - 1];
}

int mod_level_cfg(int level) {
  return MOD_CFG0 + LEVELS[CLAMP(level, 1, BLR_NLEV) - 1];
}

static void put_head(u8 * h, int eng, int cfg, uint64_t n) {
  memcpy(h, ARC_MAGIC, ARC_MAGLEN);
  h[ARC_MAGLEN] = ARC_VERSION;  h[ARC_MAGLEN + 1] = ARC_KMOD;
  h[ARC_MAGLEN + 2] = (u8) eng;  h[ARC_MAGLEN + 3] = (u8) (cfg - MOD_CFG0);
  wr64le(h + ARC_MAGLEN + 4, n);  wr32le(h + ARC_MODHDR, arc_sum_get());
}

static void emit(const char * out, const u8 * h, const u8 * b, sz n) {
  FILE * f = bf_fopen(out, "wb");
  int bad = 0;
  if (!f) FATAL_CODE(BLR_EXIT_IO, "cannot create %s", out);
  if (h && bf_fwrite(h, MDA_HDR, f) != MDA_HDR) bad = 1;
  if (!bad && n && bf_fwrite(b, n, f) != n) bad = 1;
  if (bf_fclose(f)) bad = 1;
  if (bad) FATAL_CODE(BLR_EXIT_IO, "write error on %s", out);
}

int mod_pack(const char * in, const char * out, int cfg, int scfg) {
  sz n, cl = 0;
  u8 * b = slurp(in, &n), * c = NULL, h[MDA_HDR];
  int ok = n <= MOD_MAXN && mod_kind(b, n, n) && mod_config(cfg) &&
           mod_sconfig(scfg);
  if (ok) {
    cl = mod_code(b, n, cfg, scfg, &c);
    ok = cl < n;
  }
  put_head(h, ok ? MDE_CM : MDE_STORE, cfg, n);
  emit(out, h, ok ? c : b, ok ? cl : n);
  xfree(b);  xfree(c);
  return BLR_EXIT_OK;
}

int mod_unpack(const char * in, const char * out) {
  sz al;
  u8 * a = slurp(in, &al), * o;
  uint64_t n;
  int eng, cfg;
  if (al < MDA_HDR || !arc_is_mod(a, al)) {
    xfree(a);  FATAL("%s: not a MOD archive", in);
  }
  eng = a[ARC_MAGLEN + 2];  cfg = MOD_CFG0 + a[ARC_MAGLEN + 3];
  n = rd64le(a + ARC_MAGLEN + 4);
  if (eng == MDE_STORE) {
    FATAL_UNLESS(n == al - MDA_HDR, "%s: stored length mismatch", in);
    emit(out, NULL, a + MDA_HDR, (sz) n);
    xfree(a);
    return BLR_EXIT_OK;
  }
  FATAL_UNLESS(eng == MDE_CM && mod_config(cfg),
               "%s: unknown MOD engine %d/%d", in, eng, cfg);
  FATAL_UNLESS(n <= MOD_MAXN, "%s: corrupt MOD archive", in);
  o = xcalloc((sz) n + 1, 1);
  if (mod_decode(a + MDA_HDR, al - MDA_HDR, (sz) n, cfg, o)) {
    xfree(o);  xfree(a);
    FATAL("%s: corrupt MOD archive", in);
  }
  emit(out, NULL, o, (sz) n);
  xfree(o);  xfree(a);
  return BLR_EXIT_OK;
}
