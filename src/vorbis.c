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

#include "vorbis.h"
#include "prof.h"

static void cm_alloc(vb_ctx * v);
static void ar_clear(vb_ctx * v, sz at, sz n);

/*  Value models.  An asterisk marks a field configured like its neighbour
    rather than on its own.  A plus marks depths raised to cover the field's
    full packet width. M_MULTW handles multiplicands whose residual exceeds
    the usual field.  */

static const mdl_cfg CFG[M_N] = {
  /*  depth  sgn shist freeze bank order        field  */
  {   3,     1,  1,    8,     0,   1 },  /*  channels                    +  */
  {   5,     1,  1,    8,     0,   1 },  /*  sample rate  */
  {   5,     1,  1,    8,     0,   1 },  /*  bitrate maximum (*)  */
  {   5,     1,  1,    8,     0,   1 },  /*  bitrate nominal  */
  {   5,     1,  1,    8,     0,   1 },  /*  bitrate minimum (*)  */
  {   3,     1,  1,    8,     0,   1 },  /*  block sizes byte  */
  {   3,     1,  1,    8,     0,   1 },  /*  framing byte  */
  {   3,     1,  1,    8,     1,   1 },  /*  codebook count  */
  {   4,     1,  2,    8,     0,   1 },  /*  codebook dimensions  */
  {   5,     0,  0,    32,    1,   0 },  /*  codebook entries  */
  {   3,     0,  0,    8,     0,   0 },  /*  ordered first length  */
  {   5,     0,  0,    8,     1,   0 },  /*  ordered run length          +  */
  {   3,     1,  1,    8,     1,   1 },  /*  codeword length  */
  {   2,     1,  1,    2,     1,   1 },  /*  lookup value bits  */
  {   3,     1,  1,    8,     0,   0 },  /*  multiplicand (*)  */
  {   3,     1,  1,    8,     0,   1 },  /*  time domain count (*)  */
  {   3,     1,  1,    8,     0,   1 },  /*  floor count  */
  {   3,     1,  1,    8,     0,   1 },  /*  floor 1 partitions  */
  {   2,     1,  1,    2,     1,   1 },  /*  floor 1 partition class  */
  {   3,     1,  1,    8,     0,   1 },  /*  floor 1 master book  */
  {   3,     1,  1,    8,     1,   0 },  /*  floor 1 subclass book  */
  {   4,     1,  1,    8,     0,   1 },  /*  floor 1 X value  */
  {   3,     1,  1,    8,     0,   1 },  /*  residue count  */
  {   5,     1,  1,    8,     0,   1 },  /*  residue begin (*)  */
  {   5,     1,  1,    8,     0,   1 },  /*  residue end                 +  */
  {   5,     1,  1,    1,     1,   1 },  /*  residue partition size      +  */
  {   3,     1,  1,    8,     0,   1 },  /*  residue classifications  */
  {   3,     1,  1,    8,     0,   1 },  /*  residue cascade  */
  {   4,     1,  1,    8,     1,   0 },  /*  residue book                +  */
  {   3,     1,  1,    8,     0,   1 },  /*  mapping count  */
  {   3,     1,  1,    8,     0,   1 },  /*  submap count (*)  */
  {   4,     1,  1,    8,     0,   1 },  /*  coupling steps              +  */
  {   3,     1,  1,    8,     0,   1 },  /*  coupling magnitude/angle    +  */
  {   4,     1,  1,    8,     0,   1 },  /*  channel multiplex (*)  */
  {   3,     1,  1,    8,     0,   1 },  /*  submap time (*)  */
  {   3,     1,  1,    8,     0,   1 },  /*  submap floor  */
  {   3,     1,  1,    8,     0,   1 },  /*  submap residue  */
  {   3,     1,  1,    8,     0,   1 },  /*  mode count  */
  {   3,     1,  1,    8,     0,   1 },  /*  mode mapping  */
  {   5,     1,  1,    32,    0,   1 },  /*  float mantissa  */
  {   4,     1,  1,    2,     0,   1 },  /*  float exponent, minimum  */
  {   4,     1,  1,    2,     0,   1 },  /*  float exponent, delta  */
  {   5,     1,  1,    8,     0,   0 },  /*  wide multiplicand  */
  {   5,     1,  1,   32,     0,   1 }   /*  floor classword correction  */
};

void vb_init(vb_ctx * v) {
  sz i;
  Fi(M_N, mdl_init(v->m + i, CFG + i));
  rc_probs_init(v->f, F_NSLOT);
  v->cmt = xmalloc(VB_CBANK * VB_CSIZE * sizeof *v->cmt);
  v->cmtc = NULL;
  memset(v->fc, 0, sizeof v->fc);  memset(v->amc, 0, sizeof v->amc);
  memset(v->awc, 0, sizeof v->awc);
  rc_probs_init(v->cmt, VB_CBANK * VB_CSIZE);
  v->pb = 0;  memset(&v->i, 0, sizeof v->i);
  /*  Ordinary packets use type zero; other values describe extra syntax.  */
  rc_probs_init(v->am, VB_MBANK * VB_MSTEP);
  rc_probs_init(&v->aw[0][0], 4);
  mdl_init(&v->apt, CFG + M_CH);
  v->pm = v->pw[0] = v->pw[1] = 0;
  /*  Allocate the large payload arena only when audio begins.  */
  v->su = NULL;  v->nsu = v->csu = 0;  v->cur = NULL;
  memset(v->sl, 0, sizeof v->sl);  memset(v->st, 0, sizeof v->st);
  v->nsl = 0;  v->ns = VB_NSLOT;
  v->ar = NULL;  v->arn = 0;  v->mem = NULL;
  memset(&v->arena, 0, sizeof v->arena);
  v->clsm = NULL;
  memset(v->ab, 0, sizeof v->ab);  v->aglob = v->astep = 0;
  memset(&v->cm, 0, sizeof v->cm);  v->cm_mask = 0;  memset(v->nv0, 0, sizeof v->nv0);
  memset(v->nv1, 0, sizeof v->nv1);  memset(v->nidx, 0, sizeof v->nidx);
  memset(v->nrun, 0, sizeof v->nrun);
  v->nxv = 0;  v->npch = -1;  v->nstarted = 0;
  v->symbols = NULL;  v->nsymbols = v->csymbols = 0;  v->symfull = 0;
  v->ys = NULL;  v->ysn = 0;  v->cs = NULL;  v->csn = 0;
  memset(v->fpl, 0, sizeof v->fpl);
  v->hu = 0;  v->psl = 0;
  memset(v->fh, 0, 4);  memset(v->sh, 0, 4);  memset(v->mh, 0, 4);
  vb_tune_default(&v->t);
}

void vb_tune_default(vb_tune * t) {
  t->alim = RC_ALIM;  t->lr = 7;  t->flags = 0;
}

void vb_tune_set(vb_ctx * v, const vb_tune * t) {
  FATAL_UNLESS(!v->cm.live, "vorbis: CM is already built");
  v->t = *t;
  if (!v->t.alim) v->t.alim = 1;
  if (!v->t.lr) v->t.lr = 1;
  if (v->t.lr > 31) v->t.lr = 31;
}

void vb_tune_put(const vb_tune * t, u8 * p) {
  p[0] = t->alim;  p[1] = t->lr;  p[2] = t->flags;
}

void vb_tune_get(vb_tune * t, const u8 * p, sz n) {
  vb_tune_default(t);
  if (n >= 1) t->alim = p[0] ? p[0] : 1;
  if (n >= 2) t->lr = p[1] ? p[1] : 1;
  if (n >= 3) t->flags = p[2];
}

static void bk_free(vb_book * b) {
  free(b->fast);  free(b->fastbits);
  free(b->len);  free(b->code);  free(b->nd);  free(b->mult);  free(b->inv);
}

void vb_free(vb_ctx * v) {
  sz i, j;
  Fi(M_N, mdl_free(v->m + i));
  mdl_free(&v->apt);
  free(v->cmt);  v->cmt = NULL;
  free(v->cmtc);  v->cmtc = NULL;
  cm_free(&v->cm);
  Fi(v->nsu,
    Fj(v->su[i]->nbk, bk_free(v->su[i]->bk + j));
    free(v->su[i]->bk);  free(v->su[i]->fl);  free(v->su[i]->rs);
    free(v->su[i]->mp);  free(v->su[i]));
  free(v->su);  v->su = NULL;  v->nsu = 0;  v->cur = NULL;
  Fi(VB_MAXSLOT, free(v->sl[i].len));
  memset(v->sl, 0, sizeof v->sl);  v->nsl = 0;
  bm_free(&v->arena);  v->ar = NULL;
  free(v->mem);  v->mem = NULL;
  free(v->clsm);  v->clsm = NULL;
  free(v->symbols);  v->symbols = NULL;
  free(v->ys);  v->ys = NULL;  free(v->cs);  v->cs = NULL;
  v->ysn = v->csn = 0;
}

