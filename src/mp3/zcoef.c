#include "mp3/zhdr.h"

#define NLINES    576
#define BV_MAX    288
#define MAG_MAX   8206

#define SSE_OFFSET     0x2000
#define SSE_INIT_COUNT 50

typedef struct { sse_node * lo;  u32 frac;  i32 code, upd; } sse_at;

static INLINE sse_at sse_get(const ctr * pc, sse_node * s, int intervals) {
  u32 pos = (u32) pc->p * (u32) intervals, frac = pos & 0x7fff;
  sse_node * lo = &s[(i32) pos >> 15];
  i32 p = ((i32) ((u32) lo[1].prob * frac + (0x8000 - frac) * (u32) lo->prob)
           >> 15) - SSE_OFFSET;
  sse_at a;
  a.lo = lo;  a.frac = frac;
  if (p <= 0)           { a.code = 1;  a.upd = 0; }
  else if (p >= 0x8000) { a.code = 0x7fff;  a.upd = 0x8000; }
  else                  a.code = a.upd = p;
  return a;
}

static INLINE void sse_put(sse_at a, int bit, rate_tab sse_rates) {
  sse_node * lo = a.lo, * hi = lo + 1;
  u32 n = lo->count, rate = sse_rates[n];
  lo->count = (u16) MIN(n + 1, (u32) sse_rates[0]);
  i32 target = (i32) ((u32) a.upd * (0x8000u - rate)) >> 15;
  if (bit == 0) target += (i32) rate;
  i32 d = (i32) lo->prob - (i32) hi->prob;
  i32 dlo = (i32) (a.frac * (u32) d + 0x7fff) >> 15;
  lo->prob = (u16) (target + dlo + SSE_OFFSET);
  hi->prob = (u16) (target - (d - dlo) + SSE_OFFSET);
}

static INLINE int sse_code_bit(rcoder * rc, ctr * pc, sse_node * s,
                               int intervals, int bit, rate_tab rates,
                               u32 bias, rate_tab sse_rates) {
  sse_at a = sse_get(pc, s, intervals);
  bit = rc_bit(rc, (u32) a.code, bit);
  sse_put(a, bit, sse_rates);
  counter_update(pc, bit, rates, bias);
  return bit;
}

static INLINE int sse_code_mix(rcoder * rc, ctr * pc, sse_node * s,
                               int intervals, int bit, rate_tab rates,
                               u32 bias, rate_tab sse_rates, ctr * c2,
                               rate_tab rates2, mix2 * x) {
  sse_at a = sse_get(pc, s, intervals);
  bit = mix_code(rc, (u32) a.code, c2, x, bit);
  sse_put(a, bit, sse_rates);
  counter_update(pc, bit, rates, bias);
  counter_update(c2, bit, rates2, ZC_BIAS2);
  return bit;
}

static INLINE int sse_code_mixn(rcoder * rc, ctr * pc, sse_node * s,
                                int intervals, int bit, rate_tab rates,
                                u32 bias, rate_tab sse_rates,
                                ctr * const * c, rate_tab rates2, mixn * x) {
  sse_at a = sse_get(pc, s, intervals);
  int i;
  bit = mixn_code(rc, (u32) a.code, c, ZC_MIXN, x, bit);
  sse_put(a, bit, sse_rates);
  counter_update(pc, bit, rates, bias);
  Fi(ZC_MIXN, counter_update(c[i], bit, rates2, ZC_BIAS2));
  return bit;
}

static void sse_init(sse_node * s, sz count, int intervals) {
  sz i;
  Fi(count,
    s[i].count = SSE_INIT_COUNT;
    s[i].prob = (u16) (SSE_OFFSET +
                       (int) (i % (sz) (intervals + 1)) * 0x8000 / intervals));
}

#define SSE_INIT(arr, intervals)                                               \
  sse_init((sse_node *) (arr), sizeof(arr) / sizeof(sse_node), intervals)

typedef struct {
  ctr o[9][4];
  ctr n[8][3][3];
  ctr h[8][3];
  mixn m[5];
} zc_mix4;

struct coef_sub {
  ctr zero_big_ctr[NLINES / 2][4][7];
  sse_node zero_big_sse[4][5 * 5 * 2 * 3 * 8][9];
  u8 zero_big_hist[2][5][2];

  ctr zero_c1_ctr[NLINES / 2][3];
  sse_node zero_c1_sse[4][4 * 3 * 2 * 2 * 6 * 2][9];
  u8 zero_c1_hist[2][2][2];

  ctr one_ctr[318 * 4 * 3 * 2 * 2];
  sse_node one_sse[4][6 * 5 * 2 * 8 * 2][6];
  u8 one_hist[2][3];

  ctr sign1_ctr[512 * 2 * 2 * 3];
  sse_node sign1_sse[4][6 * 2 * 2 * 4][7];
  u8 sign1_hist[2][2 * 2 * 2 * 2];

  ctr two_ctr[1024 * 6 * 2];
  sse_node two_sse[4][21 * 4 * 7][6];
  u8 two_hist[2][6];

  ctr three_ctr[288 * 6 * 2];
  sse_node three_sse[4][11 * 4 * 5][6];
  u8 three_hist[2][5];

  ctr sign2_ctr[2048 * 2 * 2 * 3];
  sse_node sign2_sse[4][4 * 4 * 5 * 4][8];
  u8 sign2_hist[2][2 * 2 * 2 * 2 * 2];

  ctr zb2[16][4][3][5][3][5];
  ctr zc2[16][4][3][5][3][5];
  ctr on2[16][4][4][5][5];
  ctr tw2[8][8][5][8];
  ctr th2[8][8][5][8];
  mix2 zbm[5], zcm[5], onm[5], twm[5], thm[5];
  zc_mix4 zb4, zc4, on4, tw4, th4;

  ctr sg2[2][2][3][2][3][3][2];
  mix2 sgm[2][2][3][2];

  ctr mg1[2][11][10][10];
  ctr mg2[2][11][10][10][5];
  mix2 mgm[2][11][10];
  ctr mt2[2][15][13][4];
  mix2 mtm[2][15][13];
  ctr mo1[2][11][11];
  mixn mgx[2][11][10];
  ctr mto[2][15][13][5];
  mixn mtx[2][15][13];
  ctr mf1[2][11][10][10][10];
  sse_node mfs[2][11][10][6];
  ctr mt1[2][15][13][4];

  ctr sl_zb[8][8][4][4][3][2];
  ctr sl_zc[4][3][2][2][3][4];
  ctr sl_one[NLINES / 4][8][4][3];
  ctr sl_s1[3][5];
  ctr sl_two[8][8][8][2];
  ctr sl_th[8][8][8][2];
  ctr sl_mag[8][11][10][10][2];
  ctr sl_s2[3][22][4];

  i16 prev_st[NLINES];
  i32 prev_gr[3][NLINES];
  i32 avg_mag[NLINES];
  u32 sign_hist[NLINES];
  u8 two_last[NLINES];
  u8 three_last[NLINES];
};

