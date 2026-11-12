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

#include "mp3/mp3.h"
#include "fmt/arc.h"
#include "coder/rc.h"
#include "mp3/z.h"

#define MPA_HDR     ARC_MP3HDR
#define MPA_MAX     (MPA_HDR + ARC_SUMLEN)
enum { MPE_STORE, MPE_Z = 3, MPE_WALK, MPE_SYM };

#define MP_WIN  ((sz) 1 << 21)
#define MP_LOOK ((sz) 1 << 17)
sz mp3_window = MP_WIN;

#define MP_MINRUN  6
#define MP_MAXFREE 8192
#define MP_PAD     8
#define MP_LIMIT   255

typedef uint64_t u64;
typedef int64_t i64;

typedef struct {
  int lsf, m25, crc, bri, sri, pad, mode, mext, nch, sbytes, hbytes, fbytes;
} mh;

static int mh_parse(u32 h, u32 freeb, mh * f) {
  int ver = (int) (h >> 19 & 3), rate;
  if ((h >> 21) != 0x7FF || ver == 1 || (h >> 17 & 3) != 1 || (h & 3) == 2)
    return 0;
  f->bri = (int) (h >> 12 & 15);  f->sri = (int) (h >> 10 & 3);
  if (f->bri == 15 || f->sri == 3) return 0;
  f->lsf = ver != 3;  f->m25 = ver == 0;  f->crc = !(h >> 16 & 1);
  f->pad = (int) (h >> 9 & 1);  f->mode = (int) (h >> 6 & 3);
  f->mext = (int) (h >> 4 & 3);
  f->nch = f->mode == 3 ? 1 : 2;
  rate = mp3_rate_hz[!f->lsf][!f->m25 << 2 | f->sri];
  if (!f->bri) { if (!freeb) return 0;  f->fbytes = (int) freeb + f->pad; }
  else f->fbytes = (f->lsf ? 72 : 144) * mp3_bitrate[!f->lsf * 15 + f->bri] /
                   rate + f->pad;
  f->sbytes = mp3_side_len[(f->nch == 2) * 2 + !f->lsf];
  f->hbytes = 4 + 2 * f->crc + f->sbytes;
  return f->fbytes >= f->hbytes;
}

static int mh_rate(const mh * f) {
  return f->m25 ? 6 + f->sri : f->lsf ? 3 + f->sri : f->sri;
}

static u32 getbits(const u8 * p, int * pos, int n) {
  u32 v = 0;
  while (n-- > 0) {
    v = v << 1 | (u32) (p[*pos >> 3] >> (7 - (*pos & 7)) & 1);
    (*pos)++;
  }
  return v;
}

static void side_sum(const mh * f, const u8 * side, int * mdb, int * bits) {
  int pos = 0, i, j;
  *mdb = (int) getbits(side, &pos, f->lsf ? 8 : 9);
  pos += f->lsf ? f->nch : f->nch == 1 ? 5 : 3;
  if (!f->lsf) pos += 4 * f->nch;
  *bits = 0;
  Fi(f->lsf ? 1 : 2,
    Fj(f->nch,
      *bits += (int) getbits(side, &pos, 12);
      pos += 9 + 8 + (f->lsf ? 9 : 4) + 1 + 22 + (f->lsf ? 2 : 3)));
}

typedef struct { i64 pay, end; } mlay;

static int lay_place(mlay * l, const mh * f, int mdb, int bits, i64 * s,
                     i64 * n) {
  i64 room = f->fbytes - f->hbytes;
  int fits;
  *s = l->pay - mdb;  *n = (bits + 7) >> 3;
  fits = *s >= l->end && *s + *n <= l->pay + room;
  if (!fits) { *s = l->end;  *n = 0; }
  l->end = *s + *n;  l->pay += room;
  return fits;
}

typedef struct { sz at, len;  u32 run, nfr, freeb; } mseg;
enum { SEG_RAW, SEG_RUN, SEG_STORED };

static u32 free_slot(const u8 * b, sz n, sz at, const mh * f) {
  u32 h = rd32be(b + at), d;
  mh g;
  for (d = (u32) f->hbytes; d < MP_MAXFREE && at + d + 4 <= n; d++) {
    u32 x = rd32be(b + at + d);
    if ((x & 0xFFFEFC00UL) == (h & 0xFFFEFC00UL) &&
        mh_parse(x, MP_MAXFREE, &g))
      return d - (u32) f->pad;
  }
  return 0;
}

static int tags_only(const u8 * b, sz n, sz at) {
  while (n > at) {
    if (n - at >= 128 && !memcmp(b + n - 128, "TAG", 3)) n -= 128;
    else if (n - at >= 15 && !memcmp(b + n - 9, "LYRICS200", 9)) {
      sz k = 0, i;
      for (i = n - 15; i < n - 9; i++) {
        if (b[i] < '0' || b[i] > '9') return 0;
        k = k * 10 + (sz) (b[i] - '0');
      }
      if (k + 15 > n - at) return 0;
      n -= k + 15;
    } else if (n - at >= 32 && !memcmp(b + n - 32, "APETAGEX", 8)) {
      u32 len = rd32le(b + n - 20);
      sz k = (sz) len + (b[n - 9] & 0x80 ? 32 : 0);
      if (len < 32 || k > n - at) return 0;
      n -= k;
    } else return 0;
  }
  return 1;
}

static int scan_run(const u8 * b, sz n, sz at, mseg * r, int whole) {
  sz off = at;
  u32 nfr = 0, placed = 0, freeb = 0;
  mh f, first;
  mlay lay = { 0, 0 };
  r->len = 0;  r->nfr = 0;
  if (at + 4 > n) return 0;
  if (!mh_parse(rd32be(b + at), 0, &f)) {
    if (!mh_parse(rd32be(b + at), MP_MAXFREE, &f) || f.bri) return 0;
    if (!(freeb = free_slot(b, n, at, &f))) return 0;
  }
  memset(&first, 0, sizeof first);
  while (off + 4 <= n && mh_parse(rd32be(b + off), freeb, &f)) {
    int mdb, bits;
    i64 s, l;
    if (nfr && (f.lsf != first.lsf || f.m25 != first.m25 ||
                f.sri != first.sri || f.nch != first.nch))
      break;
    if ((sz) f.fbytes > n - off) break;
    side_sum(&f, b + off + 4 + 2 * f.crc, &mdb, &bits);
    placed += (u32) lay_place(&lay, &f, mdb, bits, &s, &l);
    if (!nfr) first = f;
    off += (sz) f.fbytes;  nfr++;
  }
  r->at = at;  r->len = off - at;  r->run = 1;
  r->nfr = nfr;  r->freeb = freeb;
  return placed && (nfr >= MP_MINRUN ||
                    (whole && nfr >= 2 && tags_only(b, n, off)));
}

static void run_cut(const u8 * b, mseg * r, sz lim) {
  sz off = r->at, cut = 0;
  u32 i, ci = 0;
  int least = 1 << 30, mdb, bits;
  mh f;
  Fi(r->nfr,
    mh_parse(rd32be(b + off), r->freeb, &f);
    if (off >= lim && i >= MP_MINRUN) {
      side_sum(&f, b + off + 4 + 2 * f.crc, &mdb, &bits);
      if (mdb < least) { least = mdb;  ci = i;  cut = off; }
      if (!mdb) break;
    }
    off += (sz) f.fbytes);
  if (ci) { r->nfr = ci;  r->len = cut - r->at; }
}

static void run_fwd(const u8 * r, const mseg * sg, u8 * t) {
  sz off = 0, mn = 0, o = 0;
  u32 i;
  u8 * m = xmalloc(sg->len ? sg->len : 1);
  mlay lay = { 0, 0 };
  mh f;
  Fi(sg->nfr,
    mh_parse(rd32be(r + off), sg->freeb, &f);
    memcpy(m + mn, r + off + f.hbytes, (sz) (f.fbytes - f.hbytes));
    mn += (sz) (f.fbytes - f.hbytes);  off += (sz) f.fbytes);
  for (i = 0, off = 0; i < sg->nfr; i++) {
    int mdb, bits;
    i64 s, l, prev = lay.end;
    mh_parse(rd32be(r + off), sg->freeb, &f);
    side_sum(&f, r + off + 4 + 2 * f.crc, &mdb, &bits);
    lay_place(&lay, &f, mdb, bits, &s, &l);
    memcpy(t + o, r + off, 4);
    memcpy(t + o + 4, r + off + 4 + 2 * f.crc, (sz) f.sbytes);
    if (f.crc) memcpy(t + o + 4 + f.sbytes, r + off + 4, 2);
    o += (sz) f.hbytes;
    memcpy(t + o, m + prev, (sz) (s + l - prev));
    o += (sz) (s + l - prev);  off += (sz) f.fbytes;
  }
  memcpy(t + o, m + lay.end, mn - (sz) lay.end);
  xfree(m);
}

static int run_inv(const u8 * t, const mseg * sg, u8 * r) {
  sz pos = 0, mn = 0, mp = 0, off = 0, len = sg->len;
  sz * heads = xmalloc(sizeof *heads * (sg->nfr ? sg->nfr : 1));
  u8 * m = xmalloc(len ? len : 1);
  u32 i;
  int ok = 1;
  mlay lay = { 0, 0 };
  mh f;
  for (i = 0; i < sg->nfr && ok; i++) {
    int mdb, bits;
    i64 s, l, prev = lay.end;
    sz take;
    if (pos + 4 > len || !mh_parse(rd32be(t + pos), sg->freeb, &f) ||
        pos + (sz) f.hbytes > len) {
      ok = 0;  break;
    }
    side_sum(&f, t + pos + 4, &mdb, &bits);
    lay_place(&lay, &f, mdb, bits, &s, &l);
    heads[i] = pos;  pos += (sz) f.hbytes;
    take = (sz) (s + l - prev);
    if (take > len - pos) { ok = 0;  break; }
    memcpy(m + mn, t + pos, take);  mn += take;  pos += take;
  }
  if (ok) { memcpy(m + mn, t + pos, len - pos);  mn += len - pos; }
  for (i = 0; i < sg->nfr && ok; i++) {
    sz h = heads[i], room;
    mh_parse(rd32be(t + h), sg->freeb, &f);
    room = (sz) (f.fbytes - f.hbytes);
    if (mp + room > mn || off + (sz) f.fbytes > len) { ok = 0;  break; }
    memcpy(r + off, t + h, 4);
    if (f.crc) memcpy(r + off + 4, t + h + 4 + f.sbytes, 2);
    memcpy(r + off + 4 + 2 * f.crc, t + h + 4, (sz) f.sbytes);
    memcpy(r + off + f.hbytes, m + mp, room);
    mp += room;  off += (sz) f.fbytes;
  }
  ok = ok && mp == mn && off == len;
  xfree(m);  xfree(heads);
  return ok;
}

#define TNODES 512
static short TREE[MP3_NTREE + 1][2 * TNODES];
static int TREEN[MP3_NTREE + 1];
static int TREE_OF[32];
#define QTREE MP3_NTREE

static void tree_add(int t, u32 code, int len, int sym) {
  int cur = 0, i;
  for (i = len - 1; i >= 0; i--) {
    int b = (int) (code >> i & 1);
    short * nx = &TREE[t][2 * cur + b];
    if (!i) { *nx = (short) -(sym + 1);  break; }
    if (*nx <= 0) {
      *nx = (short) TREEN[t]++;
      FATAL_UNLESS(TREEN[t] < TNODES, "internal: Huffman tree too large");
    }
    cur = *nx;
  }
}

static u8 LOGM[256];

static int ilog8(u64 x) {
  int e;
  if (!x) return 0;
  e = (int) blr_ilog64(x) - 1;
  return e * 256 + LOGM[(e >= 8 ? x >> (e - 8) : x << (8 - e)) & 255];
}

static void tcode_init(void);
static void tfast_init(void);

static void mp_prepare(int code) {
  static int done, cdone;
  int t, i, j;
  if (!cdone) {
    cdone = 1;
    Fi(16, TREE_OF[i] = mp3_pairs[i].code ? i : -1);
    for (t = 16; t < 24; t++) TREE_OF[t] = 16;
    for (t = 24; t < 32; t++) TREE_OF[t] = 17;
    tcode_init();
  }
  if (done || code) return;
  done = 1;
  Fj(MP3_NTREE,
    const mp3_huff * h = &mp3_pairs[j];
    TREEN[j] = 1;
    if (!h->code) continue;
    Fi(h->dim * h->dim,
      tree_add(j, h->code[i], h->len[i], (i / h->dim) << 4 | i % h->dim)));
  TREEN[QTREE] = 1;
  Fi(16, tree_add(QTREE, mp3_quad.code[i], mp3_quad.len[i], i));
  tfast_init();
  Fi(256,
    u64 m = 256 + (u64) i, x = m << 16, r = 0;
    Fj(8,
      x = x * x >> 24;
      r <<= 1;
      if (x >= (u64) 2 << 24) { x >>= 1;  r |= 1; });
    LOGM[i] = (u8) r);
}

enum { S_RAW, S_HEAD, S_SIDE, S_CRC, S_GAP, S_MAIN, S_TAIL, S_N };
enum { F_MDB, F_PRIV, F_SCFSI, F_P23, F_BV, F_GG, F_SC, F_WS, F_BT, F_MIX,
       F_TS, F_SBG, F_R0, F_R1, F_PRE, F_SCS, F_C1T, F_N };