void vb_level(vb_ctx * v, int lev) {
  FATAL_UNLESS(lev >= 0 && lev < CM_NLEV, "vorbis: effort %d is out of range", lev);
  FATAL_UNLESS(!v->ar, "vorbis: the arena is already in use");
  /*  Header packets need these tables before the audio arena exists.  */
  rc_adapt_init();
  if (!v->cmtc) v->cmtc = xcalloc(VB_CBANK * VB_CSIZE, 1);
  v->cm_mask = CM_LEVMASK[lev];
  if (v->cm_mask) cm_alloc(v);
}

void vb_slots(vb_ctx * v, u32 n) {
  FATAL_UNLESS(n >= 16 && n <= VB_MAXSLOT && !(n & (n - 1)),
               "vorbis: %lu is not a codebook pool size", (unsigned long) n);
  FATAL_UNLESS(!v->ar, "vorbis: the pool is already in use");
  v->ns = n;
}

void vb_use(vb_ctx * v, u32 n) {
  FATAL_UNLESS(n < v->nsu, "vorbis: no setup %lu to reuse", (unsigned long) n);
  v->csu = n;  v->cur = v->su[n];
}

/*  Link boundaries retain models but reset packet history.  */
void vb_link(vb_ctx * v) {
  v->pm = v->pw[0] = v->pw[1] = 0;
  /*  Retain probabilities while resetting payload history.  */
  v->hu = 0;  v->psl = 0;
  memset(v->fh, 0, 4);  memset(v->sh, 0, 4);  memset(v->mh, 0, 4);
  memset(v->fpl, 0, sizeof v->fpl);
  memset(v->st, 0, sizeof v->st);
  if (v->mem) memset(v->mem, 0, VB_MEMSZ);
  if (v->clsm) memset(v->clsm, 0, VB_CLSMSZ);
}

void vb_reset(vb_ctx * v) {
  sz i;
  Fi(M_N, rc_probs_init(v->m[i].p, v->m[i].n));
  rc_probs_init(v->apt.p, v->apt.n);
  rc_probs_init(v->f, F_NSLOT);
  rc_probs_init(v->cmt, VB_CBANK * VB_CSIZE);
  rc_probs_init(v->am, VB_MBANK * VB_MSTEP);
  rc_probs_init(&v->aw[0][0], 4);
  memset(v->fc, 0, sizeof v->fc);  memset(v->amc, 0, sizeof v->amc);
  memset(v->awc, 0, sizeof v->awc);
  if (v->cmtc) memset(v->cmtc, 0, VB_CBANK * VB_CSIZE);
  if (v->ar) {
    /*  AR_LIVE treats zero as pristine.  */
    ar_clear(v, 0, v->aglob);
    memset(v->ai, 0, sizeof v->ai);
    /* Slot regions are cleared on their next use. */
  }
  Fi(VB_MAXSLOT, free(v->sl[i].len));
  memset(v->sl, 0, sizeof v->sl);  memset(v->st, 0, sizeof v->st);
  v->nsl = 0;  v->pb = 0;
}

void vb_endlink(vb_ctx * v) {
  sz i;
  Fi(v->nsl, if (!v->st[i] && v->sl[i].use) v->sl[i].use--);
  memset(v->st, 0, sizeof v->st);
}

static const u8 RST[] = {
  M_DIM, M_ENT, M_ORDF, M_ORDR, M_LEN, M_VBITS, M_MULT,
  M_PCLS, M_MBOOK, M_SBOOK, M_FX,
  M_MULTW                     /*  a codebook owns this one too  */
};

#define S_BULK  0                 /*  setup headers, and the audio payload  */
#define S_MODE  1                 /*  page headers, and the packet modes  */
#define S_TYPE  2                 /*  one packet-type value per packet  */

typedef struct {
  int enc;
  int replay;  sz symbol;
  int probe, rawpkt, choices; /*  syntax-only traversal, peeling, floor choices  */
  rc_enc * e[3];
  rc_dec * d[3];
  vb_ctx * v;
  u8 * b;
  sz len, pos;                /*  the packet, and a cursor in BITS  */
} io;

/*  One bit at one slot that carries an observation count: the encoder's bit
    comes back unchanged, the decoder's comes off the stream.  */
static INLINE int cbit(io * z, int s, u16 * p, u8 * c, int b) {
  if (!z->enc) return rc_dec_bit_ad(z->d[s], p, c, RC_ALIM);
  rc_enc_bit_ad(z->e[s], p, c, RC_ALIM, b);  return b;
}

/*  Vorbis packs the least significant bit first inside each byte.  */
static u32 bget(io * z, int n) {
  u32 r = 0;
  int i;
  if (z->probe && z->pos + (sz) n > z->len * 8) {
    z->rawpkt = 1;  return 0;
  }
  FATAL_IF_HOT(z->pos + (sz) n > z->len * 8)("vorbis: packet ends inside a field");
  Fi(n,
    r |= (u32) (z->b[z->pos >> 3] >> (z->pos & 7) & 1) << i;
    z->pos++);
  return r;
}

static void bput(io * z, int n, u32 v) {
  int i;
  FATAL_IF_HOT(z->pos + (sz) n > z->len * 8)("vorbis: packet ends inside a field");
  Fi(n,
    z->b[z->pos >> 3] |= (u8) ((v >> i & 1) << (z->pos & 7));
    z->pos++);
}

/*  One-bit codeword steps.  */
static INLINE u32 bget1(io * z) {
  u32 r;
  FATAL_IF_HOT(z->pos >= z->len * 8)("vorbis: packet ends inside a codeword");
  r = z->b[z->pos >> 3] >> (z->pos & 7) & 1;
  z->pos++;
  return r;
}

static INLINE void bput1(io * z, u32 b) {
  FATAL_IF_HOT(z->pos >= z->len * 8)("vorbis: packet ends inside a codeword");
  z->b[z->pos >> 3] |= (u8) (b << (z->pos & 7));
  z->pos++;
}

/*  A value through model block `k`, against an explicit prediction (0 for
    the blocks that carry their own first-order one).  */
static u32 mv(io * z, int s, int k, u32 v, u32 p) {
  if (z->enc) { mdl_enc(z->v->m + k, z->e[s], v - p);  return v; }
  return mdl_dec(z->v->m + k, z->d[s]) + p;
}

/*  A value through the bit tree of depth `d` rooted at `s[1]`.  */
static u32 tv(io * z, int st, u16 * s, u8 * c, int d, u32 v) {
  u32 idx = 1;
  int i;
  FATAL_UNLESS(!z->enc || v < 1UL << d, "vorbis: %lu does not fit a %d-bit "
               "tree", (unsigned long) v, d);
  for (i = d - 1; i >= 0; i--)
    idx = idx * 2 + (u32) cbit(z, st, s + idx, c + idx, (int) (v >> i & 1));
  return idx - (1UL << d);
}

/*  Code an n-bit field and reject overflow.  */
static u32 fld(io * z, int k, int n, u32 p) {
  u32 v = z->enc ? bget(z, n) : 0;
  v = mv(z, S_BULK, k, v, p);
  if (!z->enc) {
#ifndef BLR_NO_FLD                    /*  fuzzing may define this to reach further  */
    FATAL_UNLESS(n >= 32 || !(v >> n),
                 "vorbis: field %d value %lu exceeds %d bits",
                 k, (unsigned long) v, n);
#endif
    bput(z, n, v);
  }
  return v;
}

/*  Code `n` packet bits through a depth-`d` tree.  */
static u32 tfld(io * z, int k, int d, int n) {
  u32 v = z->enc ? bget(z, n) : 0;
  v = tv(z, S_BULK, z->v->f + k, z->v->fc + k, d, v);
  if (!z->enc) bput(z, n, v);
  return v;
}

/*  Check `n` fixed packet bits.  */
static void cst(io * z, int n, u32 k) {
  if (z->enc) {
    u32 v = bget(z, n);
    FATAL_UNLESS(v == k, "vorbis: fixed field %lu, expected %lu",
                 (unsigned long) v, (unsigned long) k);
  } else bput(z, n, k);
}

/*  Largest v where (v + 1)^dim <= entries.  */
static u32 lk1(u32 entries, u32 dim) {
  u32 v = 0, i, p;
  for (;;) {
    p = 1;
    Fi(dim,
      if (p > entries / (v + 1)) { p = entries + 1;  break; }
      p *= v + 1);
    if (p > entries) return v;
    v++;
  }
}

/*  The audio layer retains parsed codebooks to translate floor and residue
    symbols in both directions.  */

