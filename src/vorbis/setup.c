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

static INLINE u32 rev32(u32 x) {
  x = (x >> 1 & 0x55555555UL) | (x & 0x55555555UL) << 1;
  x = (x >> 2 & 0x33333333UL) | (x & 0x33333333UL) << 2;
  x = (x >> 4 & 0x0F0F0F0FUL) | (x & 0x0F0F0F0FUL) << 4;
  x = (x >> 8 & 0x00FF00FFUL) | (x & 0x00FF00FFUL) << 8;
  return x >> 16 | x << 16;
}

static void bk_words(vb_book * b) {
  u32 mk[33], i, j, e, l;
  memset(mk, 0, sizeof mk);
  Fi(b->ent,
    l = b->len[i];
    if (!l) continue;
    e = mk[l];
    FATAL_IF_HOT(l < 32 && e >> l)("vorbis: over-populated codebook");
    b->code[i] = e;
    b->rcode[i] = l < 32 ? rev32(e) >> (32 - l) : rev32(e);
    for (j = l; j > 0; j--) {
      if (mk[j] & 1) { mk[j] = j == 1 ? mk[1] + 1 : mk[j - 1] << 1;  break; }
      mk[j]++;
    }
    for (j = l + 1; j < 33; j++) {
      if (mk[j] >> 1 != e) break;
      e = mk[j];  mk[j] = mk[j - 1] << 1;
    });
}

static void bk_tree(blr_arena * ar, vb_book * b) {
  u32 i, k, n = 1, u = 0, idx, cap;
  i32 t;
  Fi(b->ent, if (b->len[i]) u++);
  cap = u + 2;
  b->nd = ar_take(ar, 2 * cap * sizeof *b->nd);
  memset(b->nd, 0, 2 * cap * sizeof *b->nd);
  Fi(b->ent,
    if (!b->len[i]) continue;
    idx = 0;
    for (k = b->len[i]; k > 1; k--) {
      u32 c = 2 * idx + (b->code[i] >> (k - 1) & 1);
      t = b->nd[c];
      if (!t) { FATAL_IF_HOT(n >= cap)("vorbis: codebook tree overflows");
                t = (i32) n++;  b->nd[c] = t; }
      FATAL_IF_HOT(t <= 0)("vorbis: codebook is not prefix free");
      idx = (u32) t;
    }
    idx = 2 * idx + (b->code[i] & 1);
    FATAL_IF_HOT(b->nd[idx])("vorbis: codebook is not prefix free");
    b->nd[idx] = -(i32) (i + 1));
}

static void bk_look(blr_arena * ar, vb_book * b) {
  u32 i, mx = 0, np;
  b->divshift = 24 + blr_ilog(b->nv - 1);
  b->divmul = (u32) ((((uint64_t) 1 << b->divshift) + b->nv - 1) / b->nv);
  Fi(b->nv, if (b->mult[i] > mx) mx = b->mult[i]);
  b->base = mx + 1;  b->off = b->base >> 1;
  b->inv = ar_take(ar, b->base * sizeof *b->inv);
  Fi(b->base, b->inv[i] = (u32) -1);
  Fi(b->nv,
    FATAL_IF_HOT(b->inv[b->mult[i]] != (u32) -1)
      ("vorbis: duplicate codebook multiplicand %lu",
       (unsigned long) b->mult[i]);
    b->inv[b->mult[i]] = i);
  b->dig = NULL;
  if ((uint64_t) b->ent * b->dim <= VB_DIGTAB && b->base <= 32768) {
    u32 k, t;
    b->dig = ar_take(ar, (sz) b->ent * b->dim * sizeof *b->dig);
    Fi(b->ent,
      t = i;
      Fk(b->dim,
        u32 next = (u32) ((uint64_t) t * b->divmul >> b->divshift);
        b->dig[i * b->dim + k] =
          (i16) ((i32) b->mult[t - next * b->nv] - (i32) b->off);
        t = next));
  }
  np = 1;
  if (b->nv > 1) Fi(b->dim, np *= b->nv);
  Fi(b->ent, if (b->len[i] && i >= np)
               FATAL("vorbis: codebook entry %lu exceeds lookup grid %lu",
                     (unsigned long) i,
                     (unsigned long) np));
  b->fz = NULL;  b->fzm = 0;
  if (b->dim && (uint64_t) b->dim * (np / MAX(b->nv, 1)) <= VB_FZMAX) {
    sz w = np / MAX(b->nv, 1), j;
    u32 k;
    int settles = 0;
    b->fz = ar_take(ar, (sz) b->dim * w);  memset(b->fz, 0, (sz) b->dim * w);
    Fi(b->ent,
      u32 t = i, pw = 1;
      if (!b->len[i]) continue;
      Fk(b->dim,
        u32 next = (u32) ((uint64_t) t * b->divmul >> b->divshift);
        i32 d = (i32) b->mult[t - next * b->nv] - (i32) b->off;
        b->fz[k * w + i % pw] |=
          (u8) ((d ? VB_FZNZ : VB_FZ0) | (d > 0 ? VB_FZP : d < 0 ? VB_FZN : 0) |
                (d == 1 || d == -1 ? VB_FZ1 : d ? VB_FZ2 : 0));
        t = next;  pw *= b->nv));
    Fj((sz) b->dim * w,
      u8 f = b->fz[j];
      b->fzm |= f;
      if (f && ((f & (VB_FZ0 | VB_FZNZ)) != (VB_FZ0 | VB_FZNZ) ||
                (f & VB_FZNZ &&
                 (f & (VB_FZP | VB_FZN)) != (VB_FZP | VB_FZN)))) settles = 1);
    if (!settles) b->fz = NULL;
  }
  if (!b->fzm) b->fzm = VB_FZALL;
}