enum { M_SCF, M_HCODE, M_LINX, M_SIGNX, M_LINY, M_SIGNY, M_C1CODE, M_C1SIGN,
       M_EXTRA, M_PAD };

typedef struct { u8 id, gr, ch, sub, bits; } sfield;
typedef struct { u8 entry, bits; } scfield;

typedef struct {
  int state;
  i64 left;
  u32 acc;  int nb;
  u32 last4;
  u32 nfr, fidx, freeb;
  u8 head[4], ph[4];
  int hn;
  i64 mainbytes;
  mh hdr;
  mlay lay;
  u8 side[32];  int sn;
  u32 crc;  int cbit;
  sfield fl[80];  int nf, fi, fbit;  u32 fval;
  short val[2][2][F_N][4], pval[2][2][F_N][4];
  int ms, ngc, gc, gr, ch;
  i64 gleft;
  scfield sf[48];  int nsf, sfi, sfbit, sfval;
  int k, tbl, lin, node, depth, x, yv, linleft, linval, c1tbl, c1q, c1idx;
  const short * tree;
  int bigend, r1, r2;
  i16 curb[576 + 2 * MP_PAD], lastb[2][576 + 2 * MP_PAD];
  u8 scf[2][40], lscf[2][40];
  int nent;  u8 ewidth[48];
} mw;

#define CUR(w)   ((w)->curb + MP_PAD)
#define LAST(w, c) ((w)->lastb[c] + MP_PAD)

static const u8 LONGW[6][22] = {
  { 4, 4, 4, 4, 4, 4, 6, 6, 8, 8, 10, 12, 16, 20, 24, 28, 34, 42, 50, 54, 76, 158 },
  { 4, 4, 4, 4, 4, 4, 6, 6, 6, 8, 10, 12, 16, 18, 22, 28, 34, 40, 46, 54, 54, 192 },
  { 4, 4, 4, 4, 4, 4, 6, 6, 8, 10, 12, 16, 20, 24, 30, 38, 46, 56, 68, 84, 102, 26 },
  { 6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 38, 46, 52, 60, 68, 58, 54 },
  { 6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 18, 22, 26, 32, 38, 46, 54, 62, 70, 76, 36 },
  { 12, 12, 12, 12, 12, 12, 16, 20, 24, 28, 32, 40, 48, 56, 64, 76, 90, 2, 2, 2, 2, 2 }
};
static const u8 LONGR[9] = { 0, 1, 2, 3, 4, 3, 3, 3, 5 };

static void w_enter(mw * w, int s, i64 bits);

static void push(mw * w, int id, int g, int c, int sub, int bits) {
  sfield * f = &w->fl[w->nf++];
  f->id = (u8) id;  f->gr = (u8) g;  f->ch = (u8) c;
  f->sub = (u8) sub;  f->bits = (u8) bits;
}

static void start_side(mw * w) {
  int nch = w->hdr.nch, i, j, lsf = w->hdr.lsf;
  w->nf = 0;
  push(w, F_MDB, 0, 0, 0, lsf ? 8 : 9);
  push(w, F_PRIV, 0, 0, 0, lsf ? nch : nch == 1 ? 5 : 3);
  if (!lsf) Fi(nch, Fj(4, push(w, F_SCFSI, 0, i, j, 1)));
  Fi(lsf ? 1 : 2,
    Fj(nch,
      push(w, F_P23, i, j, 0, 12);  push(w, F_BV, i, j, 0, 9);
      push(w, F_GG, i, j, 0, 8);  push(w, F_SC, i, j, 0, lsf ? 9 : 4);
      push(w, F_WS, i, j, 0, 1);
      if (!lsf) push(w, F_PRE, i, j, 0, 1);
      push(w, F_SCS, i, j, 0, 1);  push(w, F_C1T, i, j, 0, 1)));
  memset(w->val, 0, sizeof w->val);
  w->fi = 0;  w->fbit = 0;  w->fval = 0;  w->sn = 0;
}

static void side_bit(mw * w, int bit) {
  sfield f;
  if (w->fi >= w->nf) return;
  w->fval = w->fval << 1 | (u32) bit;
  if (++w->fbit < w->fl[w->fi].bits) return;
  f = w->fl[w->fi];
  w->val[f.gr][f.ch][f.id][f.sub] = (short) w->fval;
  if (f.id == F_WS) {
    sfield br[8];
    int n = 0, i;
#define BR(i_, s_, b_) (br[n].id = (u8) (i_), br[n].gr = f.gr,          \
                        br[n].ch = f.ch, br[n].sub = (u8) (s_),         \
                        br[n].bits = (u8) (b_), n++)
    if (w->fval) {
      BR(F_BT, 0, 2);  BR(F_MIX, 0, 1);  BR(F_TS, 0, 5);  BR(F_TS, 1, 5);
      BR(F_SBG, 0, 3);  BR(F_SBG, 1, 3);  BR(F_SBG, 2, 3);
    } else {
      BR(F_TS, 0, 5);  BR(F_TS, 1, 5);  BR(F_TS, 2, 5);
      BR(F_R0, 0, 4);  BR(F_R1, 0, 3);
    }
#undef BR
    memmove(w->fl + w->fi + 1 + n, w->fl + w->fi + 1,
            (sz) (w->nf - w->fi - 1) * sizeof *w->fl);
    Fi(n, w->fl[w->fi + 1 + i] = br[i]);
    w->nf += n;
  }
  w->fi++;  w->fbit = 0;  w->fval = 0;
}

#define V(w, f) ((w)->val[(w)->gr][(w)->ch][f][0])

static void build_entries(mw * w) {
  int ri = mh_rate(&w->hdr), ws = V(w, F_WS) != 0, sh = ws && V(w, F_BT) == 2;
  int mixed = sh && V(w, F_MIX), j, i;
  w->nent = 0;
  if (!sh) for (i = 0; i < 22; i++) w->ewidth[w->nent++] = LONGW[LONGR[ri]][i];
  else {
    int first = 0;
    if (mixed) {
      int nl = w->hdr.lsf ? 6 : 8;
      Fi(nl, w->ewidth[w->nent++] = LONGW[LONGR[ri]][i]);
      first = 3;
    }
    for (i = first; i < 13; i++)
      Fj(3, w->ewidth[w->nent++] = mp3_short_width[mp3_short_row[ri]][i]);
  }
  w->bigend = MIN(V(w, F_BV) * 2, 576);
#define SUMF(n_, out_) do { int s_ = 0, q_;                                   \
                            for (q_ = 0; q_ < (n_) && q_ < w->nent; q_++)     \
                              s_ += w->ewidth[q_];                            \
                            (out_) = MIN(s_, 576); } while (0)
  if (ws) { SUMF(sh && !mixed ? 9 : 8, w->r1);  w->r2 = 576; }
  else {
    SUMF(V(w, F_R0) + 1, w->r1);  SUMF(V(w, F_R0) + V(w, F_R1) + 2, w->r2);
  }
#undef SUMF
}

static void build_scf(mw * w) {
  int sc = V(w, F_SC), sh = V(w, F_WS) && V(w, F_BT) == 2;
  int mixed = sh && V(w, F_MIX), ch = w->ch, i, j, e = 0;
  w->nsf = 0;
  memset(w->scf[ch], 0, sizeof w->scf[ch]);
#define SF(en_, b_) (w->sf[w->nsf].entry = (u8) (en_),                  \
                     w->sf[w->nsf].bits = (u8) (b_), w->nsf++)
  if (!w->hdr.lsf) {
    int s1 = mp3_slen1[sc], s2 = mp3_slen2[sc];
    if (sh) {
      if (mixed) { Fi(8, SF(e++, s1));
                   for (i = 3; i < 12; i++) Fj(3, SF(e++, i < 6 ? s1 : s2)); }
      else Fi(12, Fj(3, SF(e++, i < 6 ? s1 : s2)));
    } else {
      int b;
      Fj(4,
        int copy = w->gr == 1 && w->val[0][ch][F_SCFSI][j];
        for (b = mp3_scfsi_band[j]; b < mp3_scfsi_band[j + 1]; b++) {
          if (copy) w->scf[ch][b] = w->lscf[ch][b];
          else SF(b, j < 2 ? s1 : s2);
        });
    }
  } else {
    int sl[4] = { 0, 0, 0, 0 }, t, bi = sh ? (mixed ? 2 : 1) : 0;
    int is = w->hdr.mode == 1 && (w->hdr.mext & 1) && ch == 1;
    if (!is) {
      if (sc < 400) {
        sl[0] = (sc >> 4) / 5;  sl[1] = (sc >> 4) % 5;
        sl[2] = (sc & 15) >> 2;  sl[3] = sc & 3;  t = 0;
      } else if (sc < 500) {
        int s = sc - 400;
        sl[0] = (s >> 2) / 5;  sl[1] = (s >> 2) % 5;  sl[2] = s & 3;  t = 1;
      } else { int s = sc - 500;  sl[0] = s / 3;  sl[1] = s % 3;  t = 2; }
    } else {
      int s = sc >> 1;
      if (s < 180) {
        sl[0] = s / 36;  sl[1] = (s % 36) / 6;  sl[2] = (s % 36) % 6;  t = 3;
      } else if (s < 244) {
        s -= 180;
        sl[0] = (s & 63) >> 4;  sl[1] = (s & 15) >> 2;  sl[2] = s & 3;  t = 4;
      } else { s -= 244;  sl[0] = s / 3;  sl[1] = s % 3;  t = 5; }
    }
    Fi(4,
      for (j = 0; j < mp3_lsf_nr[bi * 24 + t * 4 + i] && e < 40; j++)
        SF(e++, sl[i]));
  }
#undef SF
  w->sfi = 0;
}

static void finish_vals(mw * w) {
  memcpy(w->lastb[w->ch], w->curb, sizeof w->curb);
  memcpy(w->lscf[w->ch], w->scf[w->ch], sizeof w->scf[0]);
}

static void enter_c1(mw * w) {
  if (w->k >= 576) { w->ms = M_EXTRA;  return; }
  w->c1tbl = V(w, F_C1T);  w->node = 0;  w->depth = 0;  w->c1q = 0;
  w->ms = M_C1CODE;
}

static void enter_big(mw * w) {
  i16 * cur = CUR(w);
  while (w->k < w->bigend) {
    int t = w->val[w->gr][w->ch][F_TS][w->k < w->r1 ? 0 : w->k < w->r2 ? 1 : 2];
    if (TREE_OF[t] < 0) {
      cur[w->k] = cur[w->k + 1] = 0;  w->k += 2;  continue;
    }
    w->tbl = t;  w->tree = TREE[TREE_OF[t]];
    w->lin = mp3_huff_dims[t][2];  w->node = 0;  w->depth = 0;  w->ms = M_HCODE;
    return;
  }
  enter_c1(w);
}

static void next_scf(mw * w) {
  while (w->sfi < w->nsf && !w->sf[w->sfi].bits)
    w->scf[w->ch][w->sf[w->sfi++].entry] = 0;
  if (w->sfi < w->nsf) { w->ms = M_SCF;  w->sfbit = 0;  w->sfval = 0;  return; }
  w->k = 0;
  enter_big(w);
}

static void start_gc(mw * w) {
  while (w->gc < w->ngc) {
    w->gr = w->gc / w->hdr.nch;  w->ch = w->gc % w->hdr.nch;
    w->gleft = V(w, F_P23);
    memset(w->curb, 0, sizeof w->curb);
    build_entries(w);  build_scf(w);
    if (w->gleft > 0) { next_scf(w);  return; }
    finish_vals(w);
    w->gc++;
  }
  w->ms = M_PAD;
}

static void after_y(mw * w);
static void start_y(mw * w) {
  if (w->lin && w->yv == 15) {
    w->ms = M_LINY;  w->linleft = w->lin;  w->linval = 0;
  } else after_y(w);
}
static void after_x(mw * w) { if (w->x) w->ms = M_SIGNX;  else start_y(w); }
static void pair_done(mw * w) {
  CUR(w)[w->k] = (i16) w->x;  CUR(w)[w->k + 1] = (i16) w->yv;
  w->k += 2;
  enter_big(w);
}
static void after_y(mw * w) { if (w->yv) w->ms = M_SIGNY;  else pair_done(w); }

static void next_c1sign(mw * w) {
  while (w->c1idx < 4 && !(w->c1q >> (3 - w->c1idx) & 1)) w->c1idx++;
  if (w->c1idx < 4 && w->k + w->c1idx < 576) { w->ms = M_C1SIGN;  return; }
  w->k += 4;
  enter_c1(w);
}