/*  Vorbis `_make_words` codeword assignment, including sparse length lists.  */
static void bk_words(vb_book * b) {
  u32 mk[33], i, j, e, l;
  memset(mk, 0, sizeof mk);
  Fi(b->ent,
    l = b->len[i];
    if (!l) continue;
    e = mk[l];
    FATAL_UNLESS(l >= 32 || !(e >> l), "vorbis: over-populated codebook");
    b->code[i] = e;
    for (j = l; j > 0; j--) {
      if (mk[j] & 1) { mk[j] = j == 1 ? mk[1] + 1 : mk[j - 1] << 1;  break; }
      mk[j]++;
    }
    for (j = l + 1; j < 33; j++) {
      if (mk[j] >> 1 != e) break;
      e = mk[j];  mk[j] = mk[j - 1] << 1;
    });
}

/*  ... and the tree that reads them back.  A child of 0 is absent, a
    negative child is the leaf -(entry + 1); a tree over `u` used entries has
    at most `u` interior nodes.  */
static void bk_tree(vb_book * b) {
  u32 i, k, n = 1, u = 0, idx, cap;
  i32 t;
  Fi(b->ent, if (b->len[i]) u++);
  cap = u + 2;
  b->nd = xmalloc(2 * cap * sizeof *b->nd);
  memset(b->nd, 0, 2 * cap * sizeof *b->nd);
  Fi(b->ent,
    if (!b->len[i]) continue;
    idx = 0;
    for (k = b->len[i]; k > 1; k--) {
      u32 c = 2 * idx + (b->code[i] >> (k - 1) & 1);
      t = b->nd[c];
      if (!t) { FATAL_UNLESS(n < cap, "vorbis: codebook tree overflows");
                t = (i32) n++;  b->nd[c] = t; }
      FATAL_UNLESS(t > 0, "vorbis: codebook is not prefix free");
      idx = (u32) t;
    }
    idx = 2 * idx + (b->code[i] & 1);
    FATAL_UNLESS(!b->nd[idx], "vorbis: codebook is not prefix free");
    b->nd[idx] = -(i32) (i + 1));
}

/*  A lookup-1 entry is a base-`nv` list of multiplicand indices. Code centered
    values and use `inv` to recover each index before recomposition.  */
static void bk_look(vb_book * b) {
  u32 i, mx = 0, np;
  /*  For t < 2^24, rounding this reciprocal up adds less than 1/nv to
      t/nv. The integer quotient is exact, including nv == 1.  */
  b->divshift = 24 + blr_ilog(b->nv - 1);
  b->divmul = (u32) ((((uint64_t) 1 << b->divshift) + b->nv - 1) / b->nv);
  Fi(b->nv, if (b->mult[i] > mx) mx = b->mult[i]);
  b->base = mx + 1;  b->off = b->base >> 1;
  b->inv = xmalloc(b->base * sizeof *b->inv);
  Fi(b->base, b->inv[i] = (u32) -1);
  /*  Require unique multiplicands and enough digit tuples for every entry.  */
  Fi(b->nv,
    FATAL_UNLESS(b->inv[b->mult[i]] == (u32) -1,
                 "vorbis: duplicate codebook multiplicand %lu",
                 (unsigned long) b->mult[i]);
    b->inv[b->mult[i]] = i);
  /*  Count tuples, guarding the nv == 1 case.  */
  np = 1;
  if (b->nv > 1) Fi(b->dim, np *= b->nv);
  Fi(b->ent, if (b->len[i] && i >= np)
               FATAL("vorbis: codebook entry %lu exceeds lookup grid %lu",
                     (unsigned long) i,
                     (unsigned long) np));
}

/*  Assign each codebook the nearest compatible model slot, creating one when
    needed and available.  */

static u32 pool_dist(const vb_slot * s, const u8 * len, u32 n) {
  u32 i, d = 0;
  Fi(n, d += s->len[i] > len[i] ? s->len[i] - len[i] : len[i] - s->len[i]);
  return d;
}

static void pool_take(vb_ctx * v, u32 k, vb_book * b, const u8 * len) {
  vb_slot * s = v->sl + k;
  if (s->cap < b->ent) { free(s->len);  s->len = xmalloc(b->ent);  s->cap = b->ent; }
  memcpy(s->len, len, b->ent);
  s->ent = b->ent;  s->nv = b->nv;  s->dm = b->dm;  s->de = b->de;  s->ds = b->ds;
}

/*  Represent sparse holes as 0xFF.  */
static u8 * pool_cmp(vb_book * b, u8 * buf) {
  u32 i, n = 0;
  Fi(b->ent, if (b->len[i]) n++);
  if (n == b->ent) return b->len;
  Fi(b->ent, buf[i] = b->len[i] ? b->len[i] : 0xFF);
  return buf;
}

static u32 pool_slot(vb_ctx * v, vb_book * b) {
  u32 i, k, best = 0, bd = 0xFFFFFFFFUL, d, sum = 0, nrm;
  u8 * len, * tmp = xmalloc(b->ent);
  len = pool_cmp(b, tmp);
  Fi(v->nsl,
    if (v->sl[i].ent != b->ent || v->sl[i].nv != b->nv ||
        v->sl[i].dm != b->dm || v->sl[i].de != b->de || v->sl[i].ds != b->ds)
      continue;
    d = pool_dist(v->sl + i, len, b->ent);
    if (d < bd) { bd = d;  best = i;  if (!d) break; });
  /*  Allow one length unit of average distance per used entry.  */
  /*  nu == 0 is legal (every length a hole) and would trap.  Two such books
      are interchangeable, so a zero distance still matches.  */
  { u32 nu = 0;  Fi(b->ent, if (b->len[i]) nu++);
    nrm = bd >= 0x1000000UL ? 0xFFFFFFFFUL
        : nu               ? (bd << 8) / nu
        : bd               ? 0xFFFFFFFFUL : 0; }
  if (nrm <= 0x100) {
    v->sl[best].use++;  v->st[best] = 1;  free(tmp);  return best;
  }
  if (v->nsl < v->ns) {
    k = v->nsl++;  pool_take(v, k, b, len);  v->st[k] = 1;  free(tmp);  return k;
  }
  /*  If no slot is close, evict the least-used slot and reset it.  */
  k = best;
  if (nrm > 0xFFE) {
    u32 lo = 0xFFFFFFFFUL;
    k = 0;
    Fi(v->nsl, if (v->sl[i].use < lo) { lo = v->sl[i].use;  k = i; });
    /*  Re-seed only after eviction, not after a nearest-slot reuse.  */
    v->ai[k] = 0;
  }
  Fi(v->nsl, if (i != k) sum += v->sl[i].use);
  for (i = v->ns, d = 0; i > 1; i >>= 1) d++;
  v->sl[k].use = sum >> d;
  pool_take(v, k, b, len);
  v->st[k] = 1;  free(tmp);
  return k;
}


/*  Codeword lengths form one first-order chain across the setup. Sparse books
    also code use flags, while ordered books code run lengths.  */

static void lengths(io * z, vb_book * k) {
  u32 i, got, cur, n, ent = k->ent;
  int used, prev;
  if (tfld(z, F_ORD, 1, 1)) {
    got = 0;  cur = fld(z, M_ORDF, 5, 0) + 1;
    while (got < ent) {
      n = fld(z, M_ORDR, (int) blr_ilog(ent - got), 0);
      FATAL_UNLESS(n <= ent - got, "vorbis: ordered run overruns its book");
      FATAL_UNLESS(cur <= 32, "vorbis: ordered lengths run past 32");
      for (i = got; i < got + n; i++) k->len[i] = (u8) cur;
      got += n;  cur++;
    }
    return;
  }
  if (!tfld(z, F_SPARSE, 1, 1)) {
    Fi(ent, k->len[i] = (u8) (fld(z, M_LEN, 5, 0) + 1));
    return;
  }
  used = (int) tfld(z, F_USE0, 1, 1);
  Fi(ent,
    if (i) { prev = used;  used = (int) tfld(z, prev ? F_USED : F_USEU, 1, 1); }
    if (used) k->len[i] = (u8) (fld(z, M_LEN, 5, 0) + 1));
}

/*  Vorbis float32 has a sign, 10-bit exponent, and 21-bit mantissa. The two
    mantissas share one model.  */
static u32 flt(io * z, int expblk, int sgn) {
  u32 w = z->enc ? bget(z, 32) : 0, s, e, m;
  s = tv(z, S_BULK, z->v->f + sgn, z->v->fc + sgn, 1,
         z->enc ? w >> 31 : 0);
  e = mv(z, S_BULK, expblk, z->enc ? w >> 21 & 0x3FF : 0, 0);
  m = mv(z, S_BULK, M_FMANT, z->enc ? w & 0x1FFFFF : 0, 0);
  w = (s << 31) | ((e & 0x3FF) << 21) | (m & 0x1FFFFF);
  if (!z->enc) bput(z, 32, w);
  return w;
}