static u32 pool_dist(const vb_slot * s, const u8 * len, u32 n) {
  u32 i, d = 0;
  Fi(n, d += (u32) ABS(s->len[i] - len[i]));
  return d;
}

static void pool_take(vb_ctx * v, u32 k, vb_book * b, const u8 * len) {
  vb_slot * s = v->sl + k;
  if (s->cap < b->ent) {
    xfree(s->len);  s->len = xmalloc(b->ent);  s->cap = b->ent;
  }
  memcpy(s->len, len, b->ent);
  s->ent = b->ent;  s->nv = b->nv;  s->dm = b->dm;  s->de = b->de;
  s->ds = b->ds;
}

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
  { u32 nu = 0;  Fi(b->ent, if (b->len[i]) nu++);
    nrm = bd >= 0x1000000UL ? 0xFFFFFFFFUL
        : nu               ? (bd << 8) / nu
        : bd               ? 0xFFFFFFFFUL : 0; }
  if (nrm <= 0x100) {
    v->sl[best].use++;  v->st[best] = 1;  xfree(tmp);  return best;
  }
  if (v->nsl < VB_NSLOT) {
    k = v->nsl++;  pool_take(v, k, b, len);  v->st[k] = 1;  xfree(tmp);
    return k;
  }
  k = best;
  if (nrm > 0xFFE) {
    u32 lo = 0xFFFFFFFFUL;
    k = 0;
    Fi(v->nsl, if (v->sl[i].use < lo) { lo = v->sl[i].use;  k = i; });
    v->ai[k] = 0;
  }
  Fi(v->nsl, if (i != k) sum += v->sl[i].use);
  v->sl[k].use = sum >> blr_ilog(VB_NSLOT >> 1);
  pool_take(v, k, b, len);
  v->st[k] = 1;  xfree(tmp);
  return k;
}

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

static void f32(vb_book * b, u32 w) {
  b->dm = w & 0x1FFFFF;  b->de = (int) (w >> 21 & 0x3FF) - 788;
  b->ds = (int) (w >> 31);
  if (!b->dm) { b->de = 0;  b->ds = 0;  return; }
  while (!(b->dm & 1)) { b->dm >>= 1;  b->de++; }
}

static u32 * vd_boff, * vd_toff, * vd_soff, * vd_slen;
static uint64_t * vd_key;

static void vd_prepare(void) {
  u32 i, j, at = 0, r = 0;
  if (vd_boff) return;
  vd_boff = xmalloc((sz) VD_NBOOK * sizeof *vd_boff);
  vd_key = xmalloc((sz) VD_NBOOK * sizeof *vd_key);
  vd_toff = xmalloc((sz) VD_NTAIL * sizeof *vd_toff);
  vd_soff = xmalloc((sz) VD_NSETUP * sizeof *vd_soff);
  vd_slen = xmalloc((sz) VD_NSETUP * sizeof *vd_slen);
  Fi(VD_NBOOK,
    vd_boff[i] = at;  vd_key[i] = rd64le(VD_DATA + at);
    at += (VD_BOOKBITS[i] + 7) >> 3);
  Fi(VD_NTAIL, vd_toff[i] = at;  at += (VD_TAILBITS[i] + 7) >> 3);
  Fi(VD_NSETUP,
    u32 bits = 64 + VD_TAILBITS[VD_RECIPE[r]];
    vd_soff[i] = r;
    Fj(VD_RECIPE[r + 1], bits += VD_BOOKBITS[VD_RECIPE[r + 2 + j]]);
    vd_slen[i] = (bits + 7) >> 3;
    r += 2 + VD_RECIPE[r + 1]);
}

