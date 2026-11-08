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

static int th_cur[NB], th_curq[NB];
static int th_band = -1, th_k, th_frame = -1;

#define TN_W 16
typedef struct { u16 tag;  i16 v;  u16 qn;  int frame; } tnode;
static tnode tn[NB][8][TN_W];

static void th_reset(void) {
  int i;
  Fi(NB, th_cur[i] = -1;  th_curq[i] = 0);
  th_band = -1;  th_k = 0;  th_frame = -1;
  if (om_feat & OF_THCTX || oz_on) memset(tn, 0, sizeof tn);
}

static int th_scale(int v, int pq, int qn) {
  return v < 0 || pq <= 0 ? -1 : (v * qn + pq / 2) / pq;
}

static tnode * tn_at(int b, int r, int st, int off, int N) {
  u16 tag = (u16) ((off & 255) << 8 | (N & 255)) + 1;
  unsigned h = (unsigned) (off * 37 + N * 11) & (TN_W - 1), j;
  tnode * t = tn[b][r * 2 + st];
  Fj(TN_W,
    tnode * e = t + ((h + j) & (TN_W - 1));
    if (!e->tag) { e->tag = tag;  e->frame = -10; }
    if (e->tag == tag) return e);
  return t + h;
}

static INLINE tnode * th_near(int b, int N, int qn, int * px, int * np) {
  int r = orec_pvqrole & 3, st = orec_thst & 1;
  tnode * me = tn_at(b, r, st, orec_pvqoff, N), * pt = NULL;
  if (!st && (r == 1 || r == 3)) pt = tn_at(b, r - 1, 0, orec_pvqoff, N);
  *px = pt && pt->frame == orec_frame ? th_scale(pt->v, pt->qn, qn) : -1;
  *np = me->frame >= orec_frame - 2 && me->frame < orec_frame
      ? th_scale(me->v, me->qn, qn) : -1;
  return me;
}

static int th_q(int px, int np, int n) {
  return px >= 0 ? 5 + px * 4 / n : np >= 0 ? 1 + np * 4 / n : 0;
}

typedef struct {
  int qn, mode, N, k, pb, px, np;
  tnode * me;
  prior * pr;
} thx;

static void th_open(const oprec * op, int b, thx * t) {
  int i;
  t->qn = MAX((int) op->nsym - 1, 1);  t->mode = (int) op->ftb;
  t->N = (int) op->aux;
  if (orec_cframe != th_frame) {
    th_frame = orec_cframe;  th_band = -1;
    Fi(NB, th_cur[i] = -1);
  }
  if (b != th_band) { th_band = b;  th_k = 0; } else th_k++;
  t->k = MIN(th_k, 3);
  t->pr = prior_theta(t->mode, t->qn);
  t->pb = b > 0 ? th_scale(th_cur[b - 1], th_curq[b - 1], t->qn) : -1;
  t->me = th_near(b, t->N, t->qn, &t->px, &t->np);
}

static mctx * th_ctx(const thx * t, int b) {
  int q = th_q(t->px, t->np, t->qn + 1);
  okey key = (okey) b << 24 | (okey) t->mode << 22 | (okey) bktN(t->N) << 19 |
             (okey) (t->qn & 255) << 11 | (okey) q;
  return ctx_find(key, t->pr);
}

static void th_close(const thx * t, int b, int v) {
  t->me->v = (i16) v;  t->me->qn = (u16) t->qn;  t->me->frame = orec_frame;
  if (th_k == 0) { th_cur[b] = v;  th_curq[b] = t->qn; }
}

static void om_theta(oprec * op, int b) {
  int v = op->v;
  thx t;
  th_open(op, b, &t);
  if (om_side) {
    okey g = (okey) t.mode << 60 | (okey) bktN(t.N) << 56 | (okey) t.k << 54;
    scx x;
    x.n = 5;  x.seed = 1;  x.deep = 1;
    x.key[0] = (okey) 0xC1 << 56 ^ ((okey) b << 24 | (okey) t.mode << 22 |
                                    (okey) bktN(t.N) << 19 | (okey) t.qn << 8);
    x.key[1] = g ^ (okey) 2 << 50;
    x.key[2] = g ^ (okey) 5 << 50 ^ (okey) (t.pb < 0);
    x.key[3] = g ^ (okey) 6 << 50 ^ (okey) (t.px < 0);
    x.key[4] = g ^ (okey) 7 << 50 ^ (okey) (t.np < 0);
    x.pred[0] = x.pred[1] = -1;
    x.pred[2] = t.pb;  x.pred[3] = t.px;  x.pred[4] = t.np;
    sc_sym(ctx_find(x.key[0], t.pr), th_inc, KP[OP_THETA].cap, t.pr, &v, &x,
           t.mode * 4 + (t.qn < 16 ? 0 : t.qn < 64 ? 1 : t.qn < 200 ? 2 : 3));
  } else ctx_code(th_ctx(&t, b), th_inc, KP[OP_THETA].cap, &v);
  op->v = v;
  PROF(prof_theta(b, t.mode, t.k, (u32) t.qn, op->aux, (i32) v));
  th_close(&t, b, v);
}