#define COUNTERS_INIT_ROWS(arr, rows, kq)                                      \
  counters_init((ctr *) (arr), ((((sz) (rows) - 1) >> (kq)) + 1) *             \
                               (sizeof(arr) / sizeof(ctr) / (rows)))

static void submodel_init(coef_sub * s, u32 opt, int kq) {
  if (opt & MP3Z_SLIM) {
    COUNTERS_INIT(s->sl_zb);  COUNTERS_INIT(s->sl_zc);
    COUNTERS_INIT_ROWS(s->sl_one, NLINES / 4, kq);  COUNTERS_INIT(s->sl_s1);
    COUNTERS_INIT(s->sl_two);  COUNTERS_INIT(s->sl_th);
    COUNTERS_INIT(s->sl_mag);  COUNTERS_INIT(s->sl_s2);
    COUNTERS_INIT(s->mt1);
    return;
  }
  COUNTERS_INIT_ROWS(s->zero_big_ctr, NLINES / 2, kq);
  COUNTERS_INIT_ROWS(s->zero_c1_ctr, NLINES / 2, kq);
  COUNTERS_INIT_ROWS(s->one_ctr, 318, kq);
  COUNTERS_INIT(s->sign1_ctr);
  COUNTERS_INIT(s->two_ctr);
  COUNTERS_INIT(s->three_ctr);
  COUNTERS_INIT(s->sign2_ctr);
  if (opt & MP3Z_MIX) {
    COUNTERS_INIT(s->zb2);  MIX_INIT(s->zbm);
    COUNTERS_INIT(s->zc2);  MIX_INIT(s->zcm);
    COUNTERS_INIT(s->on2);  MIX_INIT(s->onm);
    COUNTERS_INIT(s->tw2);  MIX_INIT(s->twm);
    COUNTERS_INIT(s->th2);  MIX_INIT(s->thm);
  }
  if (opt & MP3Z_MIX4) {
    zc_mix4 * m4[5] = {&s->zb4, &s->zc4, &s->on4, &s->tw4, &s->th4};
    int i;
    Fi(5,
      COUNTERS_INIT(m4[i]->o);  COUNTERS_INIT(m4[i]->n);
      COUNTERS_INIT(m4[i]->h);  MIXN_INIT(m4[i]->m));
  }
  if (opt & MP3Z_SMIX) { COUNTERS_INIT(s->sg2);  MIX_INIT(s->sgm); }
  if (opt & MP3Z_MAG) {
    COUNTERS_INIT(s->mg1);  COUNTERS_INIT(s->mg2);  MIX_INIT(s->mgm);
    COUNTERS_INIT(s->mt1);  COUNTERS_INIT(s->mt2);  MIX_INIT(s->mtm);
    COUNTERS_INIT(s->mo1);  MIXN_INIT(s->mgx);
    COUNTERS_INIT(s->mto);  MIXN_INIT(s->mtx);
  }
  if (opt & MP3Z_MAGF) {
    COUNTERS_INIT(s->mt1);  COUNTERS_INIT(s->mf1);  SSE_INIT(s->mfs, 5);
  }
}

static void submodel_sse(coef_sub * s, u32 opt, u32 want) {
  int i;
  if (opt & MP3Z_SLIM) return;
  Fi(4,
    if (want >> i & 1) {
      SSE_INIT(s->zero_big_sse[i], 8);  SSE_INIT(s->zero_c1_sse[i], 8);
      SSE_INIT(s->one_sse[i], 5);  SSE_INIT(s->sign1_sse[i], 6);
      SSE_INIT(s->two_sse[i], 5);  SSE_INIT(s->three_sse[i], 5);
      SSE_INIT(s->sign2_sse[i], 7);
    });
}

typedef struct {
  const i16 * st;
  int cls;
  i32 nz, nz1;
  u32 sum_x, sum_p;
  i16 g1n[NLINES], pn[NLINES];
  u8 ahead[NLINES], dq[NLINES], pnf[NLINES];
  i32 prev[3];
  i32 up_left;
  i32 est;
} line_state;

typedef struct {
  coef_sub * s;
  rcoder * rc;
  line_state * ls;
  i32 k, ch;
  i32 c1table;
  i32 scf;
  i32 sfb;
  i32 g1, g2, g3;
  i32 l1, l2, l3;
  i32 zrun;
  u32 opt;
  int kq;
  i32 o;
  int ms;
  i32 pn;
  i32 pnq, pnf, ahead, ratio, oq, dq, zone;
  i32 sch, ssch;
} line_ctx;

static u8 qlog16_t[512];

static void qlog16_init(void) {
  static const u16 T[14] = {1, 2, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128,
                            256};
  u32 i, q = 0;
  Fi(512,
    while (q < 14 && i >= T[q]) q++;
    qlog16_t[i] = (u8) q);
}

static INLINE i32 qlog16(u32 v) { return v < 512 ? qlog16_t[v] : 15; }

static INLINE i32 sgn3(i32 v) { return v == 0 ? 0 : v > 0 ? 1 : 2; }

static INLINE i32 q8(i32 v) {
  static const u8 T[16] = {0, 1, 2, 3, 4, 4, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6};
  return v < 16 ? T[v] : 7;
}

static INLINE i32 och3(const line_ctx * c) {
  return c->ch == 1 ? 1 + MIN(ABS(c->o), 2) : 0;
}

static INLINE i32 line_zone(i32 k) {
  return (k > 16) + (k > 60) + (k > 100) + (k > 200);
}

static INLINE void mix4_ctrs(const line_ctx * c, zc_mix4 * q, ctr * c2,
                             ctr ** out) {
  out[0] = c2;
  out[1] = &q->o[c->ch == 1 ? 1 + q8(ABS(c->o)) : 0][c->pnf >> 2];
  out[2] = &q->n[q8(c->l1)][MIN(c->l2, 2)][MIN(c->l3, 2)];
  out[3] = &q->h[q8(c->g2)][MIN(c->g3, 2)];
}

static INLINE int sse_code_opt(const line_ctx * c, ctr * pc, sse_node * s,
                               int intervals, int bit, rate_tab rates,
                               u32 bias, rate_tab sse_rates, ctr * c2,
                               zc_mix4 * q, mix2 * m2) {
  if (c->opt & MP3Z_MIX4) {
    ctr * cs[ZC_MIXN];
    mix4_ctrs(c, q, c2, cs);
    return sse_code_mixn(c->rc, pc, s, intervals, bit, rates, bias, sse_rates,
                         cs, rates_w100, &q->m[c->zone]);
  }
  if (c->opt & MP3Z_MIX)
    return sse_code_mix(c->rc, pc, s, intervals, bit, rates, bias, sse_rates,
                        c2, rates_w100, &m2[c->zone]);
  return sse_code_bit(c->rc, pc, s, intervals, bit, rates, bias, sse_rates);
}