static uint64_t vd_win(const io * z) {
  sz k = z->pos >> 3, sh = z->pos & 7;
  uint64_t w = bwin(z);
  if (sh && k + 8 < z->len) w |= (uint64_t) z->b[k + 8] << (64 - sh);
  return w;
}

static int vd_same(const u8 * b, sz len, sz at, const u8 * d, u32 n) {
  sz i, k = at >> 3;
  int sh = (int) (at & 7);
  if (at + n > len * 8) return 0;
  Fi(n >> 3,
    u32 x = b[k + i] >> sh;
    if (sh) x |= (u32) b[k + i + 1] << (8 - sh);
    if ((u8) x != d[i]) return 0);
  for (i = at + (n & ~7UL); i < at + n; i++)
    if ((b[i >> 3] >> (i & 7) & 1) != (d[(i - at) >> 3] >> ((i - at) & 7) & 1))
      return 0;
  return 1;
}

static void vd_put(u8 * b, sz len, sz at, const u8 * d, u32 n) {
  sz i, k = at >> 3;
  int sh = (int) (at & 7);
  FATAL_UNLESS(at + n <= len * 8,
               "vorbis: dictionary entry overruns its packet");
  Fi(n >> 3,
    b[k + i] |= (u8) (d[i] << sh);
    if (sh) b[k + i + 1] |= (u8) (d[i] >> (8 - sh)));
  for (i = n & ~7UL; i < n; i++)
    b[(at + i) >> 3] |= (u8) ((d[i >> 3] >> (i & 7) & 1) << ((at + i) & 7));
}

static void vd_build(u32 k, u8 * b) {
  const u16 * r = VD_RECIPE + vd_soff[k];
  sz at = 64, len = vd_slen[k];
  u32 i;
  memcpy(b, "\5vorbis", 7);  b[7] = (u8) (r[1] - 1);
  Fi(r[1],
    vd_put(b, len, at, VD_DATA + vd_boff[r[2 + i]], VD_BOOKBITS[r[2 + i]]);
    at += VD_BOOKBITS[r[2 + i]]);
  vd_put(b, len, at, VD_DATA + vd_toff[r[0]], VD_TAILBITS[r[0]]);
}

static void vd_quiet(io * z, int on) {
  if (on) { z->quiet = z->enc ? 1 : 2;  z->enc = 1; }
  else { z->enc = z->quiet == 1;  z->quiet = 0; }
}

static u32 vd_bits(io * z, u32 n, u32 v) {
  if (z->enc) { rc_enc_bits(z->e[S_BULK], v, (int) n);  return v; }
  return (u32) rc_dec_bits(z->d[S_BULK], (int) n);
}

static int vd_is(u32 k, const u8 * b, sz len) {
  const u16 * r = VD_RECIPE + vd_soff[k];
  sz at = 64, i;
  if (memcmp(b, "\5vorbis", 7) || b[7] != (u8) (r[1] - 1)) return 0;
  Fi(r[1],
    if (!vd_same(b, len, at, VD_DATA + vd_boff[r[2 + i]],
                 VD_BOOKBITS[r[2 + i]])) return 0;
    at += VD_BOOKBITS[r[2 + i]]);
  if (!vd_same(b, len, at, VD_DATA + vd_toff[r[0]], VD_TAILBITS[r[0]]))
    return 0;
  at += VD_TAILBITS[r[0]];
  for (; at < len * 8 && at & 7; at++) if (b[at >> 3] >> (at & 7) & 1) return 0;
  for (i = at >> 3; i < len; i++) if (b[i]) return 0;
  return 1;
}

static int vd_fits(const io * z, int tail, u32 i) {
  return tail ? (z->pos + VD_TAILBITS[i] + 7) >> 3 == z->len
              : vd_slen[i] == z->len;
}