/*  Normalize a Vorbis float32 for numeric comparison.  */
static void f32(vb_book * b, u32 w) {
  b->dm = w & 0x1FFFFF;  b->de = (int) (w >> 21 & 0x3FF) - 788;
  b->ds = (int) (w >> 31);
  if (!b->dm) { b->de = 0;  b->ds = 0;  return; }
  while (!(b->dm & 1)) { b->dm >>= 1;  b->de++; }
}

static void codebooks(io * z) {
  vb_setup * s = z->v->cur;
  u32 nb, j, lk, vb, i;
  int mk;
  vb_book * k;
  nb = fld(z, M_NBOOK, 8, 0) + 1;
  s->bk = xmalloc(nb * sizeof *s->bk);  s->nbk = nb;
  memset(s->bk, 0, nb * sizeof *s->bk);
  Fj(nb,
    k = s->bk + j;
    cst(z, 24, 0x564342);
    k->dim = fld(z, M_DIM, 16, 0);
    k->ent = fld(z, M_ENT, 24, 0);
    /*  ent == 0 is accepted by libvorbis; dim == 0 divides by zero.  */
    FATAL_UNLESS(k->dim > 0, "vorbis: codebook %lu has dimension 0",
                 (unsigned long) j);
    FATAL_UNLESS(k->ent <= 1UL << 20, "vorbis: codebook %lu has %lu entries",
                 (unsigned long) j, (unsigned long) k->ent);
    /*  Enforce libvorbis's joint dimension and entry bound.  */
    FATAL_UNLESS(blr_ilog(k->dim) + blr_ilog(k->ent) <= 24,
                 "vorbis: codebook %lu size %lu x %lu is unsupported",
                 (unsigned long) j, (unsigned long) k->dim,
                 (unsigned long) k->ent);
    k->len = xmalloc(k->ent);  memset(k->len, 0, k->ent);
    k->code = xmalloc(k->ent * sizeof *k->code);
    lengths(z, k);
    bk_words(k);  bk_tree(k);
    lk = tfld(z, F_LOOK, 2, 4);
    k->look = (u8) lk;
    if (!lk) continue;
    FATAL_UNLESS(lk == 1, "vorbis: unsupported codebook lookup type %lu",
                 (unsigned long) lk);
    flt(z, M_FEXP0, F_MINSG);  f32(k, flt(z, M_FEXP1, F_DLTSG));
    vb = fld(z, M_VBITS, 4, 0) + 1;
    tfld(z, F_SEQ, 1, 1);
    k->nv = lk1(k->ent, k->dim);
    k->mult = xmalloc((k->nv ? k->nv : 1) * sizeof *k->mult);
    /*  Predict libvorbis's centered zigzag multiplicands. Use M_MULTW when a
        value can exceed M_MULT's residual range.  */
    mk = vb > 8 || k->nv > 0x100 ? M_MULTW : M_MULT;
    Fi(k->nv, k->mult[i] = fld(z, mk, (int) vb,
                               i & 1 ? (k->nv >> 1) - (i + 1) / 2
                                       : (k->nv >> 1) + i / 2));
    FATAL_UNLESS(k->nv > 0, "vorbis: codebook %lu has an empty lookup table",
                 (unsigned long) j);
    bk_look(k);
    k->slot = pool_slot(z->v, k));
}


/*  Sort Floor 1 posts by X.  */
static void fl_sort(vb_floor * f) {
  u32 i, j, t;
  Fi(f->posts,
    f->srt[i] = (u8) i;
    for (j = i; j > 0 && f->x[f->srt[j - 1]] > f->x[f->srt[j]]; j--) {
      t = f->srt[j];  f->srt[j] = f->srt[j - 1];  f->srt[j - 1] = (u8) t;
    });
  Fi(f->posts, if (i) FATAL_UNLESS(f->x[f->srt[i]] != f->x[f->srt[i - 1]],
                                   "vorbis: floor X list repeats a value"));
}

static const u32 QUANT[4] = { 256, 128, 86, 64 };

static void floors(io * z) {
  vb_setup * s = z->v->cur;
  u32 nt, nf, k, i, j, nc, sub;
  vb_floor * q;
  nt = fld(z, M_NTIME, 6, 0) + 1;
  Fi(nt, cst(z, 16, 0));                      /*  the time domain list  */
  nf = fld(z, M_NFLOOR, 6, 0) + 1;
  s->fl = xmalloc(nf * sizeof *s->fl);  s->nfl = nf;
  memset(s->fl, 0, nf * sizeof *s->fl);
  Fk(nf,
    q = s->fl + k;
    FATAL_UNLESS(tfld(z, F_FLTYPE, 1, 16) == 1,
                 "vorbis: floor type 0 is unsupported");
    q->parts = fld(z, M_PART, 5, 0);
    FATAL_UNLESS(q->parts <= VB_MAXPART, "vorbis: floor has %lu partitions",
                 (unsigned long) q->parts);
    nc = 0;
    Fi(q->parts,
      q->pcls[i] = (u8) fld(z, M_PCLS, 4, 0);
      FATAL_UNLESS(q->pcls[i] < VB_MAXCLASS, "vorbis: floor class %lu",
                   (unsigned long) q->pcls[i]);
      if ((u32) q->pcls[i] + 1 > nc) nc = q->pcls[i] + 1);
    Fi(nc,
      q->cdim[i] = (u8) (tfld(z, F_CDIM, 3, 3) + 1);
      sub = tfld(z, F_CSUB, 2, 2);
      q->csub[i] = (u8) sub;  q->cbook[i] = -1;
      if (sub) q->cbook[i] = (i32) fld(z, M_MBOOK, 8, 0);
      Fj(1UL << sub,
        z->v->pb = fld(z, M_SBOOK, 8, z->v->pb);
        q->csb[i][j] = (i32) z->v->pb - 1));
    q->mult = tfld(z, F_FMUL, 2, 2) + 1;
    q->quant = QUANT[q->mult - 1];
    q->rng = tfld(z, F_RANGE, 4, 4);
    q->x[0] = 0;  q->x[1] = 1UL << q->rng;  q->posts = 2;
    Fi(q->parts, Fj(q->cdim[q->pcls[i]],
      FATAL_UNLESS(q->posts < VB_MAXPOST, "vorbis: floor has over %d posts",
                   VB_MAXPOST);
      q->x[q->posts++] = fld(z, M_FX, (int) q->rng, 0)));
    fl_sort(q));
}

/*  Floor and residue book numbers share a first-order chain. Residue book
    lists continue from their class book with second-order prediction.  */
static void residues(io * z) {
  vb_setup * s = z->v->cur;
  u32 nr, k, i, j, cb, m0, m1, v, c;
  vb_res * q;
  nr = fld(z, M_NRES, 6, 0) + 1;
  s->rs = xmalloc(nr * sizeof *s->rs);  s->nrs = nr;
  memset(s->rs, 0, nr * sizeof *s->rs);
  Fk(nr,
    q = s->rs + k;
    q->type = tfld(z, F_RSTYPE, 2, 16);
    FATAL_UNLESS(q->type <= 2, "vorbis: residue type above 2");
    q->beg = fld(z, M_RBEG, 24, 0);  q->end = fld(z, M_REND, 24, 0);
    q->psz = fld(z, M_RPSZ, 24, 0) + 1;
    q->ncl = fld(z, M_RNCL, 6, 0) + 1;
    FATAL_UNLESS(q->ncl <= VB_MAXRCL, "vorbis: residue has %lu classifications",
                 (unsigned long) q->ncl);
    cb = fld(z, M_RBOOK, 8, z->v->pb);
    q->cbook = cb;
    m1 = cb - z->v->pb;  m0 = cb;  z->v->pb = cb;
    Fi(q->ncl,
      /*  One byte, split in the packet across a 3-bit low half and an
          optional 5-bit high half; coded whole.  */
      c = 0;
      if (z->enc) { c = bget(z, 3);  if (bget(z, 1)) c |= bget(z, 5) << 3; }
      c = mv(z, S_BULK, M_CASC, c, 0);
      FATAL_UNLESS(c < 256, "vorbis: residue cascade %lu", (unsigned long) c);
      q->casc[i] = (u8) c;
      if (!z->enc) {
        bput(z, 3, c & 7);  bput(z, 1, c >> 3 ? 1 : 0);
        if (c >> 3) bput(z, 5, c >> 3);
      });
    Fi(q->ncl, Fj(8,
      q->book[i][j] = -1;
      if (!(q->casc[i] & 1UL << j)) continue;
      v = fld(z, M_RBOOK, 8, m0 + m1);  m1 = v - m0;  m0 = v;
      q->book[i][j] = (i32) v)));
}