static INLINE int code_zero_big(const line_ctx * c, int zero) {
  coef_sub * s = c->s;
  i32 g1q = MIN(c->g1, 3) + (c->g1 > 5);
  i32 l1q = MIN(c->l1, 3) + (c->l1 > 4);
  i32 run = (c->zrun >= 2) + (c->zrun >= 3);
  i32 quiet = c->k > 0 && c->ls->up_left == 0 && c->g1 == 0;
  i32 scfq = MIN(c->scf, 5) + (c->scf > 7);
  u8 * hist = &s->zero_big_hist[c->ch][g1q][c->zrun >= 2];

  i32 ctx = l1q;
  ctx = ctx * 5 + g1q;
  ctx = ctx * 2 + quiet;
  ctx = ctx * 3 + run;
  ctx = ctx * 8 + (*hist & 7);

  if (c->opt & MP3Z_SLIM)
    return code_bit(c->rc, &s->sl_zb[q8(c->g1)][q8(c->l1)][och3(c)]
                                    [MIN(c->g2, 3)][run][c->l2 > 2],
                    zero, rates_w200, 256);
  zero = sse_code_opt(c, &s->zero_big_ctr[(c->k / 2) >> c->kq][MIN(c->g2, 3)]
                                         [scfq],
                      s->zero_big_sse[c->sch][ctx], 8, zero, rates_w100, 0x1555,
                      rates_w400, &s->zb2[c->pnf][c->ahead][MIN(c->l1, 2)]
                                         [c->ratio][c->oq][c->dq],
                      &s->zb4, s->zbm);
  *hist = (u8) (*hist * 2 + zero);
  return zero;
}

static INLINE int code_zero_count1(const line_ctx * c, int zero) {
  coef_sub * s = c->s;
  i32 g1q = MIN(c->g1, 1) + (c->g1 > 5);
  i32 l1q = MIN(c->l1, 2) + (c->l1 > 4);
  i32 quiet = c->k > 1 && c->zrun >= 2;
  u8 * hist = &s->zero_c1_hist[c->ch][c->zrun >= 2][MIN(c->scf, 1)];
  u32 b = *hist;
  i32 zeros = (b & 1) + ((b & 3) == 3) + ((b & 7) == 7) + ((b & 0xf) == 0xf) +
              ((b & 0x1f) == 0x1f);

  i32 ctx = l1q;
  ctx = ctx * 3 + g1q;
  ctx = ctx * 2 + (c->zrun >= 2);
  ctx = ctx * 2 + quiet;
  ctx = ctx * 6 + zeros;
  ctx = ctx * 2 + c->c1table;

  if (c->opt & MP3Z_SLIM)
    return code_bit(c->rc, &s->sl_zc[och3(c)][(c->zrun >= 2) + (c->zrun >= 3)]
                                    [c->k > 0 && c->ls->up_left == 0 &&
                                     c->g1 == 0]
                                    [c->c1table][MIN(c->scf, 2)][MIN(c->g2, 3)],
                    zero, rates_w200, 64);
  zero = sse_code_opt(c, &s->zero_c1_ctr[(c->k / 2) >> c->kq][MIN(c->scf, 2)],
                      s->zero_c1_sse[c->sch][ctx], 8, zero, rates_w100, 0xccc,
                      rates_w200, &s->zc2[c->pnf][c->ahead][MIN(c->l1, 2)]
                                         [c->ratio][c->oq][c->dq],
                      &s->zc4, s->zcm);
  *hist = (u8) (*hist * 2 + zero);
  return zero;
}

static INLINE int code_is_one(const line_ctx * c, int one) {
  coef_sub * s = c->s;
  i32 k = c->k;
  i32 up_big = ABS(c->ls->up_left) > 2;
  u8 * hist = &s->one_hist[c->ch][(c->g1 > 1) + (c->g1 > 2)];

  i32 ctx = MIN(c->l1, 4) + (c->l1 > 6);
  ctx = ctx * 5 + MIN(c->g1, 4);
  ctx = ctx * 2 + up_big;
  ctx = ctx * 8 + (*hist & 7);
  ctx = ctx * 2 + (k > 64);

  i32 ci = (k > 60 ? (k >> 1) + 30 : k) >> c->kq;
  ci = ci * 4 + MIN(c->scf, 3);
  ci = ci * 3 + (c->g2 > 2) + (c->g2 > 4);
  ci = ci * 2 + (c->l2 > 2);
  ci = ci * 2 + (c->l3 > 2);

  if (c->opt & MP3Z_SLIM) {
    one = code_bit(c->rc, &s->sl_one[(k / 4) >> c->kq][*hist & 7][och3(c)]
                                    [(c->g2 > 2) + (c->g2 > 4)],
                   one, rates_w200, 256);
    *hist = (u8) (*hist * 2 + one);
    return one;
  }
  one = sse_code_opt(c, &s->one_ctr[ci], s->one_sse[c->sch][ctx], 5, one,
                     rates_w45, k > 60 ? 0x1555 : 0xccc, rates_w350,
                     &s->on2[c->pnf][MIN(c->l1, 3)][c->ahead][c->ratio][c->dq],
                     &s->on4, s->onm);
  *hist = (u8) (*hist * 2 + one);
  return one;
}

static INLINE int code_sign_of_one(const line_ctx * c, int sign) {
  coef_sub * s = c->s;
  i32 k = c->k, q1 = c->ls->prev[0];
  u32 h = s->sign_hist[k];
  i32 h1 = (i32) (h & 1);
  i32 g1z = c->g1 == 0;

  i32 around = g1z;
  around = around * 2 + h1;
  around = around * 2 + (q1 == 0);
  around = around * 2 + (q1 < 0);
  u8 * hist = &s->sign1_hist[c->ch][around];

  i32 ctx = line_zone(k) + (k >= c->ls->nz1);
  ctx = ctx * 2 + g1z;
  ctx = ctx * 2 + h1;
  ctx = ctx * 4 + (*hist & 3);

  i32 ci = (i32) (h & 0x1ff);
  ci = ci * 2 + (k < c->ls->nz1);
  ci = ci * 2 + c->ch;
  ci = ci * 3 + (c->g2 > 0) + (c->g2 > 1);

  if (c->opt & MP3Z_SLIM)
    return code_bit(c->rc, &s->sl_s1[c->ch == 1 ? sgn3(c->o) : 0][line_zone(k)],
                    sign, rates_w400, 16);
  if (c->opt & MP3Z_SMIX) {
    i32 os = sgn3(c->o);
    sign = sse_code_mix(c->rc, &s->sign1_ctr[ci], s->sign1_sse[c->ssch][ctx], 6,
                        sign, rates_w999, 0x1c71, rates_w175,
                        &s->sg2[c->ch][c->ms][os][ABS(c->o) == 1][sgn3(q1)]
                               [sgn3(s->prev_gr[0][k])][0],
                        rates_w250, &s->sgm[c->ch][c->ms][os][0]);
  } else
    sign = sse_code_bit(c->rc, &s->sign1_ctr[ci], s->sign1_sse[c->ssch][ctx], 6,
                        sign, rates_w999, 0x1c71, rates_w175);
  *hist = (u8) (*hist * 2 + sign);
  return sign;
}