static void main_bit(mw * w, int bit) {
  i16 * cur = CUR(w);
  int i;
  if (w->ms == M_PAD) return;
  w->gleft--;
  switch (w->ms) {
  case M_SCF:
    w->sfval = w->sfval << 1 | bit;
    if (++w->sfbit == w->sf[w->sfi].bits) {
      w->scf[w->ch][w->sf[w->sfi].entry] = (u8) w->sfval;
      w->sfi++;
      next_scf(w);
    }
    break;
  case M_HCODE: {
    int nx = w->tree[2 * w->node + bit];
    w->depth++;
    if (nx > 0) w->node = nx;
    else if (nx < 0) {
      int sym = -nx - 1;
      w->x = sym >> 4;  w->yv = sym & 15;
      if (w->lin && w->x == 15) {
        w->ms = M_LINX;  w->linleft = w->lin;  w->linval = 0;
      } else after_x(w);
    } else w->ms = M_EXTRA;
    break;
  }
  case M_LINX:
    w->linval = w->linval << 1 | bit;
    if (!--w->linleft) { w->x += w->linval;  after_x(w); }
    break;
  case M_SIGNX:
    if (bit) w->x = -w->x;
    start_y(w);
    break;
  case M_LINY:
    w->linval = w->linval << 1 | bit;
    if (!--w->linleft) { w->yv += w->linval;  after_y(w); }
    break;
  case M_SIGNY:
    if (bit) w->yv = -w->yv;
    pair_done(w);
    break;
  case M_C1CODE:
    w->depth++;
    if (!w->c1tbl) {
      int nx = TREE[QTREE][2 * w->node + bit];
      if (nx > 0) { w->node = nx;  break; }
      w->c1q = nx < 0 ? -nx - 1 : 0;
    } else {
      w->c1q = w->c1q << 1 | bit;
      if (w->depth < 4) break;
      w->c1q = ~w->c1q & 15;
    }
    Fi(4, if (w->k + i < 576) cur[w->k + i] = (i16) (w->c1q >> (3 - i) & 1));
    w->c1idx = 0;
    next_c1sign(w);
    break;
  case M_C1SIGN:
    if (bit) cur[w->k + w->c1idx] = (i16) -cur[w->k + w->c1idx];
    w->c1idx++;
    next_c1sign(w);
    break;
  default: break;
  }
  if (w->gleft <= 0 && w->ms != M_PAD) {
    finish_vals(w);  w->gc++;  start_gc(w);
  }
}

static void end_frame(mw * w) {
  memcpy(w->pval, w->val, sizeof w->val);
  memcpy(w->ph, w->head, 4);
  w->fidx++;
  if (w->fidx < w->nfr) w_enter(w, S_HEAD, 32);
  else w_enter(w, S_TAIL, (i64) 1 << 62);
}

static void start_main(mw * w) {
  if (!w->mainbytes) { end_frame(w);  return; }
  w_enter(w, S_MAIN, 8 * w->mainbytes);
  w->gc = 0;
  start_gc(w);
}

static u32 crc16(const u8 * h, const u8 * s, int n) {
  u32 c = mp3_crc8(mp3_crc8(0xFFFF, h[2]), h[3]);
  int i;
  Fi(n, c = mp3_crc8(c, s[i]));
  return c;
}

static void next_state(mw * w) {
  switch (w->state) {
  case S_HEAD: w_enter(w, S_SIDE, 8 * w->hdr.sbytes);  break;
  case S_SIDE:
    if (w->hdr.crc) {
      w->crc = crc16(w->head, w->side, w->hdr.sbytes);
      w->cbit = 0;  w_enter(w, S_CRC, 16);
      break;
    }
    /*  Fall through.  */
  case S_CRC: {
    int bits = 0, i, j;
    i64 s, l, prev = w->lay.end;
    Fi(w->hdr.lsf ? 1 : 2, Fj(w->hdr.nch, bits += w->val[i][j][F_P23][0]));
    lay_place(&w->lay, &w->hdr, w->val[0][0][F_MDB][0], bits, &s, &l);
    w->ngc = (w->hdr.lsf ? 1 : 2) * w->hdr.nch;
    w->mainbytes = l;
    if (s > prev) w_enter(w, S_GAP, 8 * (s - prev));
    else start_main(w);
    break;
  }
  case S_GAP: start_main(w);  break;
  case S_MAIN: end_frame(w);  break;
  default: w->left = (i64) 1 << 62;  break;
  }
}

static void w_enter(mw * w, int s, i64 bits) {
  w->state = s;  w->left = bits;
  if (s == S_HEAD) w->hn = 0;
  if (s == S_SIDE) start_side(w);
}

static void w_segment(mw * w, const mseg * sg) {
  w->acc = 0;  w->nb = 0;
  w->lay.pay = w->lay.end = 0;
  w->nfr = sg->nfr;  w->fidx = 0;  w->freeb = sg->freeb;
  if (sg->run && sg->nfr) w_enter(w, S_HEAD, 32);
  else w_enter(w, S_RAW, (i64) 1 << 62);
}

static NOINLINE void w_byte(mw * w, u8 c) {
  w->last4 = w->last4 << 8 | c;
  if (w->state == S_HEAD) {
    w->head[w->hn++] = c;
    if (w->hn == 4 && !mh_parse(w->last4, w->freeb, &w->hdr))
      w_enter(w, S_TAIL, (i64) 1 << 62);
  } else if (w->state == S_SIDE) {
    if (w->sn < 32) w->side[w->sn++] = c;
  }
}

static NOINLINE void w_slow(mw * w, int bit) {
  if (w->state == S_SIDE) side_bit(w, bit);
  else if (w->state == S_MAIN) main_bit(w, bit);
  else if (w->state == S_CRC) w->cbit++;
}

static INLINE void w_consume(mw * w, int bit) {
  int nx;
  if (w->state == S_MAIN && w->ms == M_HCODE && w->gleft > 1 &&
      (nx = w->tree[2 * w->node + bit]) > 0) {
    w->node = nx;  w->depth++;  w->gleft--;
  } else w_slow(w, bit);
  w->acc = w->acc << 1 | (u32) bit;
  w->left--;
  if (++w->nb == 8) { w_byte(w, (u8) w->acc);  w->acc = 0;  w->nb = 0; }
  while (UNLIKELY(!w->left)) next_state(w);
}

#define K1 0x9E3779B97F4A7C15ULL
#define K2 0xD6E8FEB86659FD93ULL
#define HM(h, v) (((h) + (u64) (v) + 1) * K1)

static INLINE u64 hx(u64 h) { h ^= h >> 31;  h *= K2;  return h ^ h >> 29; }

static u64 cx_bytes(const mw * w) {
  u64 s = HM(w->state, w->acc | 1u << w->nb);
  return HM(w->state == S_HEAD ? HM(HM(s, w->hn), w->ph[w->hn & 3])
                               : HM(s, w->last4 & 0xFF), 0);
}

static u64 cx_side(const mw * w) {
  const sfield * f = &w->fl[MIN(w->fi, w->nf - 1)];
  u64 cp = HM(HM(HM(HM(20, f->id), f->sub), w->fbit), w->fval);
  if (w->fi >= w->nf) cp = HM(30, 0);
  return HM(f->id == F_MDB ? HM(cp, (int) (w->lay.pay - w->lay.end))
                           : HM(cp, w->pval[f->gr][f->ch][f->id][f->sub]), 40);
}

static u64 cx_crc(const mw * w) {
  return HM(HM(HM(90, w->cbit), w->crc >> (15 - (w->cbit & 15)) & 1), 0);
}

#include "mp3/cfg.c"

#define MP_TBITS 18

typedef struct {
  int dec;
  rc_enc e;  rc_dec d;
  u32 * t;
  int recip[MP_LIMIT + 1];
  mw * wk;
} mcod;

static void mc_init(mcod * m, int dec) {
  sz i;
  memset(m, 0, sizeof *m);
  m->dec = dec;
  for (i = 0; i <= MP_LIMIT; i++) m->recip[i] = 2 * 65536 / (2 * (int) i + 3);
  m->t = xmalloc(((sz) 1 << MP_TBITS) * sizeof *m->t);
  Fi((sz) 1 << MP_TBITS, m->t[i] = 1u << 31);
  m->wk = xcalloc(1, sizeof *m->wk);
}

static void mc_free(mcod * m) { xfree(m->t);  xfree(m->wk); }

static INLINE u64 cx_fast(const mw * w) {
  const i16 * L, * cur;
  int kk, a, l1;
  u64 cp;
  switch (w->state) {
  case S_SIDE: return cx_side(w);
  case S_CRC: return cx_crc(w);
  case S_MAIN: break;
  default: return cx_bytes(w);
  }
  if (w->ms == M_SCF) {
    int en = w->sf[w->sfi].entry;
    return HM(HM(HM(HM(HM(40, w->sf[w->sfi].bits), w->sfbit), w->sfval),
                 w->lscf[w->ch][en]), en);
  }
  kk = w->k;
  switch (w->ms) {
  case M_HCODE: cp = HM(60, w->tbl << 9 | w->node);  break;
  case M_LINX:
    cp = HM(61, w->lin << 8 | w->linleft << 4 | (w->linval & 15));
    break;
  case M_LINY:
    cp = HM(62, w->lin << 8 | w->linleft << 4 | (w->linval & 15));
    kk++;
    break;
  case M_SIGNX: return HM(63, 0);
  case M_SIGNY: return HM(64, 0);
  case M_C1CODE:
    cp = HM(65, w->c1tbl << 9 |
                (w->c1tbl ? w->c1q | 16 << w->depth : w->node));
    break;
  case M_C1SIGN: return HM(66, 0);
  default: return HM(67, w->ms);
  }
  kk = MIN(kk, 575);
  L = LAST(w, w->ch);  cur = CUR(w);
  a = ABS(L[kk]);  l1 = ABS(cur[kk - 1]);
  a = MIN(a, 3);  l1 = MIN(l1, 3);
  return HM(cp, a << 2 | l1);
}

static void mc_segment(mcod * m, const mseg * sg, const u8 * in, u8 * out) {
  mw * w = m->wk;
  const int dec = m->dec;
  sz i;
  int b;
  w_segment(w, sg);
  Fi(sg->len,
    u32 c = 0;
    for (b = 7; b >= 0; b--) {
      u32 * s = m->t + (hx(cx_fast(w)) >> 20 & (((u64) 1 << MP_TBITS) - 1));
      u32 v, p0;
      int bit, n, p;
      v = *s;
      p0 = 65536 - (v >> 16);
      p0 = CLAMP(p0, 32, 65504);
      if (dec) bit = rc_dec_bit_raw(&m->d, p0);
      else { bit = in[i] >> b & 1;  rc_enc_bit_raw(&m->e, p0, bit); }
      n = (int) (v & 1023);  p = (int) (v >> 10);
      p += (int) (((i64) ((bit ? (1 << 22) - 1 : 0) - p) * m->recip[n]) >> 16);
      *s = (u32) p << 10 | (u32) (n < MP_LIMIT ? n + 1 : MP_LIMIT);
      c = c << 1 | (u32) bit;
      w_consume(w, bit);
    }
    if (dec) { out[i] = (u8) c;  if (rc_dec_spent(&m->d)) return; });
}

#define RSB 15
#define RST (1 << RSB)
#define RSL (1u << 16)
#define NLANE 4

typedef struct {
  u16 * c;  u8 * lut;  u32 * cnt;
  u32 seen, next, tot, sb;
  u16 n;  u8 sh, due;
} rsx;

#define ZPAIR 0
#define ZQUAD (ZPAIR + MP3_NTREE * 81)
#define ZFLAG (ZQUAD + 8)
#define ZSCF  (ZFLAG + 1)
#define ZSIDE (ZSCF + 32)
#define ZSMAL (ZSIDE + F_N)
#define ZHDR  (ZSMAL + F_N * 16)
#define ZGAP  (ZHDR + 38)
#define ZRAW  (ZGAP + 1)
#define ZSGN  (ZRAW + 1)
#define ZN    (ZSGN + 36)
#define ZEOB  16
#define RS_PRIOR 15
#define RS_UNIT  8

static const u8 SIDEA[F_N] = { 2, 32, 2, 2, 2, 2, 16, 2, 4, 2, 32, 8, 16, 8, 2, 2, 2 };

static u16 HCODE[MP3_NTREE][256];
static u8 HLEN[MP3_NTREE][256], HXY[MP3_NTREE][256];
static u8 HXC[MP3_NTREE][256];
static u32 PKT[32][256];
static u16 QKT[2][16];
#define TPEEK 10
static u16 TFAST[MP3_NTREE + 1][1 << TPEEK];
static u16 TFULL[32][1 << TPEEK];

static void tcode_init(void) {
  int i, j;
  Fj(MP3_NTREE,
    const mp3_huff * h = &mp3_pairs[j];
    for (i = 0; h->code && i < h->dim * h->dim; i++) {
      HCODE[j][i] = (u16) h->code[i];
      HXY[j][i] = (u8) ((i / h->dim) << 4 | i % h->dim);
      HLEN[j][i] = h->len[i];
      HXC[j][i] = (u8) (MIN(i / h->dim, 2) << 4 | MIN(i % h->dim, 2));
    });
  Fj(32,
    int tr = TREE_OF[j], lin = mp3_huff_dims[j][2];
    if (tr < 0) continue;
    Fi(mp3_pairs[tr].dim * mp3_pairs[tr].dim,
      int x = HXY[tr][i] >> 4, y = HXY[tr][i] & 15;
      u32 e = (u32) (x != 0) + (y != 0) + (u32) (lin && x == 15) * (u32) lin +
              (u32) (lin && y == 15) * (u32) lin;
      PKT[j][i] = (u32) HCODE[tr][i] << 11 | (HLEN[tr][i] + e) << 5 | e));
  Fj(2,
    Fi(16,
      QKT[j][i] = (u16) (j ? (u32) (~i & 15) << 8 | 4
                           : mp3_quad.code[i] << 8 | mp3_quad.len[i])));
}