static void mappings(io * z, u32 ch) {
  vb_setup * s = z->v->cur;
  u32 nm, j, i, sub = 1, steps = 0, nmode, fl;
  vb_map * q;
  nm = fld(z, M_NMAP, 6, 0) + 1;
  s->mp = xmalloc(nm * sizeof *s->mp);  s->nmp = nm;
  memset(s->mp, 0, nm * sizeof *s->mp);
  Fj(nm,
    q = s->mp + j;
    cst(z, 16, 0);                            /*  mapping type  */
    /*  The counts imply their preceding flags.  */
    if (z->enc) {
      fl = bget(z, 1);  sub = fl ? bget(z, 4) + 1 : 1;
      FATAL_UNLESS(!fl || sub > 1, "vorbis: invalid submap flag");
    }
    /*  sub - 1 is what the packet carries, so a decoded 0xFFFFFFFF wraps to
        zero submaps and past the check below rather than into it.  */
    sub = mv(z, S_BULK, M_SUBM, z->enc ? sub - 1 : 0, 0) + 1;
    FATAL_UNLESS(sub >= 1 && sub <= VB_MAXSUB, "vorbis: mapping has %lu submaps",
                 (unsigned long) sub);
    if (!z->enc) { bput(z, 1, sub > 1);  if (sub > 1) bput(z, 4, sub - 1); }
    if (z->enc) steps = bget(z, 1) ? bget(z, 8) + 1 : 0;
    steps = mv(z, S_BULK, M_CSTEP, steps, 0);
    if (!z->enc) { bput(z, 1, steps != 0);  if (steps) bput(z, 8, steps - 1); }
    q->sub = sub;  q->nstep = steps;
    FATAL_UNLESS(steps <= VB_MAXCH, "vorbis: %lu coupling steps",
                 (unsigned long) steps);
    Fi(steps,
      q->mag[i] = (u8) fld(z, M_MAGANG, (int) blr_ilog(ch - 1), 0);
      q->ang[i] = (u8) fld(z, M_MAGANG, (int) blr_ilog(ch - 1), 0);
      FATAL_UNLESS(q->mag[i] < ch && q->ang[i] < ch && q->mag[i] != q->ang[i],
                   "vorbis: invalid coupling channels %d/%d of %lu",
                   q->mag[i], q->ang[i], (unsigned long) ch));
    cst(z, 2, 0);                             /*  reserved  */
    if (sub > 1) Fi(ch,
      q->mux[i] = (u8) fld(z, M_MUX, 4, 0);
      FATAL_UNLESS(q->mux[i] < sub, "vorbis: channel %lu maps to submap %d of %lu",
                   (unsigned long) i, q->mux[i],
                   (unsigned long) sub));
    Fi(sub,
      fld(z, M_SMT, 8, 0);
      q->fl[i] = (u8) fld(z, M_SMF, 8, 0);  q->rs[i] = (u8) fld(z, M_SMR, 8, 0)));
  nmode = fld(z, M_NMODE, 6, 0) + 1;
  FATAL_UNLESS(nmode <= VB_MAXMODE, "vorbis: %lu modes", (unsigned long) nmode);
  s->nmd = nmode;
  Fi(nmode,
    s->blockflag[i] = (u8) tfld(z, F_BLKF, 1, 1);
    cst(z, 16, 0);  cst(z, 16, 0);            /*  window and transform  */
    s->mdmap[i] = (u8) fld(z, M_MDMAP, 8, 0));
  cst(z, 1, 1);                               /*  framing  */
}


static void ident(io * z) {
  vb_info * n = &z->v->i;
  u32 v;
  cst(z, 8, 1);  cst(z, 8, 'v');  cst(z, 8, 'o');  cst(z, 8, 'r');
  cst(z, 8, 'b');  cst(z, 8, 'i');  cst(z, 8, 's');  cst(z, 32, 0);
  n->ch = fld(z, M_CH, 8, 0);
  n->rate = fld(z, M_RATE, 32, 0);
  fld(z, M_BRMAX, 32, 0);  fld(z, M_BRNOM, 32, 0);  fld(z, M_BRMIN, 32, 0);
  v = fld(z, M_BLK, 8, 0);
  n->bs0 = 1UL << (v & 15);  n->bs1 = 1UL << (v >> 4);
  fld(z, M_FRAME, 8, 0);
  /*  Enforce the four bounds from libvorbis `_vorbis_unpack_info`.  */
  FATAL_UNLESS(n->ch >= 1, "vorbis: %lu channels", (unsigned long) n->ch);
  FATAL_UNLESS(n->rate >= 1, "vorbis: sample rate 0");
  FATAL_UNLESS(n->bs0 >= 64 && n->bs1 >= n->bs0 && n->bs1 <= 8192,
               "vorbis: block sizes %lu and %lu",
               (unsigned long) n->bs0, (unsigned long) n->bs1);
}

/*  Bank comment-byte trees by the prior high nibble.  */
static void comment(io * z) {
  u32 prev = 0, i, idx, bank;
  int k, b = 0;
  u16 * s;
  u8 * sc;
  Fi(z->len,
    bank = prev >> 4;
    if (bank >= VB_CBANK) bank = VB_CBANK - 1;
    s = z->v->cmt + bank * VB_CSIZE;  idx = 1;
    sc = z->v->cmtc + bank * VB_CSIZE;
    for (k = 7; k >= 0; k--) {
      b = cbit(z, S_BULK, s + idx, sc + idx, z->b[i] >> k & 1);
      idx = idx * 2 + (u32) b;
    }
    prev = idx - VB_CSIZE;
    if (!z->enc) z->b[i] = (u8) prev);
  z->pos = z->len * 8;
}

/*  Validate books before the payload dereferences them.  */
static void su_check(vb_setup * s) {
  u32 i, j;
  /*  Refuse floors beyond the model arena's explicit capacity.  */
  FATAL_UNLESS(s->nfl <= VB_MAXFLOOR, "vorbis: %lu floors, limit %d",
               (unsigned long) s->nfl, VB_MAXFLOOR);
  Fi(s->nfl, Fj(VB_MAXCLASS,
    u32 k;
    FATAL_UNLESS(s->fl[i].cbook[j] < (i32) s->nbk, "vorbis: floor master book %ld",
                 (long) s->fl[i].cbook[j]);
    Fk(8,
      FATAL_UNLESS(s->fl[i].csb[j][k] < (i32) s->nbk, "vorbis: floor subclass "
                   "book %ld", (long) s->fl[i].csb[j][k]))));
  Fi(s->nrs,
    u32 k;
    FATAL_UNLESS(s->rs[i].cbook < s->nbk, "vorbis: residue class book %lu",
                 (unsigned long) s->rs[i].cbook);
    FATAL_UNLESS(s->rs[i].ncl <= 16,
                 "vorbis: %lu residue classifications, limit 16",
                 (unsigned long) s->rs[i].ncl);
    Fj(s->rs[i].ncl, Fk(8,
      i32 b = s->rs[i].book[j][k];
      FATAL_UNLESS(b < (i32) s->nbk, "vorbis: residue book %ld", (long) b);
      FATAL_UNLESS(b < 0 || s->bk[b].look == 1, "vorbis: residue book %ld has "
                   "no lookup table", (long) b))));
  Fi(s->nmd, FATAL_UNLESS(s->mdmap[i] < s->nmp, "vorbis: mode %lu names mapping "
                          "%d", (unsigned long) i, s->mdmap[i]));
  Fi(s->nmp, Fj(s->mp[i].sub,
    FATAL_UNLESS(s->mp[i].fl[j] < s->nfl, "vorbis: submap floor %d", s->mp[i].fl[j]);
    FATAL_UNLESS(s->mp[i].rs[j] < s->nrs, "vorbis: submap residue %d", s->mp[i].rs[j])));
}

static void setup(io * z) {
  vb_ctx * v = z->v;
  vb_setup * s;
  sz i;
  cst(z, 8, 5);  cst(z, 8, 'v');  cst(z, 8, 'o');  cst(z, 8, 'r');
  cst(z, 8, 'b');  cst(z, 8, 'i');  cst(z, 8, 's');
  Fi(sizeof RST, mdl_reset(z->v->m + RST[i]));
  z->v->pb = 0;
  s = xmalloc(sizeof *s);  memset(s, 0, sizeof *s);
  v->su = xrealloc(v->su, (v->nsu + 1) * sizeof *v->su);
  v->su[v->nsu] = s;  v->csu = v->nsu++;  v->cur = s;
  codebooks(z);  floors(z);  residues(z);  mappings(z, z->v->i.ch);
  su_check(s);
}

static void hdr(io * z, int which) {
  switch (which) {
    case 0: ident(z);  break;
    case 1: comment(z);  break;
    default: setup(z);  break;
  }
  /*  Only zero padding may remain after the parsed header.  */
  FATAL_UNLESS(z->len * 8 - z->pos < 8, "vorbis: %lu unparsed packet bits",
               (unsigned long) (z->len * 8 - z->pos));
  if (z->enc)
    FATAL_UNLESS(bget(z, (int) (z->len * 8 - z->pos)) == 0,
                 "vorbis: nonzero packet padding");
}