static i32 mag_estimate(const line_ctx * c) {
  return (c->g1 * 12 + c->g2 * 6 + c->s->avg_mag[c->k] * 5) / 53;
}

static i32 mag_bits(i32 est) {
  return MIN((i32) blr_ilog((u32) (MAX((est * 462) >> 8, 2) - 1)) + 1, 9);
}

static INLINE int code_is_two(const line_ctx * c, i32 nbits, int two) {
  coef_sub * s = c->s;
  i32 k = c->k, l1 = c->l1;
  u8 * hist = &s->two_hist[c->ch][MIN(c->g1, 5)];
  i32 onset = l1 > ((i32) ((u32) c->ls->est * 0x234u) >> 8);

  i32 si = c->sfb;
  si = si * 4 + (*hist & 3);
  si = si * 7 + MIN(c->scf, 6);

  i32 ci = (c->g1 > 1) + (c->g1 > 2) + (c->g1 > 4);
  ci = ci * 2 + (c->g2 > 2);
  ci = ci * 2 + (c->g3 > 2);
  ci = ci * 2 + s->two_last[k];
  ci = ci * 2 + onset;
  ci = ci * 4 + (l1 > 1) + (l1 > 2) + (l1 > 3);
  ci = ci * 2 + (c->l2 > 2);
  ci = ci * 2 + (c->l3 > 2);
  ci = ci * 6 + MIN(nbits - 2, 5);
  ci = ci * 2 + c->ch;

  if (c->opt & MP3Z_SLIM) {
    two = code_bit(c->rc,
                   &s->sl_two[MIN(nbits - 2, 7)][*hist & 7][q8(l1)][c->ch],
                   two, rates_w200, 256);
    *hist = (u8) (*hist * 2 + two);
    return two;
  }
  two = sse_code_opt(c, &s->two_ctr[ci], s->two_sse[c->sch][si], 5, two,
                     k > 60 ? rates_w200 : rates_w150, 0xccc, rates_w512,
                     &s->tw2[c->pnq][q8(l1)][c->ratio][MIN(nbits - 2, 7)],
                     &s->tw4, s->twm);
  *hist = (u8) (*hist * 2 + two);
  s->two_last[k] = (u8) two;
  return two;
}

static INLINE int code_is_three(const line_ctx * c, i32 nbits, int three) {
  coef_sub * s = c->s;
  i32 k = c->k, l1 = c->l1;
  u8 * hist = &s->three_hist[c->ch][MIN(c->g1, 4)];
  i32 onset = l1 > ((i32) ((u32) c->ls->est * 0x29au) >> 8);

  i32 si = c->sfb / 2;
  si = si * 4 + (*hist & 3);
  si = si * 5 + MIN(c->scf, 4);

  i32 ci = (c->g1 > 2) + (c->g1 > 4);
  ci = ci * 2 + (c->g2 > 4);
  ci = ci * 2 + s->three_last[k];
  ci = ci * 2 + onset;
  ci = ci * 3 + (l1 > 2) + (l1 > 4);
  ci = ci * 2 + (c->l2 > 2);
  ci = ci * 2 + (c->l3 > 2);
  ci = ci * 6 + MIN(nbits - 2, 5);
  ci = ci * 2 + c->ch;

  if (c->opt & MP3Z_SLIM)
    return code_bit(c->rc,
                    &s->sl_th[MIN(nbits - 2, 7)][q8(l1)][q8(c->g1)][c->ch],
                    three, rates_w200, 256);
  three = sse_code_opt(c, &s->three_ctr[ci], s->three_sse[c->sch][si], 5,
                       three, rates_w200, 0x924, rates_w400,
                       &s->th2[c->pnq][q8(l1)][c->ratio][MIN(nbits - 2, 7)],
                       &s->th4, s->thm);
  *hist = (u8) (*hist * 2 + three);
  s->three_last[k] = (u8) three;
  return three;
}

static INLINE int code_mix2(rcoder * rc, ctr * c1, ctr * c2, mix2 * x, int bit,
                            rate_tab r1, rate_tab r2) {
  bit = mix_code(rc, c1->p, c2, x, bit);
  counter_update(c1, bit, r1, ZC_BIAS2);
  counter_update(c2, bit, r2, ZC_BIAS2);
  return bit;
}

static INLINE int code_mixn(rcoder * rc, ctr * c1, ctr * const * c, int nc,
                            mixn * x, int bit) {
  int i;
  bit = mixn_code(rc, c1->p, c, nc, x, bit);
  counter_update(c1, bit, rates_w200, ZC_BIAS2);
  Fi(nc, counter_update(c[i], bit, rates_w200, ZC_BIAS2));
  return bit;
}

static INLINE i32 bl9(i32 v) { return MIN((i32) blr_ilog((u32) v), 9); }

static INLINE i32 against(i32 v, i32 p, i32 i) {
  i32 q = p >> (i + 1);
  return v < q ? 0 : v > q ? 3 : 1 + ((p >> i) & 1);
}

static INLINE i32 code_mag_zc(const line_ctx * c, i32 nbits, i32 ax) {
  coef_sub * s = c->s;
  rcoder * rc = c->rc;
  i32 ch = c->ch, n = 3, b1 = bl9(c->l1), bp = bl9(c->pn);
  i32 b2 = bl9(MAX(c->l2, c->l3)), v, i;
  i32 ao = ch ? ABS(c->o) : c->g1, bo = 1 + bl9(ch ? ao : c->g2);
  i32 want = (i32) blr_ilog((u32) ax);

  for (; n < 14; n++) {
    i32 st = n - 3;
    if (c->opt & MP3Z_MIX4) {
      ctr * cs[2] = {&s->mg2[ch][st][bp][b2][c->ratio], &s->mo1[ch][st][bo]};
      if (!code_mixn(rc, &s->mg1[ch][st][nbits][b1], cs, 2,
                     &s->mgx[ch][st][bp], want > n))
        break;
    } else if (!code_mix2(rc, &s->mg1[ch][st][nbits][b1],
                          &s->mg2[ch][st][bp][b2][c->ratio],
                          &s->mgm[ch][st][bp], want > n, rates_w200,
                          rates_w200))
      break;
  }
  v = 1;
  for (i = n - 2; i >= 0; i--) {
    i32 bit = (ax >> i) & 1;
    if (c->opt & MP3Z_MIX4) {
      ctr * cs[2] = {&s->mt2[ch][n][i][against(v, c->pn, i)],
                     &s->mto[ch][n][i][1 + against(v, ao, i)]};
      bit = code_mixn(rc, &s->mt1[ch][n][i][against(v, c->l1, i)], cs, 2,
                      &s->mtx[ch][n][i], bit);
    } else
      bit = code_mix2(rc, &s->mt1[ch][n][i][against(v, c->l1, i)],
                      &s->mt2[ch][n][i][against(v, c->pn, i)],
                      &s->mtm[ch][n][i], bit, rates_w200, rates_w200);
    v = v * 2 + bit;
  }
  return MIN(v, MAG_MAX);
}