static int vd_pick(io * z, int tail) {
  vb_ctx * v = z->v;
  u32 i, n = 0, k = 0, hit = 0, nc = tail ? VD_NTAIL : VD_NSETUP,
      q = tail ? VQ_TAIL : VQ_WHOLE;
  Fi(nc, n += (u32) vd_fits(z, tail, i));
  if (!n) return 0;
  if (z->enc)
    Fi(nc,
      if (!vd_fits(z, tail, i)) continue;
      if (tail ? vd_same(z->b, z->len, z->pos, VD_DATA + vd_toff[i],
                         VD_TAILBITS[i])
               : vd_is(i, z->b, z->len)) { hit = 1;  break; }
      k++);
  if (!cbit(z, S_BULK, v->vq + q, v->vqc + q, (int) hit)) return 0;
  k = vd_bits(z, blr_ilog(n - 1), k);
  FATAL_UNLESS(k < n, "vorbis: dictionary %s %lu of %lu",
               tail ? "rest" : "setup", (unsigned long) k, (unsigned long) n);
  if (!z->enc)
    Fi(nc,
      if (!vd_fits(z, tail, i) || k--) continue;
      if (tail)
        vd_put(z->b, z->len, z->pos, VD_DATA + vd_toff[i], VD_TAILBITS[i]);
      else vd_build(i, z->b);
      break);
  return 1;
}

static u32 vd_book(io * z, u32 * next) {
  vb_ctx * v = z->v;
  u32 i = 0, hit = 0, nb = VD_NBOOK, p = *next < nb ? *next : 0;
  if (z->enc) {
    uint64_t w = vd_win(z);
    Fi(nb,
      if (vd_key[i] == w && vd_same(z->b, z->len, z->pos, VD_DATA + vd_boff[i],
                                    VD_BOOKBITS[i])) { hit = 1;  break; });
  }
  if (!cbit(z, S_BULK, v->vq + VQ_BOOK, v->vqc + VQ_BOOK, (int) hit)) return 0;
  if (!cbit(z, S_BULK, v->vq + VQ_NEXT, v->vqc + VQ_NEXT, i == p)) {
    i = vd_bits(z, blr_ilog(nb - 1), i);
    FATAL_UNLESS(i < nb, "vorbis: dictionary book %lu", (unsigned long) i);
  } else i = p;
  if (!z->enc)
    vd_put(z->b, z->len, z->pos, VD_DATA + vd_boff[i], VD_BOOKBITS[i]);
  *next = i + 1;
  return i + 1;
}

#define VB_BOOKMIN ((sz) 1 << 16)
#define VB_BOOKPB  16

static void book(io * z, vb_book * k, u32 j, sz * room) {
  u32 lk, vb, i;
  int mk;
  cst(z, 24, 0x564342);
  k->dim = fld(z, M_DIM, 16, 0);
  k->ent = fld(z, M_ENT, 24, 0);
  FATAL_UNLESS(k->dim > 0, "vorbis: codebook %lu has dimension 0",
               (unsigned long) j);
  FATAL_UNLESS(k->ent <= 1UL << 20, "vorbis: codebook %lu has %lu entries",
               (unsigned long) j, (unsigned long) k->ent);
  FATAL_UNLESS(k->ent <= *room,
               "vorbis: the codebooks of a %lu-byte setup "
               "hold over %lu entries",
               (unsigned long) z->len,
               (unsigned long) (VB_BOOKMIN + VB_BOOKPB * z->len));
  *room -= k->ent;
  FATAL_UNLESS(blr_ilog(k->dim) + blr_ilog(k->ent) <= 24,
               "vorbis: codebook %lu size %lu x %lu is unsupported",
               (unsigned long) j, (unsigned long) k->dim,
               (unsigned long) k->ent);
  k->len = ar_take(&z->v->tbl, k->ent);  memset(k->len, 0, k->ent);
  k->code = ar_take(&z->v->tbl, k->ent * sizeof *k->code);
  k->rcode = ar_take(&z->v->tbl, k->ent * sizeof *k->rcode);
  lengths(z, k);
  bk_words(k);
  if (z->enc) bk_tree(&z->v->tbl, k);
  lk = tfld(z, F_LOOK, 2, 4);
  k->look = (u8) lk;
  if (!lk) return;
  FATAL_UNLESS(lk == 1, "vorbis: unsupported codebook lookup type %lu",
               (unsigned long) lk);
  flt(z, M_FEXP0, F_MINSG);  f32(k, flt(z, M_FEXP1, F_DLTSG));
  vb = fld(z, M_VBITS, 4, 0) + 1;
  tfld(z, F_SEQ, 1, 1);
  k->nv = lk1(k->ent, k->dim);
  k->mult = ar_take(&z->v->tbl, (k->nv ? k->nv : 1) * sizeof *k->mult);
  mk = vb > 8 || k->nv > 0x100 ? M_MULTW : M_MULT;
  Fi(k->nv, k->mult[i] = fld(z, mk, (int) vb,
                             i & 1 ? (k->nv >> 1) - (i + 1) / 2
                                     : (k->nv >> 1) + i / 2));
  FATAL_UNLESS(k->nv > 0, "vorbis: codebook %lu has an empty lookup table",
               (unsigned long) j);
  bk_look(&z->v->tbl, k);
  k->slot = pool_slot(z->v, k);
}