static void ioinit(io * z, vb_ctx * v, int enc, u8 * pkt, sz len) {
  sz i;
  z->enc = enc;  z->v = v;  z->b = pkt;  z->len = len;  z->pos = 0;
  z->probe = z->rawpkt = z->replay = z->choices = 0;  z->symbol = 0;
  Fi(3, z->e[i] = NULL;  z->d[i] = NULL);
}

void vb_hdr_enc(vb_ctx * v, rc_enc * e, int which, const u8 * pkt, sz len) {
  io z;
  ioinit(&z, v, 1, (u8 *) pkt, len);  z.e[S_BULK] = e;
  hdr(&z, which);
}

void vb_hdr_dec(vb_ctx * v, rc_dec * d, int which, u8 * pkt, sz len) {
  io z;
  ioinit(&z, v, 0, pkt, len);  z.d[S_BULK] = d;
  memset(pkt, 0, len);
  hdr(&z, which);
}

/*  Mode and window contexts reset at link boundaries.  */

/*  Arena table sizes as products of their context axes. Shared tables precede
    the five tables repeated for each model slot.  */

static const u32 AR_SIZE[A_NTAB] = {
  AR_HIST2,                                            /*  A_USED   */
  VB_MAXFLOOR * AR_HIST2 * VB_MAXPOST,                 /*  A_FZERO  */
  VB_MAXFLOOR * AR_PLEN * VB_MAXPOST * 8,              /*  A_FLEN   */
  VB_MAXFLOOR * VB_MAXPOST * AR_TRI * AR_LOW2,         /*  A_FMAG   */
  AR_NRES * AR_PCLS * AR_NPART * 16,                   /*  A_CLASS  */
  AR_NRES * 2 * 2 * 2 * AR_NBIN * AR_NCH,              /*  A_RZERO  */
  AR_NRES * 2 * 2 * 2 * AR_HIST2 * AR_ILOG * AR_NCH,   /*  A_RSIGN  */
  AR_NRES * 2 * AR_MCLS * 2 * AR_ILOG * AR_NCH,        /*  A_RONE   */
  AR_NRES * AR_MCLS * AR_NCH * 8,                      /*  A_RLEN   */
  AR_NRES * AR_MAGB * AR_MCLS * AR_NCH * AR_LOW2       /*  A_RMANT  */
};

/*  Pack magnitude bit positions into a triangular index.  */
static const u8 AR_TRIB[AR_MAGB + 1] = { 0, 0, 1, 3, 6, 10, 15, 21, 28 };

static INLINE u32 atab(const vb_ctx * v, int t, u32 idx) {
  return v->ab[t] + idx;
}

/*  Code one arena bit and update its packed observation count.  */
static INLINE int abit(io * z, u32 at, int lim, int b) {
  u32 * p = z->v->ar + at;
  if (!z->enc) return rc_dec_bit_packed(z->d[S_BULK], p, lim);
  rc_enc_bit_packed(z->e[S_BULK], p, lim, b);
  return b;
}

/*  Seed shared tables now and slot tables on demand.  */
static void arena(vb_ctx * v) {
  u32 i, at = 0;
  if (v->ar) return;
  Fi(A_NGLOB, v->ab[i] = at;  at += AR_SIZE[i]);
  v->aglob = at;
  for (at = 0, i = A_NGLOB; i < A_NTAB; i++) { v->ab[i] = at;  at += AR_SIZE[i]; }
  v->astep = at;
  v->arn = (sz) v->aglob + (sz) v->ns * v->astep;
  bm_alloc(&v->arena, v->arn * sizeof *v->ar);
  v->ar = (u32 *) v->arena.p;
  memset(v->ai, 0, sizeof v->ai);
  memset(v->ad, 0, sizeof v->ad);
  v->mem = xcalloc(VB_MEMSZ, 1);
  v->clsm = xcalloc(VB_CLSMSZ, 1);
}

/*  Preserve neighboring slots when reusing a model range.  */
static void ar_clear(vb_ctx * v, sz at, sz n) {
  memset(v->ar + at, 0, n * sizeof *v->ar);
}

/*  pool_slot bounds k. Seed once per partition, outside the digit loop.  */
static u32 ar_slot(vb_ctx * v, u32 k) {
  u32 at = v->aglob + k * v->astep;
  if (!v->ai[k]) {
    if (v->ad[k]) {
      ar_clear(v, at, v->astep);
    }
    v->ai[k] = 1;  v->ad[k] = 1;
  }
  return at;
}

static u32 * scratch(u32 ** p, sz * have, sz want) {
  if (*have < want) { free(*p);  *p = xmalloc(want * sizeof **p);  *have = want; }
  return *p;
}


/* Small prefix tables replace most bit-at-a-time codeword walks. */
static void bk_fast(vb_book * b) {
  u32 i, j;
  b->fast = xmalloc(256 * sizeof *b->fast);  b->fastbits = xmalloc(256);
  Fi(256,
    i32 t = 0;
    Fj(8,
      t = b->nd[2 * (u32) t + (i >> j & 1)];
      if (t <= 0) { j++;  break; });
    b->fast[i] = t;  b->fastbits[i] = (u8) j);
}

/* Cache only one packet's parsed symbols, capped at 256 KiB. Large unusual
   packets use the same checked traversal without caching. */
static u32 bk_get(io * z, vb_book * b) {
  u32 idx = 0, e;
  i32 t;
  if (z->replay) {
    e = z->v->symbols[z->symbol++];  z->pos += b->len[e];  return e;
  }
  if (z->len * 8 - z->pos >= 8) {
    sz at = z->pos >> 3;
    unsigned shift = (unsigned) (z->pos & 7);
    u32 prefix = z->b[at];
    if (shift) prefix |= (u32) z->b[at + 1] << 8;
    prefix = prefix >> shift & 255;
    if (!b->fast) bk_fast(b);
    t = b->fast[prefix];  z->pos += b->fastbits[prefix];
    FATAL_IF_HOT(t == 0)("vorbis: the packet holds no such codeword");
    if (t < 0) goto found;
    idx = (u32) t;
  }
  for (;;) {
    if (z->probe && z->pos >= z->len * 8) {
      z->rawpkt = 1;  return 0;
    }
    t = b->nd[2 * idx + bget1(z)];
    FATAL_IF_HOT(t == 0)("vorbis: the packet holds no such codeword");
    if (t < 0) break;
    idx = (u32) t;
  }
found:
  e = (u32) (-t - 1);
  if (z->probe && !z->v->symfull) {
    vb_ctx * v = z->v;
    if (v->nsymbols == v->csymbols) {
      if (v->csymbols == 1UL << 16) { v->symfull = 1;  return e; }
      v->csymbols = v->csymbols ? v->csymbols * 2 : 256;
      v->symbols = xrealloc(v->symbols, v->csymbols * sizeof *v->symbols);
    }
    v->symbols[v->nsymbols++] = e;
  }
  return e;
}

static void bk_put(io * z, vb_book * b, u32 e) {
  u32 k;
  FATAL_UNLESS(e < b->ent && b->len[e], "vorbis: codebook has no entry %lu",
               (unsigned long) e);
  for (k = b->len[e]; k > 0; k--) bput1(z, b->code[e] >> (k - 1) & 1);
}

/*  Code Floor 1 posts in ascending X order. Contexts include zero history,
    prior length at the same post, and the first two magnitude bits.  */
static u32 fl_val(io * z, u32 f, u32 i, u8 * h, u32 v) {
  vb_ctx * n = z->v;
  u32 o, idx, k, a = 0, nb = 0, r;
  u8 * pl = n->fpl + f * VB_MAXPOST + i;
  int b;
#ifdef BLR_PROFILE
  int e_h = *h, e_pl = *pl;
#endif
  o = atab(n, A_FZERO, (f * AR_HIST2 + (u32) *h) * VB_MAXPOST + i);
  PROF(prof_site = P_FZERO);
  b = abit(z, o, n->t.alim, v != 0);
  *h = (u8) ((b + *h * 2) & 3);
  if (!b) { *pl = 0;  PROF(prof_floor((int) f, (int) prof_ch, e_h, (int) i, e_pl, 0));
           PROF(prof_site = P_VOTHER);  return 0; }
  if (z->enc) { r = v;  while (r >>= 1) nb++; }
  FATAL_UNLESS(nb < AR_MAGB, "vorbis: floor value %lu exceeds 8 bits",
               (unsigned long) v);
  o = atab(n, A_FLEN, ((f * AR_PLEN + (u32) *pl) * VB_MAXPOST + i) * 8);
  PROF(prof_site = P_FLEN);
  idx = 1;
  for (k = 3; k > 0; k--)
    idx = idx * 2 + (u32) abit(z, o + idx, n->t.alim,
                              (int) (nb >> (k - 1) & 1));
  nb = idx - 8;  *pl = (u8) nb;
  o = atab(n, A_FMAG, ((f * VB_MAXPOST + i) * AR_TRI + AR_TRIB[nb]) * AR_LOW2);
  PROF(prof_site = P_FMAG);
  r = 1;
  for (k = nb; k > 0; k--) {
    b = abit(z, o + (nb - k) * AR_LOW2 + a, n->t.alim,
             (int) (v >> (k - 1) & 1));
    r = r * 2 + (u32) b;  a = (a * 2 + (u32) b) & 3;
  }
  PROF(prof_floor((int) f, (int) prof_ch, e_h, (int) i, e_pl, (i32) r));
  PROF(prof_site = P_VOTHER);
  return r;
}

