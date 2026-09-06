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


#include "cm.h"
#include "cpu.h"

/*  Stretch table and bit-history state transitions.  */

short cm_str16[65536];
u8 cm_nex[256][4];
i8 cm_nexd[256];
u16 cm_squash16[4096];

const int cm_sqt[33] = {
  1, 2, 3, 6, 10, 16, 27, 45, 73, 120, 194, 310, 488, 747, 1101,
  1546, 2047, 2549, 2994, 3348, 3607, 3785, 3901, 3975, 4022,
  4050, 4068, 4079, 4085, 4089, 4092, 4093, 4094
};

cm_bit_fn cm_bit = cm_bit_scalar;

static void cm_init(void) {
  int x, i, j, pi = 0, st, n;
  static int done = 0;
  if (done) return;
  done = 1;

#if defined(HAVE_SSE2)
  if (blr_cpu_sse2()) cm_bit = cm_bit_sse2;
#endif

  for (x = -2048; x < 2048; x++) cm_squash16[x + 2048] = (u16) cm_squash(x);
  for (x = -2047; x <= 2047; x++) {
    i = cm_squash(x);
    for (j = pi; j <= i && j < 65536; j++) cm_str16[j] = (short) x;
    pi = i + 1;
  }
  for (j = pi; j < 65536; j++) cm_str16[j] = 2047;

  { static u8 ilogt[65536];
    static u8 t[64][64][2];
    u32 xx = 14155776;
    int b[5];
    b[0] = 42;  b[1] = 41;  b[2] = 13;  b[3] = 6;  b[4] = 5;
    ilogt[0] = ilogt[1] = 0;
    for (i = 2; i < 65536; i++) {
      xx += 774541002u / (u32) (i * 2 - 1);
      ilogt[i] = (u8) (xx >> 24);
    }
    memset(t, 0, sizeof t);
    st = 0;
    Fi(256, Fj(i + 1,
      int xv = i - j, a, r;
      /*  Count the states needed for xv zero bits and j one bits.  */
      int lo = xv < j ? xv : j, hi = xv < j ? j : xv;
      n = 0;
      if (!(hi < 0 || lo < 0 || hi >= 64 || lo >= 64 || lo >= 5 || hi >= b[lo])) {
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
          /*  Find the next counts after a zero bit and after a one bit.  */
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
      FATAL_CODE(BLR_EXIT_INTERNAL, "internal: bit history came out at %d states, not 253", st);
    Fi(256, cm_nexd[i] = (i8) ((cm_nex[i][3] == 0) - (cm_nex[i][2] == 0))); }
}

/*  The match model's rolling hash.  */
#define MMUL  773u

/*  Return MMUL raised to CM_MMIN so the oldest digit can be subtracted
    from the rolling hash.  */
static u32 mmul_out(void) {
  u32 m = 1;
  int i;
  Fi(CM_MMIN, m *= MMUL);
  return m;
}

void cm_new(cm * c, int nst, int bits, int nsel, int lr, int lim) {
  int i, k;
  FATAL_UNLESS(!c->live, "internal: the context mixer is already built");
  cm_init();
  c->nst = nst;  c->nsel = nsel;  c->lr = lr;  c->lim = lim;
  c->hmask = ((u32) 1 << bits) - 1;
  c->e = NULL;  c->d = NULL;
  c->st = xmalloc((sz) nst * sizeof *c->st);
  Fi(nst,
    cm_stage * s = c->st + i;
    s->hist = xcalloc((sz) 1 << bits, 1);
    /*  Estimate the probability of a one bit from each state's bit counts.  */
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
    /*  Align the weights to 16 bytes for SSE2 loads.  Start each set with
        weight 32767 (about 1.0) for the caller's prediction and zero for
        all other inputs.  */
    s->raw = xcalloc((sz) nsel * CM_NI + CM_NI, sizeof(short));
    s->w = s->raw;
    while (((sz) s->w & 15) != 0) s->w++;
    Fk(nsel, s->w[k * CM_NI] = 32767));
  c->mbuf = xcalloc((sz) 1 << CM_MHBITS, 1);
  c->mtab = xcalloc((sz) 1 << CM_MTBITS, sizeof *c->mtab);
  c->mpos = 0;  c->mptr = 0;  c->mlen = 0;  c->mhash = 0;  c->mout = mmul_out();
  c->mp = xmalloc((sz) nst * CM_MLB * sizeof *c->mp);
  c->mpc = xcalloc((sz) nst * CM_MLB, 1);
  rc_probs_init(c->mp, (sz) nst * CM_MLB);
  c->live = 1;
}

void cm_free(cm * c) {
  int i;
  if (!c->live) return;
  Fi(c->nst,
    free(c->st[i].hist);  free(c->st[i].sm);  free(c->st[i].raw));
  free(c->st);  c->st = NULL;
  free(c->mbuf);  free(c->mtab);  free(c->mp);  free(c->mpc);
  c->mbuf = NULL;  c->mtab = NULL;  c->mp = NULL;  c->mpc = NULL;
  c->live = 0;
}

/*  The match model.  */

#define MMASK (((u32) 1 << CM_MHBITS) - 1)

void cm_match_push(cm * c, i32 val) {
  u32 h, p, chk;
  u8 d = (u8) ((val < -127 ? -127 : val > 127 ? 127 : val) + 128);
  /*  Update the rolling hash. Unwritten ring entries are zero.  */
  c->mhash = c->mhash * MMUL + d - c->mout * c->mbuf[(c->mpos - CM_MMIN) & MMASK];
  c->mbuf[c->mpos & MMASK] = d;
  c->mpos++;
  if (c->mlen && c->mbuf[c->mptr & MMASK] == d) {
    c->mptr++;
    if (c->mlen < CM_MMAX) c->mlen++;
  } else c->mlen = 0;
  if (c->mpos < CM_MMIN) return;
  h = c->mhash * 0x9E3779B1u;
  /*  Use the upper bits for a hash check and the lower bits for the
      position in the ring buffer.  */
  chk = h << CM_MTBITS >> CM_MHBITS << CM_MHBITS;
  h >>= 32 - CM_MTBITS;
  if (!c->mlen) {
    p = c->mtab[h];
    if ((p & ~MMASK) == chk && (p & MMASK) != 0) {
      /*  Find the most recent absolute position for this ring-buffer slot,
          then compare preceding digits to measure the match.  */
      p = c->mpos - ((c->mpos - p) & MMASK);
      u32 n = 0;
      while (n < CM_MMAX && n < p &&
             c->mbuf[(p - 1 - n) & MMASK] == c->mbuf[(c->mpos - 1 - n) & MMASK])
        n++;
      if (n >= CM_MMIN) { c->mptr = p;  c->mlen = n; }
    }
  }
  c->mtab[h] = (c->mpos & MMASK) | chk;
}