#define MAGF_MANT 2

static INLINE i32 code_mag_fast(const line_ctx * c, i32 nbits, i32 ax) {
  coef_sub * s = c->s;
  rcoder * rc = c->rc;
  i32 ch = c->ch, n = 3, b1 = bl9(c->l1), bg = bl9(c->g1);
  i32 b2 = bl9(MAX(c->l2, c->l3)), v, i;
  i32 want = (i32) blr_ilog((u32) ax);

  for (; n < 14; n++)
    if (c->opt & MP3Z_SLIM
        ? !code_bit(rc, &s->sl_mag[MIN(nbits - 2, 7)][n - 3][b1][bg][ch],
                    want > n, rates_w400, 256)
        : !sse_code_bit(rc, &s->mf1[ch][n - 3][nbits][b1][bg],
                        s->mfs[ch][n - 3][b2], 5, want > n, rates_w200, 0x800,
                        rates_w400))
      break;
  v = 1;
  for (i = n - 2; i >= 0; i--) {
    i32 bit = (ax >> i) & 1;
    if (i >= n - 1 - MAGF_MANT)
      bit = code_bit(rc, &s->mt1[ch][n][i][against(v, c->l1, i)], bit,
                     rates_w200, ZC_BIAS2);
    else
      bit = rc_bit(rc, 0x4000, bit);
    v = v * 2 + bit;
  }
  return MIN(v, MAG_MAX);
}

static INLINE i32 code_magnitude(const line_ctx * c, i32 nbits, i32 ax) {
  if (code_is_two(c, nbits, ax == 2)) return 2;
  if (code_is_three(c, nbits, ax == 3)) return 3;
  if (c->opt & MP3Z_MAG) return code_mag_zc(c, nbits, ax);
  return code_mag_fast(c, nbits, ax);
}

static INLINE int code_sign_of_big(const line_ctx * c, i32 mag, int sign) {
  coef_sub * s = c->s;
  i32 k = c->k, q1 = c->ls->prev[0], q2 = c->ls->prev[1];
  u32 h = s->sign_hist[k];

  i32 around = c->g1 == 0;
  around = around * 2 + (i32) (h & 1);
  around = around * 2 + (q1 == 0);
  around = around * 2 + (q1 < 0);
  around = around * 2 + (q2 < 0);
  u8 * hist = &s->sign2_hist[c->ch][around];

  i32 ctx = (c->g1 > 0) + (c->g1 > 1) + (c->g1 > 4);
  ctx = ctx * 4 + (i32) (h & 3);
  ctx = ctx * 5 + line_zone(k);
  ctx = ctx * 4 + (*hist & 3);

  i32 ci = (i32) (h & 0x7ff);
  ci = ci * 2 + c->ch;
  ci = ci * 2 + (mag > 16);
  ci = ci * 3 + (c->g2 > 0) + (c->g2 > 2);

  if (c->opt & MP3Z_SLIM)
    return code_bit(c->rc, &s->sl_s2[c->ch == 1 ? sgn3(c->o) : 0][c->sfb][h & 3],
                    sign, rates_w45, 64);
  if (c->opt & MP3Z_SMIX) {
    i32 os = sgn3(c->o);
    sign = sse_code_mix(c->rc, &s->sign2_ctr[ci], s->sign2_sse[c->ssch][ctx], 7,
                        sign, rates_w120, 0x1000, rates_w250,
                        &s->sg2[c->ch][c->ms][os][ABS(c->o) == mag][sgn3(q1)]
                               [sgn3(s->prev_gr[0][k])][1],
                        rates_w250, &s->sgm[c->ch][c->ms][os][1]);
  } else
    sign = sse_code_bit(c->rc, &s->sign2_ctr[ci], s->sign2_sse[c->ssch][ctx], 7,
                        sign, rates_w120, 0x1000, rates_w250);
  *hist = (u8) (*hist * 2 + sign);
  return sign;
}

static INLINE i32 code_value(const line_ctx * c, i32 x) {
  coef_sub * s = c->s;
  i32 k = c->k, ax = ABS(x), mag;
  int sign = x < 0;

  i32 nz = c->ls->nz, nz1 = c->ls->nz1;
  if (k != nz - 1 && k != nz1 - 1) {
    int zero = k < nz1 ? code_zero_big(c, x == 0) : code_zero_count1(c, x == 0);
    if (zero) return 0;
  }

  if (k != nz1 - 1 && (k >= nz1 || code_is_one(c, ax == 1))) {
    mag = 1;
    sign = code_sign_of_one(c, sign);
  } else {
    i32 est = mag_estimate(c);
    mag = code_magnitude(c, mag_bits(est), ax);
    sign = code_sign_of_big(c, mag, sign);
    s->avg_mag[k] = s->avg_mag[k] * 6 / 7 + mag;
    c->ls->est = est;
  }
  return sign ? -mag : mag;
}

static void granule_steps(const mp3_frame * f, int gr, int ch, i16 * st) {
  static const u8 PRETAB[22] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                1, 1, 1, 1, 2, 2, 3, 3, 3, 2, 0};
  const granule * g = &f->gr[gr][ch];
  const u16 * sf = f->scalefac[gr][ch];
  const u16 * bound = long_band_bounds(f->header);
  int mpeg1 = (int) hdr_mpeg1(f->header), mult = 2 * (1 + g->scalefac_scale);
  int is = (hdr_mode_ext(f->header) & HDR_MODE_EXT_IS) && ch == 1;
  int pre = mpeg1 ? g->preflag : g->scalefac_compress >= 500 && !is;
  const u8 * width = mp3_short_width[mp3_short_row[hdr_rate_index(f->header)]];
  i32 k = 0, nlong = 22;
  int i;

  if (g->block_type == BLOCK_SHORT)
    nlong = g->mixed_block ? (mpeg1 ? 8 : 6) : 0;
  for (int b = 0; b < nlong && k < NLINES; b++)
    for (i32 v = g->global_gain -
                 mult * ((b < 21 ? sf[b] : 0) + (pre ? PRETAB[b] : 0));
         k < bound[b + 1] && k < NLINES; k++)
      st[k] = (i16) v;
  for (int b = nlong ? 3 : 0; b < 13 && k < NLINES; b++)
    Fi(3,
      i32 e = mpeg1 ? SCF_SHORT + b * 3 + i
                    : (nlong ? 6 + (b - 3) * 3 + i : b * 3 + i);
      i32 v = g->global_gain - 8 * g->subblock_gain[i] -
              mult * (b < 12 && e < MP3_SCF_COUNT ? sf[e] : 0);
      for (i32 j = 0; j < width[b] && k < NLINES; j++, k++) st[k] = (i16) v);
  for (; k < NLINES; k++) st[k] = (i16) g->global_gain;
}

static const u32 POW16[16] = {65536, 68438, 71468, 74632, 77936, 81386,
                              84990, 88752, 92682, 96785, 101070, 105545,
                              110218, 115098, 120194, 125515};