static void tfast_init(void) {
  int t, i, j, d;
  for (t = 0; t <= MP3_NTREE; t++) {
    if (t < MP3_NTREE && !mp3_pairs[t].code) continue;
    Fi(1 << TPEEK,
      int node = 0, nx = 0;
      for (d = 1; d <= TPEEK; d++) {
        nx = TREE[t][2 * node + (i >> (TPEEK - d) & 1)];
        if (nx <= 0) break;
        node = nx;
      }
      if (d > TPEEK || nx == 0) continue;
      nx = -nx - 1;
      TFAST[t][i] = (u16) (d << 8 | (t < MP3_NTREE
                                     ? (nx >> 4) * mp3_pairs[t].dim + (nx & 15)
                                     : nx)));
  }
  Fj(32,
    for (i = 0; TREE_OF[j] >= 0 && i < 1 << TPEEK; i++) {
      u32 e = TFAST[TREE_OF[j]][i];
      if (e) TFULL[j][i] = (u16) ((PKT[j][e & 255] >> 5 & 63) << 8 | (e & 255));
    });
}

typedef struct {
  int mode;
  rsx cx[ZN];
  blr_arena ar;
  u32 * ev;  sz nev, cev;
  u32 x[NLANE];  unsigned rr;
  const u8 * p, * pe;
  rsx * due[ZN];  int ndue;
  u8 * rb;  sz rcap, rlen;
  u8 * rp;  u64 rbb;  int rbn;
  u64 rbits;
  u8 * m;  i64 pos, gend;
  u8 * op;  u64 wb;  int wn;
  int bad;
  u16 itk[512], itc[512];
  u8 itt[512], itr[512], its[512];
  u8 sv[48];
  int sgn;
  int zmul;
  u32 rbk, hv;
  u16 sgc[580];  u8 sgs[580];
  u8 sgp[512 + 144];
  u8 signs[3][580];
  u8 * csg, * psg[2];
  u8 lines[3][584];
  u8 * cb, * pmb[2];
  mw * w;
} fz;

static void rs_prior(fz * z, int k) {
  rsx * x = &z->cx[k];
  const u8 * len = NULL;
  u8 ql[17];
  int s0 = 0, i;
  u32 c0 = 1;
  if (k < ZQUAD) {
    if (mp3_pairs[(k - ZPAIR) / 81].code) len = HLEN[(k - ZPAIR) / 81];
  } else if (k < ZFLAG) {
    if (k - ZQUAD < 4) {
      Fi(16, ql[i] = (u8) (QKT[0][i] & 255));
      ql[16] = 9;  len = ql;
    }
  } else if (k == ZFLAG) c0 = 16;
  else if (k >= ZSIDE && k < ZSMAL) { s0 = 128;  c0 = 64; }
  else if (k >= ZHDR && k < ZSGN) c0 = k < ZGAP ? 64 : 8;
  x->tot = 0;
  Fi(x->n,
    x->cnt[i] = len ? (len[i] < RS_PRIOR ? (1u << RS_PRIOR) >> len[i] : 1)
                    : (i == s0 ? c0 : 1) << RS_UNIT;
    x->tot += x->cnt[i]);
}

static void rs_build(rsx * x, int dec) {
  int i, best = 0, n = x->n, sh = x->sh;
  u32 sum = 0, bc = 0, t = x->tot ? x->tot : 1, b0 = 0;
  u64 r = ((u64) (RST - n) << 32) / t;
  u16 * c = x->c;
  Fi(n,
    u32 f = 1 + (u32) (x->cnt[i] * r >> 32);
    c[i + 1] = (u16) f;
    sum += f;
    if (x->cnt[i] > bc) { bc = x->cnt[i];  best = i; });
  c[best + 1] = (u16) (c[best + 1] + RST - sum);
  c[0] = 0;
  Fi(n, c[i + 1] = (u16) (c[i + 1] + c[i]));
  for (i = 0; dec && i < n; i++) {
    u32 b1 = ((u32) c[i + 1] + (1u << sh) - 1) >> sh;
    if (b1 > b0) {
      u64 w = (u64) i * 0x0101010101010101u;
      do { memcpy(x->lut + b0, &w, 8);  b0 += 8; } while (b0 < b1);
      b0 = b1;
    }
  }
}

static NOINLINE void rs_setup(fz * z, int k) {
  rsx * x = &z->cx[k];
  int n = k < ZQUAD ? MAX(mp3_pairs[(k - ZPAIR) / 81].dim, 1) *
                      MAX(mp3_pairs[(k - ZPAIR) / 81].dim, 1)
        : k < ZFLAG ? 17 : k == ZFLAG ? 2
        : k < ZSIDE ? 1 << ((k - ZSCF) / 4 + 1) : k < ZSMAL ? 256
        : k < ZHDR ? SIDEA[(k - ZSMAL) / 16] : k < ZSGN ? 256 : 2;
  int lb = 2;
  u8 * a;
  if (n < 2) n = 2;
  while (z->mode && lb < 11 && 1 << lb < 4 * n) lb++;
  a = ar_take(&z->ar, (sz) n * 4 + (sz) (n + 1) * 2 + ((sz) 1 << lb) + 8);
  x->n = (u16) n;  x->sh = (u8) (RSB - lb);
  x->cnt = (u32 *) a;  x->c = (u16 *) (a + (sz) n * 4);
  x->lut = a + (sz) n * 4 + (sz) (n + 1) * 2;
  x->seen = 0;  x->sb = 0;  x->next = 8;  x->due = 0;
  rs_prior(z, k);
  rs_build(x, z->mode);
}

#define RS_COUNT(z_, x_, s_) do { (x_)->cnt[s_] += 1u << RS_UNIT;              \
    if (UNLIKELY(++(x_)->seen == (x_)->next)) {                                \
      (x_)->next = (x_)->seen < (z_)->rbk ? (x_)->seen * 2                     \
                                          : (x_)->seen + (z_)->rbk;            \
      if (!(x_)->due) { (x_)->due = 1;  (z_)->due[(z_)->ndue++] = (x_); }      \
    } } while (0)

static void fz_settle(fz * z) {
  int i, j;
  Fi(z->ndue,
    rsx * x = z->due[i];
    x->tot += (x->seen - x->sb) << RS_UNIT;  x->sb = x->seen;
    if (x->tot > (1u << (z->hv + RS_UNIT))) {
      x->tot = 0;
      Fj(x->n, x->tot += x->cnt[j] = (x->cnt[j] + 1) / 2);
    }
    rs_build(x, z->mode);
    x->due = 0);
  z->ndue = 0;
}

static INLINE u32 * fz_ev(fz * z, sz n) {
  if (UNLIKELY(z->nev + n > z->cev))
    z->ev = xgrow(z->ev, &z->cev, z->nev + n, sizeof *z->ev, (sz) 1 << 16);
  return z->ev + z->nev;
}

static INLINE u32 rs_ev(const rsx * x, u32 s, u32 l) {
  return x->c[s] | (u32) (x->c[s + 1] - x->c[s]) << 15 | l << 30;
}

static INLINE void rs_put(fz * z, int k, int s, unsigned l) {
  rsx * x = &z->cx[k];
  if (UNLIKELY(!x->c)) rs_setup(z, k);
  *fz_ev(z, 1) = rs_ev(x, (u32) s, l);  z->nev++;
  RS_COUNT(z, x, s);
}

static INLINE u32 rs_dec(const rsx * x, u32 * st, const u8 ** p,
                         const u8 * pe) {
  u32 y = *st, v = y & (RST - 1), s = x->lut[v >> x->sh], c;
  while (x->c[s + 1] <= v) s++;
  c = x->c[s];
  y = (u32) (x->c[s + 1] - c) * (y >> RSB) + v - c;
  { u32 r = y < RSL, w = rd16le(*p);
    y = y << (r << 4) | (w & (0u - r));
    *p += 2 * r;
    *p = MIN(*p, pe); }
  *st = y;
  return s;
}

static INLINE int rs_get(fz * z, int k, u32 * st) {
  rsx * x = &z->cx[k];
  u32 s;
  if (UNLIKELY(!x->c)) rs_setup(z, k);
  s = rs_dec(x, st, &z->p, z->pe);
  RS_COUNT(z, x, s);
  return (int) s;
}

static INLINE int fz_sym(fz * z, int k, int s) {
  unsigned l = z->rr++ & (NLANE - 1);
  if (z->mode) s = rs_get(z, k, &z->x[l]);
  else rs_put(z, k, s, l);
  if (UNLIKELY(z->ndue)) fz_settle(z);
  return s;
}

#if defined(__GNUC__) && defined(__BYTE_ORDER__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
static INLINE u64 be64(const u8 * p) {
  u64 v;  memcpy(&v, p, 8);  return __builtin_bswap64(v);
}
static INLINE void st64be(u8 * p, u64 v) {
  v = __builtin_bswap64(v);  memcpy(p, &v, 8);
}
#elif defined(__GNUC__) && defined(__BYTE_ORDER__) && \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
static INLINE u64 be64(const u8 * p) { u64 v;  memcpy(&v, p, 8);  return v; }
static INLINE void st64be(u8 * p, u64 v) { memcpy(p, &v, 8); }
#else
static INLINE u64 be64(const u8 * p) {
  return (u64) p[0] << 56 | (u64) p[1] << 48 | (u64) p[2] << 40 |
         (u64) p[3] << 32 | (u64) p[4] << 24 | (u64) p[5] << 16 |
         (u64) p[6] << 8 | p[7];
}
static INLINE void st64be(u8 * p, u64 v) {
  int k;  Fk(8, p[k] = (u8) (v >> (56 - 8 * k)));
}
#endif

static INLINE u32 bits_at(const u8 * b, u64 p, int n) {
  return n ? (u32) (be64(b + (p >> 3)) << (p & 7) >> (64 - n)) : 0;
}

static INLINE void bits_or(u8 * b, u64 p, int n, u32 v) {
  if (!n) return;
  st64be(b + (p >> 3),
         be64(b + (p >> 3)) | (u64) v << (64 - (int) (p & 7) - n));
}

#define BW_PUT(op_, wb_, wn_, v_, n_) do {                                     \
    (wb_) |= (u64) (v_) << (63 - (wn_) - (n_)) << 1;  (wn_) += (n_);           \
    st64be(op_, wb_);  (op_) += (wn_) >> 3;  (wb_) <<= (wn_) & 56;             \
    (wn_) &= 7; } while (0)

static INLINE void rput(fz * z, int n, u32 v) {
  BW_PUT(z->rp, z->rbb, z->rbn, v, n);  z->rbits += (u64) n;
}

static void rroom(fz * z) {
  sz at = (sz) (z->rp - z->rb), c = z->rcap;
  if (at + 4096 <= c) return;
  z->rb = xgrow(z->rb, &z->rcap, at + 4096, 1, (sz) 1 << 16);
  memset(z->rb + c, 0, z->rcap - c);  z->rp = z->rb + at;
}

#define BR_GET(rp_, rbb_, rbn_, rend_, v_, n_) do {                            \
    (rbb_) |= be64(rp_) >> (rbn_);  (rp_) += (63 - (rbn_)) >> 3;               \
    (rbn_) |= 56;  (rp_) = MIN(rp_, rend_);                                    \
    (v_) = (u32) ((rbb_) >> 32 >> (32 - (n_)));  (rbb_) <<= (n_);              \
    (rbn_) -= (n_); } while (0)

static INLINE u32 rget(fz * z, int n) {
  u32 v;
  BR_GET(z->rp, z->rbb, z->rbn, z->rb + z->rlen, v, n);
  z->rbits += (u64) n;
  return v;
}

static INLINE void mput(fz * z, int n, u64 v) {
  BW_PUT(z->op, z->wb, z->wn, v, n);
}

static INLINE void fz_raw(fz * z, int n) {
  if (z->pos + n > z->gend) { z->bad = 1;  return; }
  if (z->mode) mput(z, n, rget(z, n));
  else rput(z, n, bits_at(z->m, (u64) z->pos, n));
  z->pos += n;
}

static void fz_rawrest(fz * z) {
  while (!z->bad && z->pos < z->gend)
    fz_raw(z, (int) MIN(32, z->gend - z->pos));
}

#define FZ_CODE(c_, i_, j_, sv_, s_) do {                                      \
    rsx * x_ = &cx[c_];                                                        \
    if (UNLIKELY(!x_->c)) rs_setup(z, c_);                                     \
    if (dec) its[i_] = (u8) ((s_) = rs_dec(x_, &(sv_), &p, pe));               \
    else { (s_) = its[i_];  *ev++ = rs_ev(x_, s_, (u32) (j_)); }               \
    RS_COUNT(z, x_, s_); } while (0)

#define FZ_ITEM(i_, j_, sv_) do {                                              \
    int k_ = itk[i_], c_ = itc[i_] + cur[k_ - 1] * 9 + pm[k_] * 3 + pm[k_ + 1];\
    u32 s_, xy_;                                                               \
    FZ_CODE(c_, i_, j_, sv_, s_);                                              \
    xy_ = HXC[itr[i_]][s_];                                                    \
    cur[k_] = (u8) (xy_ >> 4);  cur[k_ + 1] = (u8) (xy_ & 15); } while (0)