static int fl_get(io * z, vb_floor * f, u32 * y, u32 * classes) {
  vb_setup * s = z->v->cur;
  u32 i, k, post = 2, p, cd, cs, cv;
  if (!bget(z, 1)) return 0;
  p = blr_ilog(f->quant - 1);
  y[0] = bget(z, p);  y[1] = bget(z, p);
  if (z->rawpkt) return 0;
  Fi(f->parts,
    u32 c = f->pcls[i], cv0;
    cd = f->cdim[c];  cs = f->csub[c];  cv = 0;
    if (cs) cv = bk_get(z, s->bk + f->cbook[c]);
    if (z->rawpkt) return 0;
    classes[i] = cv0 = cv;
    Fk(cd,
      i32 b = f->csb[c][cv & ((1UL << cs) - 1)];
      cv >>= cs;
      y[post + k] = b >= 0 ? bk_get(z, s->bk + b) : 0;
      if (z->rawpkt) return 0);
    /* Record whether the value model needs a classword correction. */
    if (cv0 >> cs * cd) z->choices = 1;
    Fk(cd,
      u32 j, d = cv0 >> k * cs & ((1UL << cs) - 1);
      Fj(d,
        u32 mx = f->csb[c][j] < 0 ? 1 : s->bk[f->csb[c][j]].ent;
        if (y[post + k] < mx) z->choices = 1));
    post += cd);
  return 1;
}

/*  Select the first subclass book that can represent the value. An absent book
    represents zero.  */
static void fl_put(io * z, vb_floor * f, const u32 * y, int used,
                    const u32 * classes) {
  vb_setup * s = z->v->cur;
  u32 i, k, j, post = 2, p, cd, cs, cv, mx;
  if (!z->enc) bput(z, 1, (u32) used);
  if (!used) return;
  p = blr_ilog(f->quant - 1);
  FATAL_UNLESS(y[0] < 1UL << p && y[1] < 1UL << p,
               "vorbis: floor post does not fit %lu bits", (unsigned long) p);
  if (!z->enc) { bput(z, p, y[0]);  bput(z, p, y[1]); }
  Fi(f->parts,
    u32 c = f->pcls[i];
    cd = f->cdim[c];  cs = f->csub[c];  cv = 0;
    if (cs) {
      Fk(cd,
        Fj(1UL << cs,
          mx = f->csb[c][j] < 0 ? 1 : s->bk[f->csb[c][j]].ent;
          if (y[post + k] < mx) break);
        FATAL_UNLESS(j < 1UL << cs, "vorbis: no class %lu book fits value %lu",
                     (unsigned long) c, (unsigned long) y[post + k]);
        cv |= j << k * cs);
      if (z->choices)
        cv = mv(z, S_BULK, M_FCLASS, z->enc ? classes[i] : 0, cv);
      if (!z->enc) bk_put(z, s->bk + f->cbook[c], cv);
    }
    Fk(cd,
      i32 b = f->csb[c][cv & ((1UL << cs) - 1)];
      cv >>= cs;
      if (!z->enc && b >= 0) bk_put(z, s->bk + b, y[post + k]));
    post += cd);
}

/*  Store each residue entry as centered base-`nv` multiplicand digits.  */
/*  Each CM stage has a bit history, state map, and mixer.  */
const u8 CM_LEVMASK[CM_NLEV] =
  { 0x00, 0x05, 0x1D, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F };

static int mcls(i32 v) {
  u32 a = (u32) (v < 0 ? -v : v);
  return !a ? 0 : a == 1 ? 1 : a <= 3 ? 2 : a <= 7 ? 3 : 4;
}

static void cm_alloc(vb_ctx * v) {
  if (v->cm.live) return;
  cm_new(&v->cm, CM_NST, CM_BITS, CM_SEL, v->t.lr, v->t.alim);
}

/*  Remember the previous digit and channel for same-bin context.  */
static void cm_step(vb_ctx * v, u32 ch, u32 c, i32 val) {
  v->nv1[ch] = v->nv0[ch];  v->nv0[ch] = val;  v->nidx[ch] = c;
  v->nxv = val;  v->npch = (int) ch;  v->nstarted = 1;
}

/*  One mixed bit: the arena slot at `off` is the caller-owned input the
    context mixer refines, and adapts afterwards.  */
static INLINE int pbit(io * z, u32 off, int st, int sel, u32 h, int exp,
                       int b) {
  vb_ctx * v = z->v;
  return cm_bit(&v->cm, st, sel, h, v->ar + off, exp, b);
}

/*  Partition classes use a four-bit tree banked by partition index. The final
    bank covers larger indices. A tune flag adds the bucketed class from the
    previous packet.  */
static u32 rs_cls(io * z, u32 q, u32 ch, u32 p, u32 v) {
  vb_ctx * n = z->v;
  u8 * mc;
  u32 o, idx = 1, k;
  if (p >= AR_NPART) p = AR_NPART - 1;
  if (ch >= AR_NCH) ch = AR_NCH - 1;
  mc = n->clsm + (q * AR_NCH + ch) * AR_NPART + p;
  o = atab(n, A_CLASS, ((q * AR_PCLS + (u32) *mc) * AR_NPART + p) * 16);
  PROF(prof_site = P_CLASS);
  for (k = 4; k > 0; k--)
    idx = idx * 2 + (u32) abit(z, o + idx, n->t.alim,
                               (int) (v >> (k - 1) & 1));
  /*  Read before write: within one packet each partition is visited once, so
      what *mc held on the way in is the previous packet's class.  */
  *mc = (u8) (!(n->t.flags & VB_TF_CLS) ? 0
              : idx - 16 == 0 ? 1 : idx - 16 <= 1 ? 2 : idx - 16 <= 3 ? 3
              : idx - 16 <= 7 ? 4 : 5);
  PROF(prof_cls((int) q, (int) ch, (int) p, (i32) (idx - 16)));
  PROF(prof_site = P_VOTHER);
  return idx - 16;
}

/*  Specialize only the stage masks used by CM_LEVMASK.  */
#define RS_CM 0
#define RS_NAME(n) rs_plain_##n
#include "residue.h"

#define RS_CM 5
#define RS_NAME(n) rs_zero_one_##n
#include "residue.h"

#define RS_CM 29
#define RS_NAME(n) rs_no_sign_##n
#include "residue.h"

#define RS_CM 31
#define RS_NAME(n) rs_mixed_##n
#include "residue.h"

static void rs_part(io * z, vb_res * r, vb_book * b, u32 q, u32 pass, u32 g,
                    u32 il) {
  switch (z->v->cm_mask) {
  case 0: rs_plain_part(z, r, b, q, pass, g, il);  break;
  case 5: rs_zero_one_part(z, r, b, q, pass, g, il);  break;
  case 29: rs_no_sign_part(z, r, b, q, pass, g, il);  break;
  case 31: rs_mixed_part(z, r, b, q, pass, g, il);  break;
  default: FATAL("vorbis: unsupported residue stage mask");
  }
}

/*  Process compacted residue types 0 and 1 or interleaved type 2.  */
static void residue(io * z, u32 rno, const u8 * nz, u32 nch, u32 n) {
  vb_setup * s = z->v->cur;
  vb_res * r = s->rs + rno;
  vb_book * cb = s->bk + r->cbook;
  u32 q = rno & 3, il = 0, vch, end, np, pv, pc, i, j, k, cw, w;
  u32 * cl;
  u32 nc = 0;
  Fi(nch, if (nz[i]) nc++);
  if (!nc) return;
  if (r->type == 2) { il = nch;  vch = 1;  end = MIN(r->end, n * nch); }
  else { vch = nc;  end = MIN(r->end, n); }
  if (end <= r->beg) return;
  np = (end - r->beg) / r->psz;
  if (!np) return;
  pv = cb->dim;
  w = np + pv;
  cl = scratch(&z->v->cs, &z->v->csn, (sz) vch * w);
  Fi(8,
    pc = 0;
    while (pc < np) {
      if (!i)
        Fj(vch,
          if (z->enc) {
            cw = bk_get(z, cb);
            if (z->rawpkt) return;
            for (k = pv; k > 0; k--) { cl[j * w + pc + k - 1] = cw % r->ncl;
                                       cw /= r->ncl; }
            /*  Refuse unused digits that would change the rebuilt codeword.  */
            FATAL_UNLESS(!cw, "vorbis: residue classword exceeds its partitions");
          }
          PROF(prof_ch = j);
          if (!z->probe) Fk(pv, cl[j * w + pc + k] = rs_cls(z, q, j, pc + k,
                                             z->enc ? cl[j * w + pc + k] : 0));
          if (!z->enc) {
            cw = 0;
            Fk(pv, cw = cw * r->ncl + cl[j * w + pc + k]);
            bk_put(z, cb, cw);
          });
      Fk(pv,
        if (pc >= np) break;
        Fj(vch,
          i32 bn;
          FATAL_IF_HOT(cl[j * w + pc] >= r->ncl)
            ("vorbis: residue class %lu, limit %lu",
             (unsigned long) cl[j * w + pc], (unsigned long) r->ncl);
          bn = r->book[cl[j * w + pc]][i];
          PROF(prof_rcls = cl[j * w + pc];  prof_rpart = pc);
          if (bn >= 0)
            rs_part(z, r, s->bk + bn, q, i, r->beg + pc * r->psz, il);
          if (z->rawpkt) return);
        pc++);
    });
}