static u64 scale16(u64 x, i32 e) {
  i32 q = e >> 4;
  u64 v = x * POW16[e & 15];
  return q >= 0 ? ((v << q) + 32768) >> 16
         : q > -40 ? ((v >> -q) + 32768) >> 16 : 0;
}

static u32 pow43[MAG_MAX + 2];

static void pow43_init(void) {
  for (u32 x = 0; x <= MAG_MAX + 1; x++) {
    u64 t = (u64) x * x * x * x * 512, r = 0;
    for (int b = 63; b >= 0; b -= 3) {
      r <<= 1;
      u64 c = 3 * r * (r + 1) + 1;
      if ((t >> b) >= c) { t -= c << b;  r++; }
    }
    pow43[x] = (u32) r;
  }
}

static u32 step_scale[129];
static u16 root43[4096];

static void pred_init(void) {
  u64 i;
  for (i32 d = -64; d <= 64; d++)
    step_scale[d + 64] = (u32) scale16(65536, 3 * d);
  Fi(4096, root43[i] = (u16) blr_isqrt64(blr_isqrt64(128 * i * i * i)));
}

static INLINE u32 root43_of(u64 m) {
  if (m < 4096) return root43[m];
  return (u32) ((blr_isqrt64(m * blr_isqrt64(m)) * 3444 + 512) >> 10);
}

static INLINE i32 scale_g1(i32 a0, i32 d) {
  if (d == 0 || a0 == 0) return a0;
  return (i32) MIN((a0 * (u64) step_scale[CLAMP(d, -64, 64) + 64] + 32768)
                   >> 16, MAG_MAX);
}

static void predict_granule(const coef_sub * s, line_state * ls, int full) {
  const i32 * g = s->prev_gr[0];
  i32 k;
  if (!full) {
    Fk(ls->nz,
      ls->g1n[k] = (i16) scale_g1(ABS(g[k]), s->prev_st[k] - ls->st[k]));
    return;
  }
  Fk(ls->nz,
    i32 d = s->prev_st[k] - ls->st[k], a0 = ABS(g[k]);
    i32 am = k > 0 ? ABS(g[k - 1]) : 0;
    i32 a1 = k + 1 < NLINES ? ABS(g[k + 1]) : 0;
    i32 a2 = k + 2 < NLINES ? ABS(g[k + 2]) : 0;
    u64 sc = step_scale[CLAMP(d, -64, 64) + 64];
    u64 m = ((u64) pow43[am] + 2 * (u64) pow43[a0] + pow43[a1] + 2) >> 2;
    ls->g1n[k] = (i16) scale_g1(a0, d);
    u64 p16 = (root43_of(m) * sc + 32768) >> 16;
    ls->pn[k] = (i16) MIN((p16 + 8) >> 4, MAG_MAX);
    ls->pnf[k] = (u8) qlog16((u32) MIN(p16, 65535));
    ls->ahead[k] = (u8) MIN(MAX(a1, a2), 3);
    ls->dq[k] = (u8) (d == 0 ? 0 : d > 0 ? (d > 4 ? 2 : 1) : (d < -4 ? 4 : 3)));
}

static INLINE void mix_ctx(line_ctx * c, const line_state * ls, i32 k) {
  u32 x = ls->sum_x, p = ls->sum_p;
  c->pn = ls->pn[k];
  c->pnq = q8(c->pn);
  c->pnf = ls->pnf[k];
  c->ahead = ls->ahead[k];
  c->dq = ls->dq[k];
  c->zone = line_zone(k);
  c->ratio = p == 0 ? 2 + 2 * (x != 0)
                    : (2 * x >= p) + (4 * x >= 3 * p) + (3 * x > 4 * p) +
                      (x > 2 * p);
  c->oq = c->ch == 1 ? 1 + (c->o != 0) : 0;
}

static INLINE void push_granule(coef_sub * s, i32 k, i32 x) {
  s->prev_gr[2][k] = s->prev_gr[1][k];
  s->prev_gr[1][k] = s->prev_gr[0][k];
  s->prev_gr[0][k] = x;
}

static void record_line(coef_sub * s, line_state * ls, i32 k, i32 x) {
  ls->prev[2] = ls->prev[1];  ls->prev[1] = ls->prev[0];  ls->prev[0] = x;
  ls->up_left = s->prev_gr[0][k];
  s->sign_hist[k] = s->sign_hist[k] * 2 + ((u32) x >> 31);
  push_granule(s, k, x);
}

static int class_uses_scf(int cls) {
  return cls == 0 || cls == 2 || cls == 3 || cls == 6;
}

static INLINE void code_line(model * m, line_state * ls, int gr, int ch, i32 k,
                             const u32 opt) {
  mp3_frame * f = &m->frame;
  coef_sub * s = m->coef.sub[ls->cls];
  i16 * coef = &f->coef[gr][ch][k];
  i32 sfb = m->coef.sfb_of_line[k];
  const i32 * q = ls->prev;

  line_ctx c = {
      .s = s,
      .rc = &m->rc,
      .ls = ls,
      .k = k,
      .ch = ch,
      .c1table = f->gr[gr][ch].count1table_select,
      .scf = class_uses_scf(ls->cls) ? f->scalefac[gr][ch][sfb] : 0,
      .sfb = sfb,
      .g1 = ls->g1n[k],
      .g2 = ABS(s->prev_gr[1][k]),
      .g3 = ABS(s->prev_gr[2][k]),
      .l1 = ABS(q[0]),
      .l2 = ABS(q[1]),
      .l3 = ABS(q[2]),
      .zrun = q[0] != 0 ? 0 : q[1] != 0 ? 1 : q[2] != 0 ? 2 : 3,
      .opt = opt,
      .kq = m->zc.kshift,
      .o = ch == 1 ? f->coef[gr][0][k] : 0,
      .ms = hdr_mode(f->header) == 1 && (hdr_mode_ext(f->header) & 2),
  };
  c.sch = och3(&c);
  c.ssch = ch ? 1 + sgn3(c.o) : 0;
  if (c.opt & MP3Z_MIX) mix_ctx(&c, ls, k);
  i32 x = code_value(&c, *coef);

  record_line(s, ls, k, x);
  s->prev_st[k] = ls->st[k];
  ls->sum_x += (u32) ABS(x);  ls->sum_p += (u32) c.pn;
  *coef = (i16) x;
}

static INLINE void code_lines(model * m, line_state * ls, int gr, int ch,
                              const u32 opt) {
  coef_sub * s = m->coef.sub[ls->cls];
  i32 k;
  Fk(ls->nz, code_line(m, ls, gr, ch, k, opt));
  for (; k < NLINES; k++) { push_granule(s, k, 0);  s->prev_st[k] = ls->st[k]; }
}

#define ZC_LINE_OPTS (MP3Z_MIX | MP3Z_MAG | MP3Z_SMIX | MP3Z_MAGF | \
                      MP3Z_MIX4 | MP3Z_SLIM)

#define ZC_LINES(name, opt) \
  static NOINLINE void name(model * m, line_state * ls, int gr, int ch) { \
    code_lines(m, ls, gr, ch, (opt) & ZC_LINE_OPTS); \
  }