static INLINE void fz_lanes(fz * z, int n, const u8 * pm, const int dec) {
  u8 * cur = z->cb + 4, * its = z->its;
  const u16 * itk = z->itk, * itc = z->itc;
  const u8 * itr = z->itr, * p = z->p, * pe = z->pe;
  rsx * cx = z->cx;
  u32 * ev = NULL;
  int nl = n >= 16 ? NLANE : n >= 8 ? 2 : 1, ls[NLANE + 1], j, st;
  int steps = (n + nl - 1) / nl;
  if (!dec) ev = fz_ev(z, (sz) n);
  for (j = 0; j <= nl; j++) ls[j] = j * n / nl;
  for (j = 1; j < nl; j++) {
    int k = itk[ls[j]];
    if (itk[ls[j] - 1] == k - 2) cur[k - 1] = pm[k - 1];
  }
  if (nl == NLANE) {
    u32 s0 = z->x[0], s1 = z->x[1], s2 = z->x[2], s3 = z->x[3];
    int a0 = ls[0], a1 = ls[1], a2 = ls[2], a3 = ls[3], full = n / NLANE;
    for (st = 0; st < full; st++) {
      FZ_ITEM(a0 + st, 0, s0);  FZ_ITEM(a1 + st, 1, s1);
      FZ_ITEM(a2 + st, 2, s2);  FZ_ITEM(a3 + st, 3, s3);
      if (UNLIKELY(z->ndue)) fz_settle(z);
    }
    if (a1 - a0 > full) FZ_ITEM(a0 + full, 0, s0);
    if (a2 - a1 > full) FZ_ITEM(a1 + full, 1, s1);
    if (a3 - a2 > full) FZ_ITEM(a2 + full, 2, s2);
    if (n - a3 > full) FZ_ITEM(a3 + full, 3, s3);
    if (UNLIKELY(z->ndue)) fz_settle(z);
    z->x[0] = s0;  z->x[1] = s1;  z->x[2] = s2;  z->x[3] = s3;
  } else
    for (st = 0; st < steps; st++) {
      Fj(nl,
        int i = ls[j] + st;
        if (i < ls[j + 1]) FZ_ITEM(i, j, z->x[j]));
      if (UNLIKELY(z->ndue)) fz_settle(z);
    }
  z->p = p;
  if (!dec) z->nev = (sz) (ev - z->ev);
}

static INLINE u16 fz_sgctx(const fz * z, int ch, int k, int big) {
  return (u16) (ZSGN + ch * 18 + big * 9 + z->psg[ch][k] * 3 +
                (ch ? z->psg[0][k] : 0));
}

static INLINE void fz_signs(fz * z, int n, const int dec) {
  const u16 * itc = z->sgc;
  u8 * its = z->sgs;
  const u8 * p = z->p, * pe = z->pe;
  rsx * cx = z->cx;
  u32 * ev = NULL, s0 = z->x[0], s1 = z->x[1], s2 = z->x[2], s3 = z->x[3];
  int i;
  if (!dec) ev = fz_ev(z, (sz) n);
#define FZ_SIGN(i_, j_, sv_) do {                                              \
    int c_ = itc[i_];  u32 s_;  FZ_CODE(c_, i_, j_, sv_, s_); } while (0)
  for (i = 0; i + NLANE <= n; i += NLANE) {
    FZ_SIGN(i, 0, s0);  FZ_SIGN(i + 1, 1, s1);
    FZ_SIGN(i + 2, 2, s2);  FZ_SIGN(i + 3, 3, s3);
    if (UNLIKELY(z->ndue)) fz_settle(z);
  }
  if (i < n) FZ_SIGN(i, 0, s0);
  if (i + 1 < n) FZ_SIGN(i + 1, 1, s1);
  if (i + 2 < n) FZ_SIGN(i + 2, 2, s2);
#undef FZ_SIGN
  if (UNLIKELY(z->ndue)) fz_settle(z);
  z->x[0] = s0;  z->x[1] = s1;  z->x[2] = s2;  z->x[3] = s3;
  z->p = p;
  if (!dec) z->nev = (sz) (ev - z->ev);
}

static int fz_pair_signs(fz * z, int np, int ch, const int dec) {
  const u16 * itk = z->itk;
  const u8 * itt = z->itt, * its = z->its, * sgp = z->sgp;
  u16 * sgc = z->sgc;
  u8 * sgs = z->sgs;
  int i, n = 0;
  Fi(np,
    int k = itk[i], xy = HXY[TREE_OF[itt[i]]][its[i]], x = xy >> 4, y = xy & 15;
    sgc[n] = fz_sgctx(z, ch, k, x > 1);
    if (!dec) sgs[n] = (u8) (sgp[i] >> 1);
    n += x != 0;
    sgc[n] = fz_sgctx(z, ch, k + 1, y > 1);
    if (!dec) sgs[n] = (u8) (sgp[i] & 1);
    n += y != 0);
  return n;
}

static INLINE int pop4(int q) {
  return (q & 1) + (q >> 1 & 1) + (q >> 2 & 1) + (q >> 3 & 1);
}

static int fz_quad_signs(fz * z, int np, int nq, int kq, int ch,
                         const int dec) {
  int i, j, n = 0;
  Fi(nq,
    int q = z->its[i], k = kq + 4 * i, ns = 0, nb;
    int sg = dec ? 0 : z->sgp[np + i];
    if (k + 4 > 576) q &= 0xC;
    nb = pop4(q);
    Fj(4,
      int b = q >> (3 - j) & 1;
      z->sgc[n] = fz_sgctx(z, ch, k + j, 0);
      if (!dec) z->sgs[n] = (u8) (sg >> ((nb - 1 - ns) & 3) & 1);
      n += b;  ns += b));
  return n;
}

static int fz_items(fz * z, int * nq0) {
  static const short ZEND[3] = { 32, 160, 576 };
  mw * w = z->w;
  u16 * itk = z->itk, * itc = z->itc;
  u8 * itt = z->itt, * itr = z->itr;
  int i, k = 0, n = 0, j;
  Fi(3,
    int end = MIN(i == 0 ? w->r1 : i == 1 ? w->r2 : 576, w->bigend), t, tr;
    if (k >= end) continue;
    t = w->val[w->gr][w->ch][F_TS][i];  tr = TREE_OF[t];
    if (tr < 0) {
      memset(z->cb + 4 + k, 0, (sz) (end - k));  k = end;  continue;
    }
    Fj(3,
      int lim = MIN(end, ZEND[j]);
      u16 c = (u16) (ZPAIR + tr * 81 + j * z->zmul);
      for (; k < lim; k += 2) {
        itk[n] = (u16) k;  itc[n] = c;  itt[n] = (u8) t;  itr[n] = (u8) tr;
        n++;
      }));
  *nq0 = n;
  return k;
}

static INLINE int fz_quads(fz * z, const u8 * pm, int kq, int c1t, int nq,
                           const int dec) {
  u8 * cur = z->cb + 4, * its = z->its;
  const u8 * p = z->p, * pe = z->pe;
  rsx * cx = z->cx;
  u32 st[NLANE], * ev = NULL;
  i64 pos = z->pos, gend = z->gend;
  int n = 0, k = kq, j;
  Fj(NLANE, st[j] = z->x[j]);
  if (!dec) ev = fz_ev(z, 160);
  while (pos < gend && k < 576) {
    int c = ZQUAD + c1t * 4 +
            MIN(pm[k] + pm[k + 1] + pm[k + 2] + pm[k + 3], 3), q, m;
    rsx * x = &cx[c];
    if (UNLIKELY(!x->c)) rs_setup(z, c);
    if (dec) q = (int) rs_dec(x, &st[n & (NLANE - 1)], &p, pe);
    else {
      q = n < nq ? its[n] : ZEOB;
      *ev++ = rs_ev(x, (u32) q, (u32) (n & (NLANE - 1)));
    }
    RS_COUNT(z, x, q);
    if (q == ZEOB) break;
    m = k + 4 > 576 ? q & 0xC : q;
    pos += (QKT[c1t][q] & 255) + pop4(m);
    its[n++] = (u8) q;
    cur[k] = (u8) (q >> 3);  cur[k + 1] = (u8) (q >> 2 & 1);
    cur[k + 2] = (u8) (q >> 1 & 1);  cur[k + 3] = (u8) (q & 1);
    k += 4;
    if (!(n & (NLANE - 1)) && UNLIKELY(z->ndue)) fz_settle(z);
  }
  if (UNLIKELY(z->ndue)) fz_settle(z);
  Fj(NLANE, z->x[j] = st[j]);
  z->p = p;
  if (!dec) z->nev = (sz) (ev - z->ev);
  return n;
}

static void fz_gran_end(fz * z, int k) {
  int ch = z->w->ch;
  memset(z->cb + 4 + k, 0, (sz) (580 - k));
  fz_rawrest(z);
  { u8 * b = z->pmb[ch];  z->pmb[ch] = z->cb;  z->cb = b; }
  if (z->sgn) { u8 * b = z->psg[ch];  z->psg[ch] = z->csg;  z->csg = b; }
}

static void fz_gran_dec(fz * z) {
  mw * w = z->w;
  u8 * pm = z->pmb[w->ch] + 4;
  int i, prev = 0, np, nq, kq, c1t = V(w, F_C1T) != 0;
  for (i = 0; i < w->nsf && !z->bad; i++) {
    int b = w->sf[i].bits;
    if (!b) continue;
    if (z->pos + b > z->gend) { z->bad = 1;  break; }
    if (b > 8) {
      prev = (int) rget(z, b);  mput(z, b, (u32) prev);  z->pos += b;
      continue;
    }
    prev = fz_sym(z, ZSCF + (b - 1) * 4 + MIN(prev, 3), 0);
    mput(z, b, (u32) prev);  z->pos += b;
  }
  if (z->bad) return;
  kq = fz_items(z, &np);
  fz_lanes(z, np, pm, 1);
  if (z->sgn) {
    int ns = fz_pair_signs(z, np, w->ch, 1), j = 0;
    u8 * csg = z->csg;
    fz_signs(z, ns, 1);
    memset(csg, 0, 580);
    for (i = 0; i < np && !z->bad; i++) {
      int t = z->itt[i], tr = TREE_OF[t], s = z->its[i], xy = HXY[tr][s];
      int x = xy >> 4, y = xy & 15, k = z->itk[i], lin = mp3_huff_dims[t][2];
      int nx = x != 0, ny = y != 0, sx, sy, nt = (int) (PKT[t][s] >> 5 & 63);
      sx = z->sgs[j] & nx;  j += nx;
      sy = z->sgs[j] & ny;  j += ny;
      if (z->pos + nt > z->gend) { z->bad = 1;  break; }
      csg[k] = (u8) ((1 + sx) & -nx);  csg[k + 1] = (u8) ((1 + sy) & -ny);
      if (LIKELY(!lin || (x < 15 && y < 15)))
        mput(z, HLEN[tr][s] + nx + ny,
             (u64) HCODE[tr][s] << (nx + ny) | (u32) (sx << ny | sy));
      else {
        mput(z, HLEN[tr][s], HCODE[tr][s]);
        if (x == 15) mput(z, lin, rget(z, lin));
        if (x) mput(z, 1, (u32) sx);
        if (y == 15) mput(z, lin, rget(z, lin));
        if (y) mput(z, 1, (u32) sy);
      }
      z->pos += nt;
    }
  } else {
    u8 * op = z->op, * rp = z->rp, * rend = z->rb + z->rlen;
    u64 wb = z->wb, rbb = z->rbb, rbits = 0;
    int wn = z->wn, rbn = z->rbn;
    i64 pos = z->pos, gend = z->gend;
    Fi(np,
      u32 e = PKT[z->itt[i]][z->its[i]], r;
      int ne = (int) (e & 31), nt = (int) (e >> 5 & 63);
      if (i + 1 < np) {
        u32 e2 = PKT[z->itt[i + 1]][z->its[i + 1]];
        int ne2 = (int) (e2 & 31), nt2 = (int) (e2 >> 5 & 63);
        if (LIKELY(nt + nt2 <= 56 && ne + ne2 <= 32 &&
                   pos + nt + nt2 <= gend)) {
          BR_GET(rp, rbb, rbn, rend, r, ne + ne2);
          BW_PUT(op, wb, wn,
                 ((u64) (e >> 11) << ne | r >> ne2) << nt2 |
                 (u64) (e2 >> 11) << ne2 | (r & ((1u << ne2) - 1)),
                 nt + nt2);
          pos += nt + nt2;  rbits += (u64) (ne + ne2);  i++;
          continue;
        }
      }
      if (UNLIKELY(pos + nt > gend)) { z->bad = 1;  break; }
      BR_GET(rp, rbb, rbn, rend, r, ne);
      BW_PUT(op, wb, wn, (u64) (e >> 11) << ne | r, nt);
      pos += nt;  rbits += (u64) ne);
    z->op = op;  z->rp = rp;  z->wb = wb;  z->rbb = rbb;
    z->wn = wn;  z->rbn = rbn;  z->pos = pos;  z->rbits += rbits;
  }
  if (z->bad) return;
  nq = fz_quads(z, pm, kq, c1t, 0, 1);
  if (z->sgn) {
    int ns = fz_quad_signs(z, np, nq, kq, w->ch, 1), j = 0, l;
    u8 * csg = z->csg;
    fz_signs(z, ns, 1);
    for (i = 0; i < nq && !z->bad; i++) {
      int q = z->its[i], k = kq + 4 * i, nb = 0, nt;
      u32 e = QKT[c1t][q], sb = 0;
      if (k + 4 > 576) q &= 0xC;
      for (l = 0; l < 4; l++) {
        int b = q >> (3 - l) & 1, sg = z->sgs[j] & b;
        sb = sb << b | (u32) sg;  csg[k + l] = (u8) ((1 + sg) & -b);
        j += b;  nb += b;
      }
      nt = (int) (e & 255) + nb;
      if (z->pos + nt > z->gend) { z->bad = 1;  break; }
      mput(z, nt, (u64) (e >> 8) << nb | sb);
      z->pos += nt;
    }
  } else {
    u8 * op = z->op, * rp = z->rp, * rend = z->rb + z->rlen;
    u64 wb = z->wb, rbb = z->rbb, rbits = 0;
    int wn = z->wn, rbn = z->rbn;
    i64 pos = z->pos, gend = z->gend;
    Fi(nq,
      int q = z->its[i], nb, nt;
      u32 e = QKT[c1t][q], r;
      if (kq + 4 * i + 4 > 576) q &= 0xC;
      nb = pop4(q);
      nt = (int) (e & 255) + nb;
      if (UNLIKELY(pos + nt > gend)) { z->bad = 1;  break; }
      BR_GET(rp, rbb, rbn, rend, r, nb);
      BW_PUT(op, wb, wn, (u64) (e >> 8) << nb | r, nt);
      pos += nt;  rbits += (u64) nb);
    z->op = op;  z->rp = rp;  z->wb = wb;  z->rbb = rbb;
    z->wn = wn;  z->rbn = rbn;  z->pos = pos;  z->rbits += rbits;
  }
  if (z->bad) return;
  fz_gran_end(z, kq + 4 * nq);
}