/*  Process floor curves, coupling, and residues for one audio packet.  */
static void payload(io * z, u32 mode) {
  vb_ctx * v = z->v;
  vb_setup * s = v->cur;
  vb_map * mp = s->mp + s->mdmap[mode];
  u32 n = (s->blockflag[mode] ? v->i.bs1 : v->i.bs0) / 2;
  u32 ch = v->i.ch, k, i, j, m;
  u8 nz[VB_MAXCH], sub[VB_MAXCH];
  u32 * y = scratch(&v->ys, &v->ysn, (sz) ch * VB_MAXPOST);

  /*  Initialize every coupling index because reused setups may name channels
      absent from the current identification header.  */
  memset(nz, 0, sizeof nz);

  Fk(ch,
    u32 fno = mp->fl[mp->mux[k]];
    vb_floor * f = s->fl + fno;
    u32 * yc = y + k * VB_MAXPOST, classes[VB_MAXPART];
    u8 h = 0;
    int u = 0;
    PROF(prof_ch = k);
    if (z->enc) u = fl_get(z, f, yc, classes);
    if (z->rawpkt) return;
    if (z->probe) { nz[k] = (u8) u;  continue; }
    PROF(prof_site = P_USED);
    u = abit(z, atab(v, A_USED, v->hu), v->t.alim, u);
    PROF(prof_site = P_VOTHER);
    v->hu = (u8) ((u + v->hu * 2) & 3);
    nz[k] = (u8) u;
    if (u)
      Fi(f->posts, u32 p = f->srt[i];
                     yc[p] = fl_val(z, fno, p, &h, z->enc ? yc[p] : 0));
    if (!z->enc || z->choices) fl_put(z, f, yc, u, classes));
  Fi(mp->nstep, if (nz[mp->mag[i]] || nz[mp->ang[i]])
                  nz[mp->mag[i]] = nz[mp->ang[i]] = 1);
  Fi(mp->sub,
    m = 0;
    Fj(ch, if (mp->mux[j] == i) sub[m++] = nz[j]);
    if (m) residue(z, mp->rs[i], sub, m, n);
    if (z->rawpkt) return);
}

/* Probe without changing coding models. Type 1 uses the byte model for a
   peeled packet; bit 1 adds floor class corrections and bit 2 codes a tail. */
static int audio_type(io * z) {
  io p = *z;
  u32 md;
  p.probe = 1;  z->v->nsymbols = 0;  z->v->symfull = 0;
  FATAL_UNLESS(z->v->cur && z->v->cur->nmd, "vorbis: audio before setup");
  FATAL_UNLESS(bget(&p, 1) == 0, "vorbis: header on audio path");
  md = bget(&p, (int) blr_ilog(z->v->cur->nmd - 1));
  FATAL_UNLESS(md < z->v->cur->nmd, "vorbis: invalid audio mode");
  if (z->v->cur->blockflag[md]) bget(&p, 2);
  if (p.rawpkt) return 1;
  payload(&p, md);
  if (p.rawpkt) return 1;
  return (p.choices ? 2 : 0) |
    (p.len * 8 - p.pos >= 8 || bget(&p, (int) (p.len * 8 - p.pos)) ? 4 : 0);
}

/* Model exceptional bytes and padding using the existing small byte-context
   bank. Code from the current bit position, including a final partial byte. */
static sz packet_tail(io * z) {
  u32 prev = 0;
  while (z->pos < z->len * 8) {
    int n = (int) MIN((sz) 8, z->len * 8 - z->pos), k;
    u32 val = z->enc ? bget(z, n) : 0, idx = 1;
    u32 bank = MIN(prev >> 4, VB_CBANK - 1) * VB_CSIZE;
    for (k = n - 1; k >= 0; k--)
      idx = idx * 2 + (u32) cbit(z, S_BULK, z->v->cmt + bank + idx,
                                 z->v->cmtc + bank + idx, val >> k & 1);
    prev = idx - (1U << n);
    if (!z->enc) bput(z, n, prev);
  }
  return z->pos;
}

static sz audio(io * z, int cont) {
  vb_ctx * v = z->v;
  u32 md, bank, type;
  int d, w[2], i;

  FATAL_UNLESS(v->cur && v->cur->nmd > 0, "vorbis: audio before setup");
  d = (int) blr_ilog(v->cur->nmd - 1);
  PROF(prof_pkt++);
  type = z->enc ? (u32) audio_type(z) : mdl_dec(&v->apt, z->d[S_TYPE]);
  FATAL_UNLESS(type <= 6 && type != 3 && type != 5,
               "vorbis: invalid audio packet type");
  if (z->enc) mdl_enc(&v->apt, z->e[S_TYPE], type);
  if (type == 1) {
    sz bits = packet_tail(z);
    FATAL_UNLESS(z->len && !(z->b[0] & 1), "vorbis: invalid peeled audio packet");
    return bits;
  }
  z->choices = (type & 2) != 0;
  if (z->enc) {
    z->replay = !v->symfull;
    FATAL_UNLESS(bget(z, 1) == 0, "vorbis: header on audio path");
  } else bput(z, 1, 0);
  arena(v);
  cm_bind(&v->cm, z->e[S_BULK], z->d[S_BULK]);

  bank = (u32) (cont & 1) + (u32) (v->pm & 1) * 2 + (u32) v->pw[1] * 4;
  md = z->enc ? bget(z, d) : 0;
  md = tv(z, S_MODE, v->am + bank * VB_MSTEP,
          v->amc + bank * VB_MSTEP, d, md);
  if (!z->enc) bput(z, d, md);
  FATAL_UNLESS(md < v->cur->nmd, "vorbis: mode %lu of %lu",
               (unsigned long) md, (unsigned long) v->cur->nmd);

  w[0] = w[1] = 0;
  if (v->cur->blockflag[md])
    Fi(2,
      /*  aw[0] is the previous-window flag's pair of slots, aw[1] the
          next-window flag's; each is indexed by the other's history.  */
      w[i] = cbit(z, S_MODE, v->aw[i] + v->pw[!i],
                  v->awc[i] + v->pw[!i],
                  z->enc ? (int) bget(z, 1) : 0);
      if (!z->enc) bput(z, 1, (u32) w[i]));
  v->pm = (u8) md;  v->pw[0] = (u8) w[0];  v->pw[1] = (u8) w[1];
  payload(z, md);
  FATAL_IF_HOT(z->replay && z->symbol != v->nsymbols)
    ("internal: Vorbis symbol replay mismatch");
  if (type & 4) return packet_tail(z);
  /*  Only byte-alignment padding may remain.  */
  FATAL_UNLESS(z->len * 8 - z->pos < 8, "vorbis: %lu unparsed audio bits",
               (unsigned long) (z->len * 8 - z->pos));
  /*  Require the remaining byte-padding bits to be zero.  */
  { sz used = z->pos;
    if (z->enc)
      FATAL_UNLESS(bget(z, (int) (z->len * 8 - z->pos)) == 0,
                   "vorbis: nonzero audio padding");
    return used; }
}

sz vb_aud_enc(vb_ctx * v, rc_enc * eb, rc_enc * em, rc_enc * ep,
              const u8 * pkt, sz len, int cont) {
  io z;
  ioinit(&z, v, 1, (u8 *) pkt, len);
  z.e[S_BULK] = eb;  z.e[S_MODE] = em;  z.e[S_TYPE] = ep;
  return audio(&z, cont);
}

sz vb_aud_dec(vb_ctx * v, rc_dec * db, rc_dec * dm, rc_dec * dp,
              u8 * pkt, sz len, int cont) {
  io z;
  ioinit(&z, v, 0, pkt, len);
  z.d[S_BULK] = db;  z.d[S_MODE] = dm;  z.d[S_TYPE] = dp;
  return audio(&z, cont);
}