ZC_LINES(code_lines0, MP3Z_LEVEL0)
ZC_LINES(code_lines4, MP3Z_LEVEL4)
ZC_LINES(code_lines5, MP3Z_LEVEL5)
ZC_LINES(code_lines6, MP3Z_LEVEL6)
ZC_LINES(code_lines7, MP3Z_LEVEL7)

#define NZ_TREE_ROOT 0x400

static i32 code_nz_tree(rcoder * rc, ctr * nodes, sse_node (*sse)[4][6],
                        u8 * hist, i32 val, i32 lo, u32 bias,
                        rate_tab sse_rates, ctr * t2, mix2 * mx) {
  u32 v = 0;
  for (int i = 9; i >= 0; i--) {
    int bit = tree_forced_bit(v, i, lo, NLINES + 1);
    if (bit < 0) {
      u32 node = (v + NZ_TREE_ROOT) >> (i + 1);
      if (t2)
        bit = sse_code_mix(rc, &nodes[node], sse[node][hist[node] & 3], 5,
                           (val >> i) & 1, rates_w20, bias, sse_rates,
                           &t2[node], rates_w40, &mx[i]);
      else
        bit = sse_code_bit(rc, &nodes[node], sse[node][hist[node] & 3], 5,
                           (val >> i) & 1, rates_w20, bias, sse_rates);
      hist[node] = (u8) (hist[node] * 2 + bit);
    }
    v |= (u32) bit << i;
  }
  return (i32) v;
}

static i32 code_big_values(model * m, i32 nz1, i32 nz, i32 bv) {
  coef_model * cm = &m->coef;
  rcoder * rc = &m->rc;
  i32 est = (nz1 + 1) / 2, lim = MIN((nz + 1) / 2, est + 2);

  if (code_bit(rc, &cm->bv_escape, bv < est || bv > lim, rates_w999, 16))
    return (i32) code_tree_lt(rc, cm->bv_tree, 9, (u32) bv, BV_MAX + 1,
                              rates_w250, 32);
  for (int i = 0; i < 2 && est < lim; i++, est++)
    if (code_bit(rc, &cm->bv_is_est[i], bv == est, rates_w250, 32)) break;
  return est;
}

static void code_extent(model * m, line_state * ls, int gr, int ch) {
  coef_model * cm = &m->coef;
  const i16 * coef = m->frame.coef[gr][ch];
  granule * g = &m->frame.gr[gr][ch];
  int cls = ls->cls;
  i32 nz, nz1;

  for (nz = NLINES; nz > 0 && coef[nz - 1] == 0; nz--);
  for (nz1 = nz; nz1 > 0 && ABS(coef[nz1 - 1]) <= 1; nz1--);

  zc_model * z = &m->zc;
  int ext = (z->opt & MP3Z_EXT) != 0;
  if (!z->nz_ready[cls]) {
    COUNTERS_INIT_ROWS(cm->nz1_tree[cls], NZ_CTX, z->nzshift);
    COUNTERS_INIT_ROWS(cm->nz_tree[cls], NZ_CTX, z->nzshift);
    SSE_INIT(cm->nz1_sse[cls], 5);  SSE_INIT(cm->nz_sse[cls], 5);
    z->nz_ready[cls] = 1;
  }
  if (ext) {
    int r1 = z->nz1_prev[ch] / 16, r = z->nz_prev[ch] / 16;
    if (!(z->ext_ready[ch][0] >> r1 & 1)) {
      COUNTERS_INIT(z->nz1_2[ch][r1]);
      z->ext_ready[ch][0] |= (u64) 1 << r1;
    }
    if (!(z->ext_ready[ch][1] >> r & 1)) {
      COUNTERS_INIT(z->nz_2[ch][r]);
      z->ext_ready[ch][1] |= (u64) 1 << r;
    }
  }
  nz1 = code_nz_tree(&m->rc, cm->nz1_tree[cls][(cm->prev_nz1[cls] / 48) >>
                                               m->zc.nzshift],
                     cm->nz1_sse[cls], cm->nz1_hist[cls], nz1, -1, 0x1364,
                     rates_w120, ext ? z->nz1_2[ch][z->nz1_prev[ch] / 16] : NULL,
                     z->nzm[0]);
  nz = code_nz_tree(&m->rc, cm->nz_tree[cls][(cm->prev_nz[cls] / 48) >>
                                             m->zc.nzshift],
                    cm->nz_sse[cls], cm->nz_hist[cls], nz, nz1 - 1, 0x1000,
                    rates_w150, ext ? z->nz_2[ch][z->nz_prev[ch] / 16] : NULL,
                    z->nzm[1]);
  cm->prev_nz[cls] = ls->nz = nz;  cm->prev_nz1[cls] = ls->nz1 = nz1;
  z->nz_prev[ch] = nz;  z->nz1_prev[ch] = nz1;
  g->big_values = code_big_values(m, nz1, nz, g->big_values);
}

static void repeat_extent(model * m, line_state * ls, int gr) {
  coef_model * cm = &m->coef;
  int cls = ls[1].cls;
  m->frame.gr[gr][1].big_values = m->frame.gr[gr][0].big_values;
  ls[1].nz = ls[0].nz;  ls[1].nz1 = ls[0].nz1;
  cm->prev_nz[cls] = m->zc.nz_prev[1] = ls[1].nz;
  cm->prev_nz1[cls] = m->zc.nz1_prev[1] = ls[1].nz1;
}

static void repeat_lines(model * m, line_state * ls, int gr) {
  mp3_frame * f = &m->frame;
  coef_sub * s = m->coef.sub[ls->cls];
  i32 k;
  memcpy(f->coef[gr][1], f->coef[gr][0], sizeof f->coef[gr][0]);
  Fk(NLINES,
    i32 x = f->coef[gr][1][k];
    if (k < ls->nz) {
      if (ABS(x) >= 2) s->avg_mag[k] = s->avg_mag[k] * 6 / 7 + ABS(x);
      record_line(s, ls, k, x);
    } else
      push_granule(s, k, 0);
    s->prev_st[k] = ls->st[k]);
}

#define WS_REGION0       7
#define WS_REGION0_SHORT 5
#define WS_REGION1       22

static int best_table(const i16 * coef, i32 lo, i32 hi) {
  i32 mx = 0, best = 99999, sum[3] = {0, 0, 0};
  int sel = 0, i;

  for (i32 k = lo; k < hi; k++) mx = MAX(mx, ABS(coef[k]));
  if (mx >= 16) mx = (i32) blr_ilog((u32) (mx - 15)) + 15;

  const u8 * c = huff_candidates[mx];
  const u8 * b0 = huff_pair_bits[c[0]], * b1 = huff_pair_bits[c[1]];
  const u8 * b2 = huff_pair_bits[c[2]];
  for (i32 k = lo; k < hi; k += 2) {
    u32 q = huff_pair_index(coef + k);
    sum[0] += b0[q];  sum[1] += b1[q];  sum[2] += b2[q];
  }
  Fi(3,
    int tab = c[i];
    if (tab == 0) break;
    if (tab == 4 || tab == 14) sum[i] = 0;
    if (sum[i] < best) { best = sum[i];  sel = tab; });
  return sel;
}

