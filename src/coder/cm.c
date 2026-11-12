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

#include "coder/cm.h"
#include "sys/cpu.h"

short cm_str16[65536];
u8 cm_nex[256][4];
i8 cm_nexd[256];
u16 cm_squash16[4096];
u16 cm_apmid[CM_APMN];

const int cm_sqt[33] = {
  1, 2, 3, 6, 10, 16, 27, 45, 73, 120, 194, 310, 488, 747, 1101,
  1546, 2047, 2549, 2994, 3348, 3607, 3785, 3901, 3975, 4022,
  4050, 4068, 4079, 4085, 4089, 4092, 4093, 4094
};

#define CM_HUGEMIN ((sz) 1 << 20)

cm_bit_fn cm_bit = cm_bit_scalar;
cm_plain_fn cm_plain = cm_plain_scalar;
int cm_inl = 0;

static void cm_init(void) {
  int x, i, j, pi = 0, st, n;
  static int done = 0;
  if (done) return;
  done = 1;

#if defined(HAVE_SSE2)
  if (blr_cpu_sse2()) {
    cm_bit = cm_bit_sse2;  cm_plain = cm_plain_sse2;
  }
#endif
#if defined(HAVE_AVX2)
  if (blr_cpu_avx2()) {
    cm_bit = cm_bit_avx2;  cm_plain = cm_plain_avx2;
  }
#endif
  cm_inl = CM_INLINE && blr_cpu_sse2() && !blr_cpu_avx2_forced();

  for (x = -2048; x < 2048; x++) cm_squash16[x + 2048] = (u16) cm_squash(x);
  for (x = -2047; x <= 2047; x++) {
    i = cm_squash(x);
    for (j = pi; j <= i && j < 65536; j++) cm_str16[j] = (short) x;
    pi = i + 1;
  }
  for (j = pi; j < 65536; j++) cm_str16[j] = 2047;
  Fi(CM_APMN, cm_apmid[i] = (u16) cm_squash(MIN(i * 128 - 2048, 2047)));

  { static u8 ilogt[256];
    static u8 t[64][64][2];
    u32 xx = 14155776;
    int b[5];
    b[0] = 42;  b[1] = 41;  b[2] = 13;  b[3] = 6;  b[4] = 5;
    ilogt[0] = ilogt[1] = 0;
    for (i = 2; i < 256; i++) {
      xx += 774541002u / (u32) (i * 2 - 1);
      ilogt[i] = (u8) (xx >> 24);
    }
    memset(t, 0, sizeof t);
    st = 0;
    Fi(256, Fj(i + 1,
      int xv = i - j, a, r;
      int lo = MIN(xv, j), hi = MAX(xv, j);
      n = 0;
      if (!(hi < 0 || lo < 0 || hi >= 64 || lo >= 64 || lo >= 5 ||
            hi >= b[lo])) {
        if (hi + lo <= 4) {
          r = 1;
          for (a = hi + 1; a <= hi + lo; a++) r *= a;
          for (a = 2; a <= lo; a++) r /= a;
          n = r;
        } else n = 1 + (lo > 0 && hi + lo < 16);
      }
      if (n && xv < 64 && j < 64) {
        t[xv][j][0] = (u8) st;  t[xv][j][1] = (u8) n;  st += n;
      }));
    st = 0;
    Fi(64, Fj(i + 1,
      int xv = i - j, k;
      Fk(t[xv][j][1],
        int x0 = xv, y0 = j, x1 = xv, y1 = j;
        if (st < 15) {
          x0++;  y1++;
          cm_nex[st][0] = (u8) (t[x0][y0][0] + st - t[xv][j][0]);
          cm_nex[st][1] = (u8) (t[x1][y1][0] + st - t[xv][j][0]
                             + (xv > 0 ? t[xv - 1][j + 1][1] : 0));
        } else {
          int p, q, bb, sw;
          for (bb = 0; bb < 2; bb++) {
            int * px = bb ? &x1 : &x0, * py = bb ? &y1 : &y0;
            p = *px;  q = *py;  sw = 0;
            if (p < q) { int tmp = p;  p = q;  q = tmp;  sw = 1; }
            if (bb ^ sw) { q++;  if (p > 2) p = ilogt[p] / 6 - 1; }
            else         { p++;  if (q > 2) q = ilogt[q] / 6 - 1; }
            while (!t[p][q][1]) {
              if (q < 2) p--;
              else { p = (p * (q - 1) + q / 2) / q;  q--; }
            }
            if (sw) { int tmp = p;  p = q;  q = tmp; }
            *px = p;  *py = q;
          }
          cm_nex[st][0] = t[x0][y0][0];
          cm_nex[st][1] = (u8) (t[x1][y1][0] + (t[x1][y1][1] > 1));
        }
        cm_nex[st][2] = (u8) xv;  cm_nex[st][3] = (u8) j;
        st++)));
    if (st != 253)
      FATAL_CODE(BLR_EXIT_INTERNAL,
                 "internal: bit history came out at %d states, not 253", st);
    Fi(256, cm_nexd[i] = (i8) ((cm_nex[i][3] == 0) - (cm_nex[i][2] == 0))); }
}

void cm_prepare(void) { cm_init(); }