static void codebooks(io * z) {
  vb_setup * s = z->v->cur;
  u32 nb, j, next = 0;
  sz room = VB_BOOKMIN + VB_BOOKPB * z->len;
  nb = fld(z, M_NBOOK, 8, 0) + 1;
  s->bk = xmalloc(nb * sizeof *s->bk);  s->nbk = nb;
  memset(s->bk, 0, nb * sizeof *s->bk);
  Fj(nb,
    sz at = z->pos;
    u32 d = !z->quiet ? vd_book(z, &next) : 0;
    if (!d) { book(z, s->bk + j, j, &room);  continue; }
    vd_quiet(z, 1);  book(z, s->bk + j, j, &room);  vd_quiet(z, 0);
    FATAL_UNLESS(z->pos == at + VD_BOOKBITS[d - 1],
                 "internal: dictionary book %lu parses to %lu bits",
                 (unsigned long) d - 1, (unsigned long) (z->pos - at)));
}

static void fl_sort(vb_floor * f) {
  u32 i, j, t;
  Fi(f->posts,
    f->srt[i] = (u8) i;
    for (j = i; j > 0 && f->x[f->srt[j - 1]] > f->x[f->srt[j]]; j--) {
      t = f->srt[j];  f->srt[j] = f->srt[j - 1];  f->srt[j - 1] = (u8) t;
    });
  Fi(f->posts, if (i) FATAL_UNLESS(f->x[f->srt[i]] != f->x[f->srt[i - 1]],
                                   "vorbis: floor X list repeats a value"));
  Fi(f->posts,
    u32 l = 0, h = 1;
    Fj(i,
      if (f->x[j] < f->x[i] && f->x[j] >= f->x[l]) l = j;
      if (f->x[j] > f->x[i] && f->x[j] <= f->x[h]) h = j);
    f->lo[i] = (u8) l;  f->hi[i] = (u8) h;
    f->rdx[i] = f->x[h] > f->x[l] ? vb_rcp(f->x[h] - f->x[l]) : 0);
}

static const u32 QUANT[4] = { 256, 128, 86, 64 };