static int fz_parse(fz * z, int * nqp, int * kqp) {
  mw * w = z->w;
  int i, np, kq, nq = 0, c1t = V(w, F_C1T) != 0, ns = 0;
  Fi(w->nsf,
    int b = w->sf[i].bits;
    if (!b) continue;
    if (z->pos + b > z->gend) return -1;
    if (b > 8) {
      rput(z, b, bits_at(z->m, (u64) z->pos, b));  z->pos += b;  continue;
    }
    z->sv[ns++] = (u8) bits_at(z->m, (u64) z->pos, b);  z->pos += b);
  kq = fz_items(z, &np);
  { const u8 * m = z->m, * itt = z->itt;
    u8 * its = z->its, * op = z->rp;
    u64 wb = z->rbb, rbits = 0;
    int wn = z->rbn, ok = 1;
    i64 pos = z->pos, gend = z->gend;
    Fi(np,
      int t = itt[i], tr = TREE_OF[t], ix, ne;
      u64 b = be64(m + (pos >> 3)) << (pos & 7);
      u32 e = TFULL[t][b >> (64 - TPEEK)], r;
      if (LIKELY(e) && !z->sgn) {
        ix = (int) (e & 255);  ne = (int) (PKT[t][ix] & 31);
        r = (u32) (b << HLEN[tr][ix] >> 32 >> (32 - ne));
        pos += e >> 8;
        if (UNLIKELY(pos > gend)) { ok = 0;  break; }
        BW_PUT(op, wb, wn, r, ne);
        rbits += (u64) ne;
        its[i] = (u8) ix;
        continue;
      }
      if (e) {
        ix = (int) (e & 255);  pos += HLEN[tr][ix];  b <<= HLEN[tr][ix];
      } else {
        int node = 0, nx;
        for (;;) {
          nx = TREE[tr][2 * node + (int) bits_at(m, (u64) pos, 1)];
          pos++;
          if (nx <= 0 || pos > gend) break;
          node = nx;
        }
        if (nx >= 0 || pos > gend) { ok = 0;  break; }
        ix = ((-nx - 1) >> 4) * mp3_pairs[tr].dim + ((-nx - 1) & 15);
        b = be64(m + (pos >> 3)) << (pos & 7);
      }
      ne = (int) (PKT[t][ix] & 31);
      r = (u32) (b >> 32 >> (32 - ne));
      pos += ne;
      if (UNLIKELY(pos > gend)) { ok = 0;  break; }
      its[i] = (u8) ix;
      if (z->sgn) {
        int x = HXY[tr][ix] >> 4, y = HXY[tr][ix] & 15;
        int lin = mp3_huff_dims[t][2], at = ne, sg = 0;
        u32 lb = 0;
        int nl = 0;
        if (lin && x == 15) {
          at -= lin;  lb = r >> at & ((1u << lin) - 1);  nl = lin;
        }
        if (x) { at--;  sg = (int) (r >> at & 1) << 1; }
        if (lin && y == 15) {
          at -= lin;  lb = lb << lin | (r >> at & ((1u << lin) - 1));
          nl += lin;
        }
        if (y) { at--;  sg |= (int) (r >> at & 1); }
        z->sgp[i] = (u8) sg;
        BW_PUT(op, wb, wn, lb, nl);
        rbits += (u64) nl;
        continue;
      }
      BW_PUT(op, wb, wn, r, ne);
      rbits += (u64) ne);
    z->rp = op;  z->rbb = wb;  z->rbn = wn;  z->rbits += rbits;  z->pos = pos;
    if (!ok) return -1; }
  while (z->pos < z->gend && kq + 4 * nq < 576) {
    int q, nb, k = kq + 4 * nq;
    i64 p0 = z->pos;
    if (c1t) {
      if (z->pos + 4 > z->gend) break;
      q = (int) (~bits_at(z->m, (u64) z->pos, 4) & 15);  z->pos += 4;
    } else {
      int node = 0, nx = 0;
      u32 e = TFAST[QTREE][bits_at(z->m, (u64) z->pos, TPEEK)];
      if (e && z->pos + (e >> 8) <= z->gend) {
        z->pos += e >> 8;  nx = -(int) (e & 255) - 1;
      } else while (z->pos < z->gend) {
        nx = TREE[QTREE][2 * node + (int) bits_at(z->m, (u64) z->pos, 1)];
        z->pos++;
        if (nx <= 0) break;
        node = nx;
      }
      if (nx >= 0) { z->pos = p0;  break; }
      q = -nx - 1;
    }
    nb = pop4(k + 2 < 576 ? q : q & 0xC);
    if (z->pos + nb > z->gend) { z->pos = p0;  break; }
    if (z->sgn) z->sgp[np + nq] = (u8) bits_at(z->m, (u64) z->pos, nb);
    else rput(z, nb, bits_at(z->m, (u64) z->pos, nb));
    z->pos += nb;
    z->its[np + nq++] = (u8) q;
  }
  *nqp = nq;  *kqp = kq;
  return np;
}

static void fz_gran_enc(fz * z, int np, int nq, int kq, i64 p1) {
  mw * w = z->w;
  u8 * pm = z->pmb[w->ch] + 4;
  int i, prev = 0, ns = 0, c1t = V(w, F_C1T) != 0;
  i64 pe = z->pos;
  Fi(w->nsf,
    int b = w->sf[i].bits;
    if (!b || b > 8) continue;
    { int v = z->sv[ns++];
      fz_sym(z, ZSCF + (b - 1) * 4 + MIN(prev, 3), v);  prev = v; });
  fz_lanes(z, np, pm, 0);
  if (z->sgn) {
    int n = fz_pair_signs(z, np, w->ch, 0), j = 0;
    fz_signs(z, n, 0);
    memset(z->csg, 0, 580);
    Fi(np,
      int xy = HXY[TREE_OF[z->itt[i]]][z->its[i]], k = z->itk[i];
      int nx = (xy >> 4) != 0, ny = (xy & 15) != 0;
      z->csg[k] = (u8) ((1 + z->sgs[j]) & -nx);  j += nx;
      z->csg[k + 1] = (u8) ((1 + z->sgs[j]) & -ny);  j += ny);
  }
  Fi(nq, z->its[i] = z->its[np + i]);
  z->pos = p1;
  fz_quads(z, pm, kq, c1t, nq, 0);
  z->pos = pe;
  if (z->sgn) {
    int n = fz_quad_signs(z, np, nq, kq, w->ch, 0), j = 0, l;
    fz_signs(z, n, 0);
    Fi(nq,
      int q = z->its[i], k = kq + 4 * i;
      if (k + 4 > 576) q &= 0xC;
      for (l = 0; l < 4; l++) {
        int b = q >> (3 - l) & 1;
        z->csg[k + l] = (u8) ((1 + z->sgs[j]) & -b);  j += b;
      });
  }
  fz_gran_end(z, kq + 4 * nq);
}

static void fz_init(fz * z, int dec, u64 n, int sgn) {
  static const u32 TIER[5][3] = { { 16384, 32, 13 }, { 65536, 64, 13 },
                                  { 262144, 128, 13 }, { 1048576, 512, 14 },
                                  { 0, 1024, 14 } };
  memset(z, 0, sizeof *z);
  z->mode = dec;
  z->sgn = sgn;
  z->zmul = n < 65536 ? 0 : 27;
  { int t = 0;
    while (t < 4 && n >= TIER[t][0]) t++;
    z->rbk = TIER[t][1];  z->hv = TIER[t][2]; }
  z->w = xcalloc(1, sizeof *z->w);
  z->csg = z->signs[0];  z->psg[0] = z->signs[1];  z->psg[1] = z->signs[2];
  z->cb = z->lines[0];  z->pmb[0] = z->lines[1];  z->pmb[1] = z->lines[2];
}

static void fz_free(fz * z) {
  ar_free(&z->ar);
  xfree(z->ev);  xfree(z->rb);  xfree(z->w);
}

static void fz_main(fz * z, const mh * f, const u8 * side, u8 * m, i64 len) {
  mw * w = z->w;
  int i, j, k;
  (void) side;
  z->m = m;  z->pos = 0;
  z->op = m;  z->wb = 0;  z->wn = 0;
  Fi(f->lsf ? 1 : 2,
    Fj(f->nch,
      int raw;
      i64 start = z->pos;
      w->gr = i;  w->ch = j;
      build_entries(w);  build_scf(w);
      z->gend = MIN(start + V(w, F_P23), 8 * len);
      if (!z->mode) {
        u8 * r0;
        u64 b0 = z->rbb, n0 = z->rbits;
        int w0 = z->rbn, np, nq = 0, kq = 0;
        rroom(z);
        r0 = z->rp;
        np = fz_parse(z, &nq, &kq);
        raw = np < 0;
        if (raw) {
          z->rp = r0;  z->rbb = b0;  z->rbn = w0;  z->rbits = n0;
          z->pos = start;
        }
        fz_sym(z, ZFLAG, raw);
        if (!raw) {
          i64 pp = start;
          Fk(w->nsf, pp += w->sf[k].bits);
          Fk(np, pp += (PKT[z->itt[k]][z->its[k]] >> 5) & 63);
          fz_gran_enc(z, np, nq, kq, pp);
        }
      } else {
        raw = fz_sym(z, ZFLAG, 0);
        if (!raw) fz_gran_dec(z);
      }
      if (raw) {
        if (!z->mode) rroom(z);
        fz_rawrest(z);
        memset(z->pmb[j], 0, sizeof z->lines[0]);
        memset(z->psg[j], 0, sizeof z->signs[0]);
      }
      fz_settle(z);
      FATAL_UNLESS(!z->bad, "%s", z->mode ? "MP3 archive: corrupt granule"
                                          : "internal: MP3 granule parse")));
  z->gend = 8 * len;
  while (z->pos < z->gend) {
    if (!z->mode) rroom(z);
    fz_raw(z, (int) MIN(32, z->gend - z->pos));
  }
}

static INLINE void fz_byte(fz * z, u8 * p, int k, u8 * prev) {
  if (z->mode) *p = (u8) (fz_sym(z, k, 0) ^ (prev ? *prev : 0));
  else fz_sym(z, k, *p ^ (prev ? *prev : 0));
  if (prev) *prev = *p;
}

static void fz_bytes(fz * z, u8 * p, sz n, int k) {
  sz i;
  Fi(n,
    fz_byte(z, p + i, k, NULL);
    if ((i & 255) == 255) fz_settle(z));
  fz_settle(z);
}

static void fz_side(fz * z, const mh * f, u8 * side, const mlay * lay) {
  mw * w = z->w;
  u64 p = 0;
  w->hdr = *f;
  start_side(w);
  if (!z->mode) rroom(z);
  while (w->fi < w->nf) {
    sfield d = w->fl[w->fi];
    int pv = w->pval[d.gr][d.ch][d.id][d.sub], v;
    if (d.bits > 5) {
      int e = d.id == F_MDB ? (int) CLAMP(lay->pay - lay->end, 0, 511) : pv;
      int y;
      if (z->mode) {
        y = fz_sym(z, ZSIDE + d.id, 0);
        v = (y ? e + y - 128 : (int) rget(z, d.bits)) & ((1 << d.bits) - 1);
        bits_or(side, p, d.bits, (u32) v);
      } else {
        v = (int) bits_at(side, p, d.bits);
        y = v - e > -128 && v - e < 128 ? v - e + 128 : 0;
        fz_sym(z, ZSIDE + d.id, y);
        if (!y) rput(z, d.bits, (u32) v);
      }
    } else {
      int k = ZSMAL + d.id * 16 + MIN(pv, 15);
      if (z->mode) {
        v = fz_sym(z, k, 0) & ((1 << d.bits) - 1);
        bits_or(side, p, d.bits, (u32) v);
      } else { v = (int) bits_at(side, p, d.bits);  fz_sym(z, k, v); }
    }
    p += d.bits;
    w->fbit = d.bits - 1;  w->fval = (u32) v >> 1;
    side_bit(w, v & 1);
  }
}