static int fr_qi[NB][2], fr_fv[NB][2], fr_fn[NB][2], fr_frame = -1;

static u8 fr_tf[NB], fr_dyn[NB], pf_tf[NB], pf_dyn[NB];

static void fr_reset(void) {
  fr_frame = -1;
  memset(fr_qi, 0, sizeof fr_qi);  memset(fr_fv, 0, sizeof fr_fv);
  memset(fr_fn, 0, sizeof fr_fn);
  memset(fr_tf, 0, sizeof fr_tf);  memset(fr_dyn, 0, sizeof fr_dyn);
  memset(pf_tf, 0, sizeof pf_tf);  memset(pf_dyn, 0, sizeof pf_dyn);
}

static void fr_sync(void) {
  if (orec_cframe != fr_frame) {
    fr_frame = orec_cframe;
    memset(fr_qi, 0, sizeof fr_qi);  memset(fr_fn, 0, sizeof fr_fn);
    memset(band_qi, 0, sizeof band_qi);
    memcpy(pf_tf, fr_tf, sizeof fr_tf);  memcpy(pf_dyn, fr_dyn, sizeof fr_dyn);
    memset(fr_tf, 0, sizeof fr_tf);  memset(fr_dyn, 0, sizeof fr_dyn);
  }
}

static i32 en_E[2][NB], en_P[2][NB], en_acc[2];
static int en_frame = -1;
static u8 dn_est[NB];
static int dn_frame = -1, dn_band, dn_k, dn_short;

static void en_reset(void) {
  memset(en_E, 0, sizeof en_E);  memset(en_P, 0, sizeof en_P);
  en_acc[0] = en_acc[1] = 0;  en_frame = -1;  dn_frame = -1;
}

static i32 en_pred(int b, int c) {
  static const i32 pc[4] = { 29440, 26112, 21248, 16384 };
  i32 old = MAX(-9 * 256, en_E[c][b]);
  if (orec_cframe != en_frame) {
    en_frame = orec_cframe;  en_acc[0] = en_acc[1] = 0;
  }
  return (orec_intra & 1 ? 0
                         : (i32) ((opus_int64) pc[orec_LM & 3] * old / 32768)) +
         en_acc[c];
}

#define EN_LIM (1L << 22)

static void en_coarse(int b, int c, int qi) {
  static const i32 bc[4] = { 30147, 22282, 12124, 6554 };
  i32 q = (i32) qi * 256, beta = orec_intra & 1 ? 4915 : bc[orec_LM & 3];
  i32 p = en_pred(b, c), e;
  en_P[c][b] = en_E[c][b];
  e = p + q;
  en_E[c][b] = CLAMP(e, -EN_LIM, EN_LIM);
  en_acc[c] += q - (i32) ((opus_int64) beta * q / 32768);
}

static int fine_xf(int b, int c, int nb, int m) {
  return c && fr_fn[b][0] == nb ? 1 + (fr_fv[b][0] * m >> nb) : 0;
}

static int fine_fh(int b, int c, int nb) {
  i32 d = en_P[c][b] - en_E[c][b] + 128;
  return d >= 0 && d < 256 ? (int) (d << nb >> 8) : -1;
}

static int fine_fq(int b, int c, int nb) {
  int fh = fine_fh(b, c, nb);
  return fh < 0 ? 0 : 1 + (fh * 4 >> nb);
}

static void fine_upd(int b, int c, int nb, int v) {
  en_E[c][b] += ((2 * (i32) v + 1) << 7 >> nb) - 128;
  fr_fv[b][c] = v;  fr_fn[b][c] = nb;
}