const char * cm_dispatched(void) {
  cm_init();
  return cm_inl ? "sse2 (inlined)" : blr_simd_dispatched();
}

void cm_new(cm * c, int nst, int bits, int nsel, int lr) {
  int i, k;
  FATAL_UNLESS(!c->live, "internal: the context mixer is already built");
  FATAL_UNLESS(lr >= 1 && lr <= 31,
               "internal: mixer learning rate %d out of range", lr);
  cm_init();
  c->nst = nst;  c->nsel = nsel;  c->lr = lr;
  c->hmask = ((u32) 1 << bits) - 1;
  c->e = NULL;  c->d = NULL;
  c->st = xmalloc((sz) nst * sizeof *c->st);
  if ((sz) nst << bits >= CM_HUGEMIN) bm_alloc_huge(&c->hmap, (sz) nst << bits);
  else bm_alloc(&c->hmap, (sz) nst << bits);
  Fi(nst,
    cm_stage * s = c->st + i;
    s->hist = c->hmap.p + ((sz) i << bits);
    s->hist2 = NULL;  s->sm2 = NULL;
    s->sm = xmalloc(256 * sizeof *s->sm);
    Fk(256,
      int n0 = cm_nex[k][2], n1 = cm_nex[k][3];
      u32 p;
      if (n0 == 0) n1 *= 64;
      if (n1 == 0) n0 *= 64;
      p = (u32) (65535 * (n1 + 1) / (n0 + n1 + 2));
      if (p < 1) p = 1;
      if (p > 65534) p = 65534;
      s->sm[k] = cm_sm(p, 0, k));
    s->raw = xcalloc((sz) nsel * CM_NI + CM_NI, sizeof(short));
    s->w = s->raw;
    while (((sz) s->w & 15) != 0) s->w++;
    Fk(nsel, s->w[k * CM_NI] = 32767));
  c->lrt = c->lr0 = lr;  c->nmix = 0;
  c->dq = NULL;  c->dlim = 0;
  c->apm = NULL;  c->apmn = 0;  c->actx[0] = c->actx[1] = 0;
  c->apmu = 0;  c->apms = 0;
  c->w2 = c->w2raw = NULL;  c->sel2 = 0;  c->nsel2 = 0;  c->xt = 0;
  c->h2on = 0;  c->h2b = c->h2x = 0;
  c->live = 1;
}

void cm_prior(cm * c, const short (*w)[4], int lr0) {
  int i, j, k;
  Fi(c->nst, Fk(c->nsel, Fj(4, c->st[i].w[k * CM_NI + j] = w[i][j])));
  c->lrt = c->lr0 = lr0;  c->xt = 1;
}

void cm_second(cm * c, u32 nsel2) {
  u32 i, k;
  FATAL_UNLESS(c->live && !c->w2, "internal: second weight set misuse");
  c->nsel2 = nsel2;  c->xt = 1;
  c->w2raw = xmalloc(((sz) c->nst * nsel2 + 1) * CM_NI * sizeof *c->w2raw);
  c->w2 = c->w2raw;
  while (((sz) c->w2 & 15) != 0) c->w2++;
  Fi((u32) c->nst,
    Fk(nsel2, memcpy(c->w2 + ((sz) i * nsel2 + k) * CM_NI, c->st[i].w,
                     CM_NI * sizeof *c->w2)));
}

void cm_hist2(cm * c) {
  int i;
  FATAL_UNLESS(c->live && !c->h2on, "internal: second history misuse");
  if ((sz) (c->hmask + 1) * (u32) c->nst >= CM_HUGEMIN)
    bm_alloc_huge(&c->hmap2, (sz) (c->hmask + 1) * (u32) c->nst);
  else bm_alloc(&c->hmap2, (sz) (c->hmask + 1) * (u32) c->nst);
  Fi(c->nst,
    cm_stage * s = c->st + i;
    s->hist2 = c->hmap2.p + (sz) i * (c->hmask + 1);
    s->sm2 = xmalloc(256 * sizeof *s->sm2);
    memcpy(s->sm2, s->sm, 256 * sizeof *s->sm2));
  c->h2on = 1;  c->xt = 1;
}

void cm_apm(cm * c, u32 n0, u32 n1, int staged) {
  FATAL_UNLESS(c->live && !c->apm, "internal: secondary estimation misuse");
  c->apmn = n0;  c->xt = 1;  c->apmu = 0;  c->apms = staged;
  bm_alloc(&c->apmmap, (sz) c->nst * (n0 + n1) * CM_APMN * sizeof *c->apm);
  c->apm = (u16 *) (void *) c->apmmap.p;
}

void cm_free(cm * c) {
  int i;
  if (!c->live) return;
  Fi(c->nst,
    xfree(c->st[i].sm);  xfree(c->st[i].raw);  xfree(c->st[i].sm2));
  bm_free(&c->hmap);
  if (c->h2on) bm_free(&c->hmap2);
  c->h2on = 0;
  xfree(c->st);  c->st = NULL;
  if (c->apm) bm_free(&c->apmmap);
  xfree(c->w2raw);  c->apm = NULL;  c->w2 = c->w2raw = NULL;
  c->live = 0;
}