static void fz_segment(fz * z, u8 * t, const mseg * sg) {
  sz o = 0, len = sg->len;
  u32 j;
  u8 prev[38];
  mlay lay = { 0, 0 };
  if (!sg->run) { fz_bytes(z, t, len, ZRAW);  return; }
  memset(prev, 0, sizeof prev);
  memset(z->lines, 0, sizeof z->lines);
  memset(z->signs, 0, sizeof z->signs);
  memset(z->w->pval, 0, sizeof z->w->pval);
  Fj(sg->nfr,
    mh f;
    int i, mdb, bits;
    i64 s, l, pe = lay.end;
    FATAL_UNLESS(o + 4 <= len, "MP3 archive: corrupt run");
    Fi(4, fz_byte(z, t + o + i, ZHDR + i, prev + i));
    FATAL_UNLESS(mh_parse(rd32be(t + o), sg->freeb, &f) &&
                 o + (sz) f.hbytes <= len, "MP3 archive: corrupt run");
    fz_side(z, &f, t + o + 4, &lay);
    if (f.crc) {
      u32 c = crc16(t + o, t + o + 4, f.sbytes);
      u8 cb[2];
      cb[0] = (u8) (c >> 8);  cb[1] = (u8) c;
      Fi(2, fz_byte(z, t + o + 4 + f.sbytes + i, ZHDR + 4 + i, cb + i));
    }
    side_sum(&f, t + o + 4, &mdb, &bits);
    lay_place(&lay, &f, mdb, bits, &s, &l);
    FATAL_UNLESS((sz) (s + l - pe) <= len - o - (sz) f.hbytes,
                 "MP3 archive: corrupt run");
    fz_settle(z);
    fz_bytes(z, t + o + (sz) f.hbytes, (sz) (s - pe), ZGAP);
    if (l)
      fz_main(z, &f, t + o + 4, t + o + (sz) f.hbytes + (sz) (s - pe), l);
    memcpy(z->w->pval, z->w->val, sizeof z->w->val);
    o += (sz) f.hbytes + (sz) (s + l - pe));
  fz_bytes(z, t + o, len - o, ZGAP);
}

static sz fz_finish(fz * z, u8 ** buf) {
  sz cap = 2 * z->nev + 16, i = z->nev;
  u8 * b = xmalloc(cap), * p = b + cap;
  u32 x[NLANE];
  int j;
  Fj(NLANE, x[j] = RSL);
  while (i--) {
    u32 e = z->ev[i], c = e & 0x7FFF, f = e >> 15 & 0x7FFF, * st = &x[e >> 30];
    if (*st >= f << 17) {
      p -= 2;  wr16le(p, *st);  *st >>= 16;
    }
    *st = ((*st / f) << RSB) + *st % f + c;
  }
  for (j = NLANE - 1; j >= 0; j--) { p -= 4;  wr32le(p, x[j]); }
  i = (sz) (b + cap - p);
  memmove(b, p, i);
  *buf = b;
  return i;
}

static sz put_var(u8 * p, u64 v) {
  sz n = 0;
  while (v >= 128) { p[n++] = (u8) (v | 128);  v >>= 7; }
  p[n++] = (u8) v;
  return n;
}

static u64 fget_var(FILE * f, const char * in) {
  u64 v = 0;
  int sh = 0;
  for (;;) {
    u8 c = 0;
    FATAL_UNLESS(sh < 64 && fread(&c, 1, 1, f) == 1, "%s: truncated archive", in);
    v |= (u64) (c & 127) << sh;
    if (!(c & 128)) return v;
    sh += 7;
  }
}

static void fget(FILE * f, void * b, sz n, const char * in) {
  FATAL_UNLESS(fread(b, 1, n, f) == n, "%s: truncated archive", in);
}

static void fput(FILE * f, const void * b, sz n, int * bad) {
  if (n && bf_fwrite(b, n, f) != n) *bad = 1;
}

static u64 fcopy(FILE * fi, FILE * fo, u64 lim, int * bad) {
  u8 * b = xmalloc(BLR_IO_CHUNK);
  u64 n = 0;
  sz k;
  while (!*bad && n < lim &&
         (k = fread(b, 1, (sz) MIN(lim - n, (u64) BLR_IO_CHUNK), fi)) > 0) {
    fput(fo, b, k, bad);  n += k;
  }
  if (ferror(fi)) *bad = 1;
  xfree(b);
  return n;
}

static FILE * open_in(const char * in) {
  FILE * f = fopen(in, "rb");
  if (!f) FATAL_CODE(BLR_EXIT_IO, "cannot open %s", in);
  return f;
}

static FILE * open_out(const char * out, FILE * fi) {
  FILE * f = bf_fopen(out, "wb");
  if (!f) {
    if (fi) fclose(fi);
    FATAL_CODE(BLR_EXIT_IO, "cannot create %s", out);
  }
  return f;
}

static u64 fcopy_file(const char * in, const char * out, const u8 * h,
                      sz head, u64 lim) {
  FILE * fi = open_in(in), * fo = open_out(out, fi);
  u64 n;
  int bad = 0;
  fput(fo, h, head, &bad);
  n = fcopy(fi, fo, lim, &bad);
  fclose(fi);
  if (bf_fclose(fo)) bad = 1;
  if (bad) FATAL_CODE(BLR_EXIT_IO, "I/O error copying %s to %s", in, out);
  return n;
}

int mp3_sniff(const u8 * b, sz n, int eof) {
  sz at = 0, lim;
  mseg r;
  if (n >= 10 && !memcmp(b, "ID3", 3) && b[3] < 0xFF && b[4] < 0xFF) return 1;
  lim = MIN(n, 65536);
  for (; at + 4 <= lim; at++)
    if (b[at] == 0xFF && scan_run(b, n, at, &r, eof)) return 1;
  return 0;
}

int mp3_is_archive(const char * path) {
  u8 b[ARC_HDRLEN];
  FILE * f = fopen(path, "rb");
  sz n;
  if (!f) return 0;
  n = fread(b, 1, sizeof b, f);
  fclose(f);
  return arc_is_mp3(b, n);
}

static int incompressible(const u8 * b, sz n) {
  sz c[256], i;
  i64 h = 0;
  if (n < 4096) return 0;
  memset(c, 0, sizeof c);
  Fi(n, c[b[i]]++);
  Fi(256, if (c[i]) h += (i64) c[i] * (ilog8((u64) n) - ilog8((u64) c[i])));
  return h >= (i64) n * 2022;
}

static mseg * segments(const u8 * b, sz n, sz lim, u8 * t, sz * nseg,
                       sz * used, int whole) {
  sz at = 0, raw = 0, ns = 0, cap = 16, i;
  mseg * s = xmalloc(cap * sizeof *s), r;
  u8 * chk = NULL;
  while (at < lim) {
    int ok;
    r.nfr = 0;
    ok = b[at] == 0xFF && scan_run(b, n, at, &r, whole && !ns);
    if (ok) {
      if (lim < n && at + r.len + MP_MAXFREE > n) run_cut(b, &r, lim);
      run_fwd(b + at, &r, t + at);
      chk = xrealloc(chk, r.len);
      ok = run_inv(t + at, &r, chk) && blr_same(chk, b + at, r.len);
    }
    if (!ok) { at += r.nfr >= MP_MINRUN ? r.len : 1;  continue; }
    s = xgrow(s, &cap, ns + 2, sizeof *s, 16);
    if (at > raw) {
      s[ns].at = raw;  s[ns].len = at - raw;  s[ns].run = 0;  s[ns].nfr = 0;
      s[ns].freeb = 0;  ns++;
    }
    s[ns++] = r;
    at += r.len;  raw = at;
  }
  if (at > raw) {
    s = xgrow(s, &cap, ns + 1, sizeof *s, 16);
    s[ns].at = raw;  s[ns].len = at - raw;  s[ns].run = 0;  s[ns].nfr = 0;
    s[ns].freeb = 0;  ns++;
  }
  xfree(chk);
  Fi(ns,
    if (!s[i].run && incompressible(b + s[i].at, s[i].len))
      s[i].run = SEG_STORED);
  *nseg = ns;  *used = at;
  return s;
}

static sz put_head(u8 * h, int eng, int cfg, u64 n) {
  memcpy(h, ARC_MAGIC, ARC_MAGLEN);
  h[ARC_MAGLEN] = ARC_VERSION;  h[ARC_MAGLEN + 1] = ARC_KMP3;
  h[ARC_MAGLEN + 2] = (u8) eng;  h[ARC_MAGLEN + 3] = (u8) cfg;
  wr64le(h + ARC_MAGLEN + 4, n);  wr32le(h + MPA_HDR, arc_sum_get());
  return MPA_MAX;
}

static i64 fsize(const char * path) {
  FILE * f = fopen(path, "rb");
  i64 n = -1;
  if (f) { if (!blr_fseek(f, 0, SEEK_END)) n = blr_ftell(f);  fclose(f); }
  FATAL_UNLESS(n >= 0, "cannot read %s", path);
  return n;
}

static void store(const char * in, const char * out) {
  u8 h[MPA_MAX];
  u64 n = (u64) fsize(in);
  sz hl = put_head(h, MPE_STORE, 0, n);
  FATAL_UNLESS(fcopy_file(in, out, h, hl, n) == n, "%s changed while read", in);
}

static char * temp_path(const char * out, int c) {
  sz n = strlen(out), dot = n, i;
  char * t = xmalloc(n + 5);
  int k;
  for (i = n; i > 0 && out[i - 1] != '/' && out[i - 1] != '\\' &&
              out[i - 1] != ':'; i--)
    if (out[i - 1] == '.') { dot = i - 1;  break; }
  memcpy(t, out, dot);
  for (k = -1; k < 10; k++) {
    FILE * f;
    sz j;
    int same = 1;
    if (k < 0) sprintf(t + dot, ".b%c", c);
    else sprintf(t + dot, ".b%c%d", c, k);
    if (strlen(t) != n) same = 0;
    for (j = dot; j < n && same; j++)
      same = (t[j] | 0x20) == (out[j] | 0x20);
    if (same) continue;
    if (!(f = fopen(t, "rb"))) break;
    fclose(f);
  }
  return t;
}

static void pack_alt(const char * in, const char * out, int c, int cfg) {
  char * t = temp_path(out, c);
  bf_doom_temp(t, 1);
  mp3_pack(in, t, cfg);
  if (fsize(t) < fsize(out)) fcopy_file(t, out, NULL, 0, ~(u64) 0);
  remove(t);  bf_doom_temp(t, 0);  xfree(t);
}

#define MP_JUNK 32
#define MP_MORE 64

static u64 run_bytes(const char * in) {
  sz win = mp3_window && mp3_window < MP_WIN ? mp3_window : MP_WIN;
  sz cap = win + MIN(win, MP_LOOK), fill = 0;
  u8 * b = xmalloc(cap);
  u64 cover = 0;
  int eof = 0, first = 1;
  FILE * f = open_in(in);
  for (;;) {
    sz at = 0, lim;
    mseg r;
    if (!eof) { fill += fread(b + fill, 1, cap - fill, f);  eof = fill < cap; }
    if (!fill) break;
    lim = eof ? fill : win;
    while (at < lim) {
      r.nfr = 0;
      if (b[at] == 0xFF && scan_run(b, fill, at, &r, eof && first && !cover)) {
        cover += r.len;  at += r.len;
      } else at += r.nfr >= MP_MINRUN ? r.len : 1;
    }
    first = 0;
    at = MIN(at, fill);
    fill -= at;  memmove(b, b + at, fill);
  }
  fclose(f);  xfree(b);
  return cover;
}

static int pack_z(const char * in, const char * out, int cfg) {
  const mp_cfg * c = &CFGS[cfg - MP_CFG0];
  FILE * fi = open_in(in), * fo;
  u8 h[MPA_MAX];
  i64 len;
  sz hl;
  long junk = 0;
  int rc;
  if (blr_fseek(fi, 0, SEEK_END) || (len = blr_ftell(fi)) < 0 ||
      blr_fseek(fi, 0, SEEK_SET)) {
    fclose(fi);  FATAL_CODE(BLR_EXIT_IO, "cannot read %s", in);
  }
  if (len > INT32_MAX) { fclose(fi);  return mp3_pack(in, out, MP_FALLBACK); }
  fo = open_out(out, fi);
  hl = put_head(h, MPE_Z, cfg - MP_CFG0, (u64) len);
  if (fwrite(h, 1, hl, fo) != hl) rc = 2;
  else rc = mp3z_pack_stream(fi, fo, c->cls, &junk);
  if (!rc && (blr_fseek(fo, 0, SEEK_END) || blr_ftell(fo) >= (i64) hl + len))
    rc = 1;
  fclose(fi);
  if (bf_fclose(fo) && !rc) rc = 2;
  if (rc == 2) FATAL_CODE(BLR_EXIT_IO, "write error on %s", out);
  if (rc) store(in, out);
  if (rc || (junk > len / MP_JUNK &&
             run_bytes(in) > (u64) (len - junk + len / MP_MORE)))
    pack_alt(in, out, 'f', MP_FALLBACK);
  return BLR_EXIT_OK;
}