static void floors(io * z) {
  vb_setup * s = z->v->cur;
  u32 nt, nf, k, i, j, nc, sub;
  vb_floor * q;
  nt = fld(z, M_NTIME, 6, 0) + 1;
  Fi(nt, cst(z, 16, 0));
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

static void residues(io * z) {
  vb_setup * s = z->v->cur;
  u32 nr, k, i, j, cb, m0, m1, v, c;
  vb_res * q;
  nr = fld(z, M_NRES, 6, 0) + 1;
  s->rs = xmalloc(nr * sizeof *s->rs);  s->nrs = nr;
  memset(s->rs, 0, nr * sizeof *s->rs);
  Fk(nr, memset(s->rs[k].th, 0xFF, sizeof s->rs[k].th));
  Fk(nr,
    q = s->rs + k;
    q->type = tfld(z, F_RSTYPE, 2, 16);
    FATAL_UNLESS(q->type <= 2, "vorbis: residue type above 2");
    q->beg = fld(z, M_RBEG, 24, 0);  q->end = fld(z, M_REND, 24, 0);
    q->psz = fld(z, M_RPSZ, 24, 0) + 1;
    q->ncl = fld(z, M_RNCL, 6, 0) + 1;
    FATAL_UNLESS(q->ncl <= VB_MAXRCL, "vorbis: residue has %lu classifications",
                 (unsigned long) q->ncl);
    q->cdshift = 24 + blr_ilog(q->ncl - 1);
    q->cdmul = (u32) ((((uint64_t) 1 << q->cdshift) + q->ncl - 1) / q->ncl);
    cb = fld(z, M_RBOOK, 8, z->v->pb);
    q->cbook = cb;
    m1 = cb - z->v->pb;  m0 = cb;  z->v->pb = cb;
    Fi(q->ncl,
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
      q->book[i][j] = (i32) v));
    q->npass = 1;
    Fi(q->ncl, while (q->casc[i] >> q->npass) q->npass++));
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
    cst(z, 16, 0);
    if (z->enc) {
      fl = bget(z, 1);  sub = fl ? bget(z, 4) + 1 : 1;
      FATAL_UNLESS(!fl || sub > 1, "vorbis: invalid submap flag");
    }
    sub = mv(z, S_BULK, M_SUBM, z->enc ? sub - 1 : 0, 0) + 1;
    FATAL_UNLESS(sub >= 1 && sub <= VB_MAXSUB,
                 "vorbis: mapping has %lu submaps", (unsigned long) sub);
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
    cst(z, 2, 0);
    if (sub > 1) Fi(ch,
      q->mux[i] = (u8) fld(z, M_MUX, 4, 0);
      FATAL_UNLESS(q->mux[i] < sub,
                   "vorbis: channel %lu maps to submap %d of %lu",
                   (unsigned long) i, q->mux[i], (unsigned long) sub));
    Fi(sub,
      fld(z, M_SMT, 8, 0);
      q->fl[i] = (u8) fld(z, M_SMF, 8, 0);
      q->rs[i] = (u8) fld(z, M_SMR, 8, 0)));
  nmode = fld(z, M_NMODE, 6, 0) + 1;
  FATAL_UNLESS(nmode <= VB_MAXMODE, "vorbis: %lu modes", (unsigned long) nmode);
  s->nmd = nmode;
  Fi(nmode,
    s->blockflag[i] = (u8) tfld(z, F_BLKF, 1, 1);
    cst(z, 16, 0);  cst(z, 16, 0);
    s->mdmap[i] = (u8) fld(z, M_MDMAP, 8, 0));
  cst(z, 1, 1);
}

static void ident(io * z) {
  vb_info * n = &z->v->i;
  u32 v, nominal;
  cst(z, 8, 1);  cst(z, 8, 'v');  cst(z, 8, 'o');  cst(z, 8, 'r');
  cst(z, 8, 'b');  cst(z, 8, 'i');  cst(z, 8, 's');  cst(z, 32, 0);
  n->ch = fld(z, M_CH, 8, 0);
  n->rate = fld(z, M_RATE, 32, 0);
  fld(z, M_BRMAX, 32, 0);  nominal = fld(z, M_BRNOM, 32, 0);
  fld(z, M_BRMIN, 32, 0);
  v = fld(z, M_BLK, 8, 0);
  n->bs0 = 1UL << (v & 15);  n->bs1 = 1UL << (v >> 4);
  fld(z, M_FRAME, 8, 0);
  FATAL_UNLESS(n->ch >= 1, "vorbis: %lu channels", (unsigned long) n->ch);
  FATAL_UNLESS(n->rate >= 1, "vorbis: sample rate 0");
  FATAL_UNLESS(n->bs0 >= 64 && n->bs1 >= n->bs0 && n->bs1 <= 8192,
               "vorbis: block sizes %lu and %lu",
               (unsigned long) n->bs0, (unsigned long) n->bs1);
  vb_ftier(z->v, n->rate, (i32) nominal, n->bs1);
}

static int comment_bit(void * ctx, u32 prob, int b) {
  io * z = ctx;
  if (!z->enc) b = rc_dec_bit_raw(z->d[S_BULK], prob);
  else rc_enc_bit_raw(z->e[S_BULK], prob, b);
  REPORT(prob, b);  return b;
}

static void comment_read(void * ctx, sz at, u8 * b, sz n) {
  io * z = ctx;
  FATAL_UNLESS(at <= z->len && n <= z->len - at, "comment: read beyond packet");
  memcpy(b, z->b + at, n);
}

static void comment_write(void * ctx, const u8 * b, sz n) {
  io * z = ctx;
  memcpy(z->b + z->pos / 8, b, n);  z->pos += n * 8;
}