static void code_region_counts(model * m, granule * g, int ch) {
  coef_model * cm = &m->coef;

  if (g->window_switching != 0) {
    int lsf_short = !hdr_mpeg1(m->frame.header) && g->block_type == BLOCK_SHORT;
    g->region0_count = lsf_short ? WS_REGION0_SHORT : WS_REGION0;
    g->region1_count = WS_REGION1;
  } else if (m->zc.opt & MP3Z_EXT) {
    zc_model * z = &m->zc;
    const u16 * bound = long_band_bounds(m->frame.header);
    i32 b = 0;
    while (b < 22 && (i32) bound[b + 1] <= g->big_values * 2) b++;
    g->region0_count = (i32) tree_mix(&m->rc, cm->region0[ch], z->r0_2[b],
                                      &z->rm[0], 4, (u32) g->region0_count,
                                      16, rates_w20, 16, rates_w20);
    g->region1_count = (i32) tree_mix(&m->rc,
                                      cm->region1[g->region0_count][ch],
                                      z->r1_2[b][g->region0_count], &z->rm[1],
                                      3, (u32) g->region1_count, 8, rates_w28,
                                      16, rates_w20);
  } else {
    g->region0_count = (i32) code_tree_lt(
        &m->rc, cm->region0[ch], 4, (u32) g->region0_count, 16, rates_w20, 16);
    g->region1_count =
        (i32) code_tree_lt(&m->rc, cm->region1[g->region0_count][ch], 3,
                           (u32) g->region1_count, 8, rates_w28, 16);
  }
}

static void code_regions(model * m, int gr, int ch) {
  coef_model * cm = &m->coef;
  granule * g = &m->frame.gr[gr][ch];
  const i16 * coef = m->frame.coef[gr][ch];

  code_region_counts(m, g, ch);

  regions r = granule_regions(m->frame.header, g);
  i32 start[4] = {0, r.end0, r.end1, r.big_end};
  i32 j;

  Fj(3 - g->window_switching,
    int sel = best_table(coef, start[j], start[j + 1]);
    g->table_select[j] =
        (i32) code_tree_lt(&m->rc, cm->table_select[sel][j], 5,
                           (u32) g->table_select[j], 32, rates_w20, 2));
}

static int coef_class(u32 header, int ch, int short_blocks) {
  if (hdr_mode(header) == 1) {
    u32 ext = hdr_mode_ext(header);
    if (ext == 2)
      return short_blocks + (ch == 1 ? 6 : 0);
    if (ext != 0)
      return short_blocks * 2 + ch + 2;
  }
  return short_blocks;
}

void coef_model_code(model * m) {
  mp3_frame * f = &m->frame;
  int ngr = hdr_granules(f->header), nch = hdr_channels(f->header);
  line_state ls[2][2];
  i16 st[2][2][NLINES];
  int i, j;
  int full = (m->zc.opt & (MP3Z_MIX | MP3Z_MAG)) != 0;

  memset(ls, 0, sizeof ls);
  Fi(ngr,
    Fj(nch,
      granule_steps(f, i, j, st[i][j]);
      ls[i][j].st = st[i][j];
      ls[i][j].cls =
          coef_class(f->header, j, f->gr[i][j].block_type == BLOCK_SHORT);
      int cl = ls[i][j].cls;
      u32 want = j ? 0xE : 0x1;
      if (!m->zc.sub_ready[cl]) {
        submodel_init(m->coef.sub[cl], m->zc.opt, m->zc.kshift);
        m->zc.sub_ready[cl] = 1;
      }
      if (want & ~m->zc.sse_ready[cl]) {
        submodel_sse(m->coef.sub[cl], m->zc.opt, want & ~m->zc.sse_ready[cl]);
        m->zc.sse_ready[cl] |= (u8) want;
      }
      if (j == 1 && m->zc.dup[i]) repeat_extent(m, ls[i], i);
      else code_extent(m, &ls[i][j], i, j)));

  Fi(ngr,
    Fj(nch,
      if (j == 1 && m->zc.dup[i]) { repeat_lines(m, &ls[i][1], i);  continue; }
      predict_granule(m->coef.sub[ls[i][j].cls], &ls[i][j], full);
      switch (m->zc.opt & ZC_LINE_OPTS) {
      case MP3Z_LEVEL0 & ZC_LINE_OPTS: code_lines0(m, &ls[i][j], i, j);  break;
      case MP3Z_LEVEL4 & ZC_LINE_OPTS: code_lines4(m, &ls[i][j], i, j);  break;
      case MP3Z_LEVEL5 & ZC_LINE_OPTS: code_lines5(m, &ls[i][j], i, j);  break;
      case MP3Z_LEVEL6 & ZC_LINE_OPTS: code_lines6(m, &ls[i][j], i, j);  break;
      case MP3Z_LEVEL7 & ZC_LINE_OPTS: code_lines7(m, &ls[i][j], i, j);  break;
      default: code_lines(m, &ls[i][j], i, j, m->zc.opt);  break;
      }));

  Fi(ngr,
    Fj(nch,
      granule * g = &f->gr[i][j];
      if (j == 1 && m->zc.dup[i]) {
        g->region0_count = f->gr[i][0].region0_count;
        g->region1_count = f->gr[i][0].region1_count;
        memcpy(g->table_select, f->gr[i][0].table_select,
               sizeof g->table_select);
      } else
        code_regions(m, i, j)));
}

int coef_model_init(model * m) {
  coef_model * cm = &m->coef;
  int i;

  if (!pow43[2]) {
    pow43_init();  acm_prepare();  pred_init();  qlog16_init();
  }
  MIX_INIT(m->zc.nzm);  MIX_INIT(m->zc.rm);

  for (i32 band = 0, line = 0; band < 21; band++)
    for (; line < (band == 20 ? 576 : sfb_bounds[band + 1]); line++)
      cm->sfb_of_line[line] = (u8) band;

  COUNTERS_INIT(cm->region0);
  COUNTERS_INIT(cm->region1);
  COUNTERS_INIT(cm->table_select);
  memset(cm->nz1_hist, 0, sizeof cm->nz1_hist);
  memset(cm->nz_hist, 0, sizeof cm->nz_hist);
  counters_init(&cm->bv_escape, 1);
  COUNTERS_INIT(cm->bv_tree);
  COUNTERS_INIT(cm->bv_is_est);
  memset(cm->prev_nz, 0, sizeof cm->prev_nz);
  memset(cm->prev_nz1, 0, sizeof cm->prev_nz1);

  Fi(COEF_CLASSES, cm->sub[i] = xcalloc(1, sizeof(coef_sub)));
  return 0;
}

void coef_model_free(model * m) {
  int i;
  Fi(COEF_CLASSES, xfree(m->coef.sub[i]);  m->coef.sub[i] = NULL);
}