static int unpack_z(const char * in, const char * out, u64 n, const mp_cfg * c,
                    sz hl) {
  FILE * fi = open_in(in), * fo = open_out(out, fi);
  int rc;
  i64 len;
  if (blr_fseek(fi, (i64) hl, SEEK_SET)) rc = 2;
  else {
    u8 zh[8];
    FATAL_UNLESS(fread(zh, 1, 8, fi) == 8 &&
                 !blr_fseek(fi, (i64) hl, SEEK_SET),
                 "%s: truncated archive", in);
    FATAL_UNLESS(rd32le(zh + 4) == n, "%s: corrupt MP3 run", in);
    rc = mp3z_unpack_stream(fi, fo, c->cls);
  }
  len = blr_ftell(fo);
  fclose(fi);
  if (bf_fclose(fo) && !rc) rc = 2;
  if (rc == 2) FATAL_CODE(BLR_EXIT_IO, "I/O error on %s", out);
  FATAL_UNLESS(!rc && len >= 0 && (u64) len == n, "%s: corrupt MP3 run", in);
  return BLR_EXIT_OK;
}

static int pack_choose(const char * in, const char * out, int cfg) {
  const mp_cfg * c = &CFGS[cfg - MP_CFG0];
  int alt = (u64) fsize(in) <= (u64) c->nm << 15;
  mp3_pack(in, out, MP_CFG0 + c->nb);
  if (alt) pack_alt(in, out, 'q', MP_CFG0 + c->ns);
  return BLR_EXIT_OK;
}

static int pack_walk(const char * in, const char * out, int cfg) {
  const mp_cfg * c = &CFGS[cfg - MP_CFG0];
  const int sym = c->eng == MP_SYM;
  sz win = mp3_window && mp3_window < MP_WIN ? mp3_window : MP_WIN;
  sz cap = win + MIN(win, MP_LOOK), fill = 0, hl;
  u8 * b = xmalloc(cap), * t = xcalloc(cap + 8, 1), h[MPA_MAX];
  u64 n = 0, n0 = 0;
  i64 size = -1;
  int bad = 0, eof = 0, first = 1;
  FILE * fi = open_in(in), * fo = open_out(out, fi);
  mcod m;
  fz z;
  mp_prepare(0);
  if (sym) fz_init(&z, 0, n0 = (u64) fsize(in), (int) (c->cls & 1));
  else mc_init(&m, 0);
  hl = put_head(h, sym ? MPE_SYM : MPE_WALK, cfg - MP_CFG0, 0);
  fput(fo, h, hl, &bad);
  for (;;) {
    sz ns, used, i, tl, cl;
    mseg * s;
    u8 * tab;
    if (!eof) { fill += fread(b + fill, 1, cap - fill, fi);  eof = fill < cap; }
    if (!fill || bad) break;
    s = segments(b, fill, eof ? fill : win, t, &ns, &used, eof && first);
    first = 0;
    tab = xmalloc(10 + ns * 30);
    tl = put_var(tab, ns);
    Fi(ns,
      tl += put_var(tab + tl, (u64) s[i].len << 2 | s[i].run);
      if (s[i].run == SEG_RUN) {
        tl += put_var(tab + tl, s[i].nfr);
        tl += put_var(tab + tl, s[i].freeb);
      });
    fput(fo, tab, tl, &bad);
    Fi(ns, if (s[i].run == SEG_STORED) fput(fo, b + s[i].at, s[i].len, &bad));
    if (sym) {
      u8 * rs;
      sz rl;
      z.nev = 0;  z.rr = 0;  z.rbb = 0;  z.rbn = 0;  z.rbits = 0;
      rroom(&z);
      z.rp = z.rb;
      Fi(ns,
        if (s[i].run != SEG_STORED)
          fz_segment(&z, (s[i].run ? t : b) + s[i].at, &s[i]));
      cl = fz_finish(&z, &rs);
      rroom(&z);
      st64be(z.rp, z.rbb);
      rl = (sz) ((z.rbits + 7) >> 3);
      tl = put_var(tab, cl);  fput(fo, tab, tl, &bad);
      fput(fo, rs, cl, &bad);
      tl = put_var(tab, rl);  fput(fo, tab, tl, &bad);
      fput(fo, z.rb, rl, &bad);
      xfree(rs);
    } else {
      rc_enc_init(&m.e);
      Fi(ns,
        if (s[i].run != SEG_STORED)
          mc_segment(&m, &s[i], (s[i].run ? t : b) + s[i].at, NULL));
      cl = rc_enc_finish(&m.e);
      tl = put_var(tab, cl);  fput(fo, tab, tl, &bad);
      fput(fo, rc_enc_data(&m.e), cl, &bad);
      rc_enc_free(&m.e);
    }
    xfree(tab);  xfree(s);
    n += used;  fill -= used;
    memmove(b, b + used, fill);
  }
  if (ferror(fi)) FATAL_CODE(BLR_EXIT_IO, "read error on %s", in);
  fclose(fi);
  if (sym) fz_free(&z);
  else mc_free(&m);
  xfree(b);  xfree(t);
  FATAL_UNLESS(!sym || n == n0, "%s changed while read", in);
  hl = put_head(h, sym ? MPE_SYM : MPE_WALK, cfg - MP_CFG0, n);
  if (!bad) size = blr_ftell(fo);
  if (!bad && blr_fseek(fo, 0, SEEK_SET)) bad = 1;
  fput(fo, h, hl, &bad);
  if (bf_fclose(fo)) bad = 1;
  if (bad) FATAL_CODE(BLR_EXIT_IO, "write error on %s", out);
  if (size < 0 || (u64) size >= hl + n) store(in, out);
  return BLR_EXIT_OK;
}

int mp3_pack(const char * in, const char * out, int cfg) {
  FATAL_UNLESS(mp3_config(cfg), "no MP3 configuration %d", cfg);
  if (CFGS[cfg - MP_CFG0].eng == MP_CHOOSE) return pack_choose(in, out, cfg);
  if (CFGS[cfg - MP_CFG0].eng == MP_Z) return pack_z(in, out, cfg);
  return pack_walk(in, out, cfg);
}

static int unpack_walk(FILE * fi, const char * in, const char * out, u64 n,
                       int eng, const mp_cfg * c) {
  const int sym = eng == MPE_SYM;
  sz cap = MP_WIN + MP_LOOK, scap = 0, ccap = 0;
  u8 * o = xmalloc(cap), * t = xmalloc(cap + 8), * cd = NULL, x;
  mseg * s = NULL;
  u64 pos = 0;
  i64 here = blr_ftell(fi), alen = -1;
  int bad = 0;
  FILE * fo;
  mcod m;
  fz z;
  if (here >= 0 && !blr_fseek(fi, 0, SEEK_END)) alen = blr_ftell(fi);
  FATAL_UNLESS(alen >= here && here >= 0 && !blr_fseek(fi, here, SEEK_SET),
               "cannot read %s", in);
  fo = open_out(out, NULL);
  mp_prepare(sym);
  if (sym) fz_init(&z, 1, n, (int) (c->cls & 1));
  else mc_init(&m, 1);
  while (pos < n && !bad) {
    sz ns = (sz) fget_var(fi, in), wl = 0, i;
    u64 cl;
    FATAL_UNLESS(ns >= 1 && ns <= cap, "%s: bad segment table", in);
    if (ns > scap) { scap = ns;  s = xrealloc(s, scap * sizeof *s); }
    Fi(ns,
      u64 v = fget_var(fi, in);
      s[i].run = (u32) (v & 3);  s[i].at = wl;
      FATAL_UNLESS(s[i].run <= SEG_STORED && (v >> 2) <= cap - wl,
                   "%s: bad segment table", in);
      s[i].len = (sz) (v >> 2);
      s[i].nfr = s[i].run == SEG_RUN ? (u32) fget_var(fi, in) : 0;
      s[i].freeb = s[i].run == SEG_RUN ? (u32) fget_var(fi, in) : 0;
      FATAL_UNLESS(s[i].nfr <= s[i].len, "%s: bad segment table", in);
      wl += s[i].len);
    FATAL_UNLESS(wl && wl <= n - pos, "%s: bad segment table", in);
    memset(t, 0, wl + 8);
    Fi(ns, if (s[i].run == SEG_STORED) fget(fi, o + s[i].at, s[i].len, in));
    cl = fget_var(fi, in);
    FATAL_UNLESS(cl <= (u64) (alen - blr_ftell(fi)),
                 "%s: truncated archive", in);
    if (cl + 2 > ccap) { ccap = (sz) cl + 2;  cd = xrealloc(cd, ccap); }
    fget(fi, cd, (sz) cl, in);
    if (sym) {
      u64 rl;
      FATAL_UNLESS(cl >= 4 * NLANE, "%s: truncated archive", in);
      cd[cl] = cd[cl + 1] = 0;
      z.p = cd + 4 * NLANE;  z.pe = cd + cl;  z.rr = 0;
      Fi(NLANE, z.x[i] = rd32le(cd + 4 * i));
      rl = fget_var(fi, in);
      FATAL_UNLESS(rl <= (u64) (alen - blr_ftell(fi)),
                   "%s: truncated archive", in);
      z.rb = xrealloc(z.rb, (sz) rl + 16);
      fget(fi, z.rb, (sz) rl, in);
      memset(z.rb + rl, 0, 16);
      z.rlen = (sz) rl;  z.rp = z.rb;  z.rbb = 0;  z.rbn = 0;  z.rbits = 0;
      Fi(ns,
        if (s[i].run == SEG_STORED) continue;
        fz_segment(&z, (s[i].run ? t : o) + s[i].at, &s[i]);
        FATAL_UNLESS(!s[i].run || run_inv(t + s[i].at, &s[i], o + s[i].at),
                     "%s: corrupt MP3 run", in));
      FATAL_UNLESS(z.rbits <= 8 * (u64) z.rlen, "%s: truncated archive", in);
    } else {
      rc_dec_init(&m.d, cd, (sz) cl);
      Fi(ns,
        if (s[i].run == SEG_STORED) continue;
        mc_segment(&m, &s[i], NULL, (s[i].run ? t : o) + s[i].at);
        FATAL_UNLESS(!s[i].run || run_inv(t + s[i].at, &s[i], o + s[i].at),
                     "%s: corrupt MP3 run", in));
      FATAL_UNLESS(!rc_dec_spent(&m.d), "%s: truncated archive", in);
    }
    fput(fo, o, wl, &bad);
    pos += wl;
  }
  FATAL_UNLESS(bad || fread(&x, 1, 1, fi) == 0, "%s: trailing bytes", in);
  if (sym) fz_free(&z);
  else mc_free(&m);
  xfree(s);  xfree(o);  xfree(t);  xfree(cd);
  if (bf_fclose(fo)) bad = 1;
  if (bad) FATAL_CODE(BLR_EXIT_IO, "write error on %s", out);
  return BLR_EXIT_OK;
}

int mp3_unpack(const char * in, const char * out) {
  FILE * fi = open_in(in);
  u8 h[MPA_MAX];
  u64 n;
  sz hl = MPA_MAX;
  int eng, cfg, r;
  if (fread(h, 1, MPA_MAX, fi) != MPA_MAX || !arc_is_mp3(h, MPA_MAX)) {
    fclose(fi);  FATAL("%s: not an MP3 archive", in);
  }
  eng = h[ARC_MAGLEN + 2];  cfg = h[ARC_MAGLEN + 3];
  n = rd64le(h + ARC_MAGLEN + 4);
  if (eng == MPE_STORE) {
    FILE * fo = open_out(out, fi);
    int bad = 0;
    u8 x;
    FATAL_UNLESS(fcopy(fi, fo, n, &bad) == n && fread(&x, 1, 1, fi) == 0,
                 "%s: stored length mismatch", in);
    fclose(fi);
    if (bf_fclose(fo)) bad = 1;
    if (bad) FATAL_CODE(BLR_EXIT_IO, "I/O error on %s", out);
    return BLR_EXIT_OK;
  }
  if (eng == MPE_Z) {
    fclose(fi);
    FATAL_UNLESS(cfg < NCFG && CFGS[cfg].eng == MP_Z,
                 "%s: unknown MP3 configuration %d", in, cfg);
    return unpack_z(in, out, n, &CFGS[cfg], hl);
  }
  FATAL_UNLESS((eng == MPE_WALK || eng == MPE_SYM) && cfg < NCFG &&
               CFGS[cfg].eng == (eng == MPE_SYM ? MP_SYM : MP_DIRECT),
               "%s: unknown MP3 engine %d/%d", in, eng, cfg);
  r = unpack_walk(fi, in, out, n, eng, &CFGS[cfg]);
  fclose(fi);
  return r;
}