static void comment(io * z) {
  cmt_io c;
  c.enc = z->enc;  c.input = z;  c.read = comment_read;
  c.coder = z;  c.bit = comment_bit;
  c.output = z;  c.write = comment_write;
  cmt_code(&c, z->len, 0);
  z->pos = z->len * 8;
}

static void su_check(vb_setup * s) {
  u32 i, j;
  FATAL_UNLESS(s->nfl <= VB_MAXFLOOR, "vorbis: %lu floors, limit %d",
               (unsigned long) s->nfl, VB_MAXFLOOR);
  Fi(s->nfl, Fj(VB_MAXCLASS,
    u32 k;
    FATAL_UNLESS(s->fl[i].cbook[j] < (i32) s->nbk,
                 "vorbis: floor master book %ld", (long) s->fl[i].cbook[j]);
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
  Fi(s->nmd,
    FATAL_UNLESS(s->mdmap[i] < s->nmp, "vorbis: mode %lu names mapping %d",
                 (unsigned long) i, s->mdmap[i]));
  Fi(s->nmp, Fj(s->mp[i].sub,
    FATAL_UNLESS(s->mp[i].fl[j] < s->nfl, "vorbis: submap floor %d",
                 s->mp[i].fl[j]);
    FATAL_UNLESS(s->mp[i].rs[j] < s->nrs, "vorbis: submap residue %d",
                 s->mp[i].rs[j])));
}

static void setup(io * z) {
  vb_ctx * v = z->v;
  vb_setup * s;
  sz i;
  int whole = 0, rest = 0;
  vd_prepare();  whole = vd_pick(z, 0);
  if (whole) vd_quiet(z, 1);
  cst(z, 8, 5);  cst(z, 8, 'v');  cst(z, 8, 'o');  cst(z, 8, 'r');
  cst(z, 8, 'b');  cst(z, 8, 'i');  cst(z, 8, 's');
  Fi(ARRAY_LEN(RST), mdl_reset(z->v->m + RST[i]));
  z->v->pb = 0;
  s = xmalloc(sizeof *s);  memset(s, 0, sizeof *s);
  v->su = xrealloc(v->su, (v->nsu + 1) * sizeof *v->su);
  v->su[v->nsu] = s;  v->csu = v->nsu++;  v->cur = s;
  codebooks(z);
  if (!whole && (rest = vd_pick(z, 1)) != 0) vd_quiet(z, 1);
  floors(z);  residues(z);  mappings(z, z->v->i.ch);
  if (whole || rest) vd_quiet(z, 0);
  su_check(s);
}

static void hdr(io * z, int which) {
  switch (which) {
    case 0: ident(z);  break;
    case 1: comment(z);  break;
    default: setup(z);  break;
  }
  FATAL_UNLESS(z->len * 8 - z->pos < 8, "vorbis: %lu unparsed packet bits",
               (unsigned long) (z->len * 8 - z->pos));
  if (z->enc)
    FATAL_UNLESS(bget(z, (int) (z->len * 8 - z->pos)) == 0,
                 "vorbis: nonzero packet padding");
}

static void ioinit(io * z, vb_ctx * v, int enc, u8 * pkt, sz len) {
  sz i;
  z->enc = enc;  z->v = v;  z->b = pkt;  z->len = len;  z->pos = 0;
  z->probe = z->rawpkt = z->replay = z->choices = z->acmh = z->quiet =
    z->open = z->zero = 0;
  z->symbol = 0;
  z->cmap = NULL;  z->rch = 0;
  Fi(3, z->e[i] = NULL;  z->d[i] = NULL);
}

void vb_hdr_enc(vb_ctx * v, rc_enc * e, int which, const u8 * pkt, sz len) {
  io z;
  ioinit(&z, v, 1, (u8 *) pkt, len);  z.e[S_BULK] = e;
  if (v->acm && which != 1) { z.acmh = 1;  vba_bind(v->acm, vba_code, &z); }
  hdr(&z, which);
}

void vb_hdr_dec(vb_ctx * v, rc_dec * d, int which, u8 * pkt, sz len) {
  io z;
  ioinit(&z, v, 0, pkt, len);  z.d[S_BULK] = d;
  if (v->acm && which != 1) { z.acmh = 1;  vba_bind(v->acm, vba_code, &z); }
  memset(pkt, 0, len);
  hdr(&z, which);
}