static mctx * fine_ctx(int b, int c, int nb) {
  int xf = fine_xf(b, c, nb, 5), fq = fine_fq(b, c, nb);
  prior * pr = prior_uniform(1 << nb, KP[OP_BITS].scale);
  okey key = (okey) 0x70000000UL << 32 | (okey) b << 16 | (okey) c << 15 |
             (okey) nb << 11 | (okey) fq | (okey) xf << 24;
  return ctx_find(key, pr);
}

static void fine_note(int b, int c, int nb, int v) {
  fine_upd(b, c, nb, v);
  PROF(prof_fine(b, c, nb, prev_fine[b][c], (i32) v));
  prev_fine[b][c] = v;
}

static void om_fine(oprec * op, int b, int c) {
  int nb = (int) op->ftb, v = op->v;
  if (om_side) {
    int q = CLAMP(fr_qi[b][c], -3, 3) + 3;
    int xf = fine_xf(b, c, nb, 5), fh = fine_fh(b, c, nb);
    i32 d = en_P[c][b] - en_E[c][b] + 128;
    prior * pr = prior_uniform(1 << nb, KP[OP_BITS].scale);
    int xq = c ? CLAMP(fr_qi[b][0], -3, 3) + 4 : 0;
    int db = d < -128 ? 0 : d >= 384 ? 9 : 1 + (int) ((d + 128) >> 6);
    okey g = (okey) nb << 60 | (okey) q << 56;
    scx x;
    x.n = 5;  x.seed = 1;  x.deep = 2;
    x.key[0] = (okey) 0x71 << 56 | (okey) b << 16 | (okey) c << 15 |
               (okey) nb << 11 | (okey) q << 8;
    x.key[1] = g ^ (okey) 2 << 52 ^ (okey) xq << 40 ^ (okey) xf << 32;
    x.key[2] = g ^ (okey) 3 << 52 ^ (okey) (prev_fine[b][c] & 0xFF) << 40 ^
               (okey) b << 32;
    x.key[3] = g ^ (okey) 4 << 52 ^ (okey) (fh < 0);
    x.key[4] = g ^ (okey) 5 << 52 ^ (okey) db << 40;
    x.pred[0] = x.pred[1] = x.pred[2] = x.pred[4] = -1;  x.pred[3] = fh;
    sc_sym(ctx_find(x.key[0], pr), KP[OP_BITS].inc, KP[OP_BITS].cap, pr, &v,
           &x, 12 + nb);
  } else ctx_code(fine_ctx(b, c, nb), KP[OP_BITS].inc, KP[OP_BITS].cap, &v);
  op->v = v;
  fine_note(b, c, nb, v);
}

static mctx * final_ctx(int b, int c) {
  int nb = fr_fn[b][c], fv = nb ? fr_fv[b][c] & 7 : 0;
  prior * pr = prior_uniform(2, KP[OP_BITS].scale);
  return ctx_find(((okey) 0x72 << 56 | (okey) nb << 8 | (okey) fv) ^
                  (okey) b << 16, pr);
}

static void final_note(int b, int c, int v) {
  en_E[c][b] += (2 * (i32) v - 1) * 64 / (1 << fr_fn[b][c]);
}

static void om_final(oprec * op, int b, int c) {
  int nb = fr_fn[b][c], fv = nb ? fr_fv[b][c] & 7 : 0, v = op->v;
  if (om_side) {
    prior * pr = prior_uniform(2, KP[OP_BITS].scale);
    okey g = (okey) 0x72 << 56 | (okey) nb << 48 | (okey) fv << 40;
    scx x;
    x.n = 2;  x.seed = 1;  x.deep = 1;
    x.key[0] = (okey) 0x72 << 56 | (okey) nb << 8 | (okey) fv;
    x.key[1] = g ^ (okey) b << 8 ^ (okey) c << 16;
    x.pred[0] = x.pred[1] = -1;
    sc_sym(ctx_find(x.key[0], pr), KP[OP_BITS].inc, KP[OP_BITS].cap, pr, &v,
           &x, 21);
  } else ctx_code(final_ctx(b, c), KP[OP_BITS].inc, KP[OP_BITS].cap, &v);
  op->v = v;
  final_note(b, c, v);
}

static void en_bits(int site, int b, int c, int nb, int v) {
  if (site == OREC_S_CELT_FINE) fine_upd(b, c, nb, v);
  else if (site == OREC_S_CELT_FINALISE) final_note(b, c, v);
}

#define OM_TRACK (om_feat & (OF_COARSE | OF_FINE) || OM_DYN)
