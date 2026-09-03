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

#include "codec.h"
#include "archive.h"
#include "ogg.h"
#include "vorbis.h"

/*  Link metadata and shared back-reference models.  */

#define C_CONT  0             /*  and C_CONT + 1: the link-continues bit, banked
                                  by its previous value  */
#define C_SEL   2
#define C_REC   6
#define C_DUP   10
#define C_KEEP  14
#define C_NPROB 32

/*  Back-references count backward, making the latest run zero.  */
static const mdl_cfg BACKCFG = { 5, 0, 0, 2, 1, 0 };

#define MAXPAY  (OGG_MAXSEG * OGG_MAXSEG)


typedef struct {
  ogg_page p;
  const u8 * body;
  sz off, len;
  int skip;                   /*  a header page the dedup drops  */
} pg_t;

typedef struct {
  int a, z;                   /*  page range, inclusive  */
  u32 serial;
  int rep;                    /*  replays this link whole, or -1  */
  int hdup;                   /*  reuses this link's header pages, or -1  */
  int rec, keep;              /*  record whole link or headers  */
  int setup;                  /*  which setup the audio layer runs on  */
} lnk_t;

/*  Half a mode-selected block per audio packet.  */
static u32 pkt_samples(const vb_ctx * v, const u8 * pk) {
  int d;
  sz j;
  u32 md = 0;
  if (pk[0] & 1) return 0;
  FATAL_UNLESS(v->cur != NULL, "vorbis: audio before setup");
  d = (int) blr_ilog(v->cur->nmd - 1);
  Fj((sz) d, md |= (u32) ((pk[(1 + j) >> 3] >> ((1 + j) & 7)) & 1) << j);
  FATAL_UNLESS(md < v->cur->nmd, "vorbis: mode %lu of %lu",
               (unsigned long) md, (unsigned long) v->cur->nmd);
  return (v->cur->blockflag[md] ? v->i.bs1 : v->i.bs0) / 2;
}

/*  Count samples at packet boundaries.  */
static u32 samples(const vb_ctx * v, const ogg_page * p, const u8 * body,
                   u32 * carry) {
  int i, frag = p->np > 0 && (p->type & 1);
  sz at = 0;
  u32 s = 0, tl = 0;
  Fi(p->np,
    const u8 * pk = body + at;
    sz pl = p->plen[i];
    int last = i == p->np - 1;
    at += pl;
    if (!pl || (i == 0 && frag)) continue;
    if (last && p->tail) tl = pkt_samples(v, pk);
    else s += pkt_samples(v, pk));
  if (frag && !(p->np == 1 && p->tail)) { s += *carry;  *carry = 0; }
  if (tl) *carry = tl;
  return s;
}

/*  Deduplicate pages containing only complete headers.  */
static int allhdr(const ogg_page * p, const u8 * body) {
  int i;
  sz at = 0;
  if (p->np == 0 || p->tail || (p->type & 1)) return 0;
  Fi(p->np,
    if (!p->plen[i] || !(body[at] & 1)) return 0;
    at += p->plen[i]);
  return 1;
}

static int isheader(const pg_t * q) { return allhdr(&q->p, q->body); }

/*  Total length of the packet whose first `pl` bytes end page `i`.  */
static sz join_len(const pg_t * pg, int i, int z, sz pl) {
  sz total = pl;
  int k;
  for (k = i + 1; k <= z; k++) {
    FATAL_UNLESS(pg[k].p.np > 0 && (pg[k].p.type & 1),
                 "page %d continuation is missing", i);
    total += pg[k].p.plen[0];
    if (!(pg[k].p.np == 1 && pg[k].p.tail)) return total;
  }
  FATAL("continued packet exceeds its link");
  return 0;
}

/*  Gather a continued packet into one buffer.  */
static void join_pkt(const pg_t * pg, int i, int z, sz off, sz pl, sz total,
                     u8 * dst) {
  sz n = pl;
  int k;
  memcpy(dst, pg[i].body + off, pl);
  for (k = i + 1; n < total; k++) {
    sz t;
    FATAL_UNLESS(k <= z, "continued packet exceeds its link");
    t = pg[k].p.plen[0];
    if (t > total - n) t = total - n;
    memcpy(dst + n, pg[k].body, t);  n += t;
  }
}

typedef struct { u8 * b;  sz n, cap; } obuf;

static void ob_init(obuf * o) {
  o->cap = 1 << 16;  o->n = 0;  o->b = xmalloc(o->cap);
}

static u8 * ob_room(obuf * o, sz n) {
  sz want;
  FATAL_UNLESS(n <= SIZE_MAX - o->n, "output is too large");
  want = o->n + n;
  while (want > o->cap) {
    FATAL_UNLESS(o->cap <= SIZE_MAX / 2, "output is too large");
    o->cap *= 2;  o->b = xrealloc(o->b, o->cap);
  }
  return o->b + o->n;
}

/*  Replay pages with a new serial number and CRCs.  */
static void replay(obuf * o, sz off, sz len, u32 serial) {
  u8 * d = ob_room(o, len);
  sz at = 0, got;
  ogg_page q;
  memcpy(d, o->b + off, len);
  while (at < len) {
    got = ogg_parse(&q, d + at, len - at);
    FATAL_UNLESS(got != 0, "replayed run is not page-aligned");
    d[at + 14] = (u8) serial;  d[at + 15] = (u8) (serial >> 8);
    d[at + 16] = (u8) (serial >> 16);  d[at + 17] = (u8) (serial >> 24);
    ogg_crc_set(d + at, got);
    at += got;
  }
  o->n += len;
}

/*  Compare runs while ignoring serial numbers and CRCs.  */
static int samerun(const u8 * x, const u8 * y, sz n) {
  sz at = 0, got, i;
  ogg_page q;
  while (at < n) {
    got = ogg_parse(&q, x + at, n - at);
    if (!got) return 0;
    Fi(got, if ((i < 14 || (i >= 18 && i < 22) || i >= 26) &&
                x[at + i] != y[at + i]) return 0);
    at += got;
  }
  return 1;
}

/*  Find the ranges used for link and header comparison.  */
static void runof(const pg_t * pg, const lnk_t * l, sz * off, sz * len) {
  *off = pg[l->a].off;
  *len = pg[l->z].off + pg[l->z].len - *off;
}

static int hdrrun(const pg_t * pg, const lnk_t * l, sz * off, sz * len) {
  int i, n = 0;
  sz beg = 0, end = 0;
  for (i = l->a + 1; i <= l->z && isheader(pg + i); i++) {
    if (!n++) beg = pg[i].off;
    end = pg[i].off + pg[i].len;
  }
  *off = beg;  *len = end - beg;
  return n;
}

void vb_opt_default(vb_opt * o) { o->flags = 0x09;  o->dd = o->df = 0;
                                 o->search = 0; }


/*  Encode one link. Solid mode retains model state.  */
static void enc_link(vb_ctx * v, ogg_hdr * h, archive * s, const pg_t * pg,
                     const lnk_t * l, int solid) {
  rc_enc et, em, eb;
  u8 * jb = NULL;                 /*  a straddling packet, gathered whole  */
  sz jc = 0, spill = 0;           /*  buffer capacity and remaining bytes  */
  sz n;
  u32 carry = 0;                  /*  samples of the packet being handed on  */
  int i, j, w = 0, cont = 0;      /*  a packet is still being handed on  */

  rc_enc_init(&et);  rc_enc_init(&em);  rc_enc_init(&eb);
  if (!solid) { vb_reset(v);  ogg_hdr_free(h);  ogg_hdr_init(h); }
  vb_link(v);  ogg_hdr_reset(h);
  if (l->hdup >= 0) vb_use(v, (u32) l->setup);
  for (i = l->a; i <= l->z; i++) {
    sz at = 0;
    if (!pg[i].skip) ogg_hdr_enc(h, &em, &pg[i].p, i == l->a);
    Fj(pg[i].p.np,
      const u8 * pk = pg[i].body + at;
      sz pl = pg[i].p.plen[j];
      sz off = at;
      at += pl;
      /*  Skip fragments already coded where their packet started. `cont`
          also covers a zero-length closing fragment.  */
      if (j == 0 && (pg[i].p.type & 1) && cont) {
        FATAL_UNLESS(pl <= spill, "page %d takes %lu of %lu remaining bytes",
                     i, (unsigned long) pl, (unsigned long) spill);
        spill -= pl;
        if (!(pg[i].p.np == 1 && pg[i].p.tail)) {
          FATAL_UNLESS(!spill, "page %d closes a packet %lu bytes early", i,
                       (unsigned long) spill);
          cont = 0;
        }
        continue;
      }
      FATAL_UNLESS(pl > 0, "page %d carries an empty packet", i);
      if (j == pg[i].p.np - 1 && pg[i].p.tail) {
        sz tot;
        FATAL_UNLESS(!pg[i].skip, "page %d continues a dropped run", i);
        tot = join_len(pg, i, l->z, pl);
        FATAL_UNLESS(tot - pl <= MAXPAY * (sz) OGG_MAXSEG,
                     "page %d continues a packet too far", i);
        if (jc < tot) { free(jb);  jb = xmalloc(tot);  jc = tot; }
        join_pkt(pg, i, l->z, off, pl, tot, jb);
        ogg_cont_enc(h, &em, (u32) (tot - pl));
        spill = tot - pl;  cont = 1;  pk = jb;  pl = tot;
      }
      if (pk[0] & 1) {
        if (!pg[i].skip) {
          FATAL_UNLESS(w < 3, "a link has over three header packets");
          vb_hdr_enc(v, &eb, w, pk, pl);
        }
        w++;
      } else vb_aud_enc(v, &eb, &em, &et, pk, pl, pg[i].p.type & 1));
    ogg_hdr_step(h, samples(v, &pg[i].p, pg[i].body, &carry));
  }
  FATAL_UNLESS(!cont, "continued packet is %lu bytes short",
               (unsigned long) spill);
  free(jb);
  vb_endlink(v);
  n = rc_enc_finish(&et);  arc_push(s, rc_enc_data(&et), n);
  n = rc_enc_finish(&em);  arc_push(s, rc_enc_data(&em), n);
  n = rc_enc_finish(&eb);  arc_push(s, rc_enc_data(&eb), n);
  rc_enc_free(&et);  rc_enc_free(&em);  rc_enc_free(&eb);
}

/*  Encode an in-memory file under one tune and return the archive image.  */
static u8 * pack_once(const u8 * buf, sz len, const char * in,
                      const vb_opt * o, const vb_tune * tu, sz * alen) {
  u8 * arc;
  pg_t * pg;
  lnk_t * lk;
  vb_ctx v;
  ogg_hdr h;
  model lrep, hrep;
  rc_enc e0;
  u16 cp[C_NPROB];
  archive a, s;
  sz at = 0, got, n, i;
  int np = 0, cap = 64, nl = 0, l, m, eos = 1, prev = 0, nset = 0;
  int nrec = 0, nkeep = 0, * recl, * keepl;

  pg = xmalloc((sz) cap * sizeof *pg);
  while (at < len) {
    if (np == cap) { cap *= 2;  pg = xrealloc(pg, (sz) cap * sizeof *pg); }
    got = ogg_parse(&pg[np].p, buf + at, len - at);
    FATAL_UNLESS(got != 0, "%s: no Ogg page at %lu", in, (unsigned long) at);
    /*  CRCs must be valid because decoding recomputes them.  */
    { const u8 * ph = buf + at;
      FATAL_UNLESS(ogg_crc_ok(ph, got),
                   "%s: the page at %lu has a bad CRC", in, (unsigned long) at); }
    pg[np].off = at;  pg[np].len = got;  pg[np].skip = 0;
    pg[np].body = buf + at + OGG_HDRMIN + pg[np].p.nseg;
    at += got;  np++;
  }
  FATAL_UNLESS(np > 0, "%s: not an Ogg bitstream", in);

  /*  BOS starts a link only after EOS.  */
  lk = xmalloc((sz) np * sizeof *lk);
  Fi((sz) np,
    if ((pg[i].p.type & 2) && eos) {
      lk[nl].a = lk[nl].z = (int) i;  lk[nl].serial = pg[i].p.serial;
      lk[nl].rep = lk[nl].hdup = -1;  lk[nl].rec = lk[nl].keep = 0;
      lk[nl].setup = -1;  nl++;  eos = 0;
    }
    FATAL_UNLESS(nl > 0, "%s: page %lu precedes any bitstream", in,
                 (unsigned long) i);
    /*  Pages after EOS must open another link.  */
    FATAL_UNLESS(!eos, "%s: page %lu follows end of stream", in,
                 (unsigned long) i);
    lk[nl - 1].z = (int) i;
    if (pg[i].p.type & 4) eos = 1);
  FATAL_UNLESS(eos, "%s: final bitstream has no end-of-stream page", in);

  /*  Find whole-link and header-page repeats in one pass.  */
  for (l = 0; l < nl; l++) {
    sz xo, xn, yo, yn;
    runof(pg, lk + l, &xo, &xn);
    if (!o->dd)
      for (m = 0; m < l; m++) {
        if (lk[m].rep >= 0) continue;
        runof(pg, lk + m, &yo, &yn);
        if (xn == yn && samerun(buf + xo, buf + yo, xn)) {
          lk[l].rep = m;  lk[m].rec = 1;  break;
        }
      }
    if (lk[l].rep >= 0) continue;
    if (!o->df && hdrrun(pg, lk + l, &xo, &xn) > 0)
      for (m = 0; m < l; m++) {
        if (lk[m].rep >= 0 || lk[m].hdup >= 0) continue;
        if (hdrrun(pg, lk + m, &yo, &yn) > 0 && xn == yn &&
            samerun(buf + xo, buf + yo, xn)) {
          lk[l].hdup = m;  lk[m].keep = 1;  break;
        }
      }
    if (lk[l].hdup >= 0) {
      int k;
      lk[l].setup = lk[lk[l].hdup].setup;
      for (k = lk[l].a + 1; k <= lk[l].z && isheader(pg + k); k++)
        pg[k].skip = 1;
    } else lk[l].setup = nset++;
  }

  arc_init(&a, o->flags);  arc_init(&s, o->flags);
  /*  Omit the tune blob when all values are default.  */
  { vb_tune df;
    vb_tune_default(&df);
    if (tu->alim != df.alim || tu->lr != df.lr || tu->flags != df.flags) {
      a.ntune = VB_TUNE_LEN;  vb_tune_put(tu, a.tune);
    } }
  mdl_adapt();
  vb_init(&v);  vb_tune_set(&v, tu);  vb_level(&v, (int) ARC_LEVEL(o->flags));
  vb_slots(&v, (u32) 1 << ((o->flags & 7) + 4));  ogg_hdr_init(&h);
  mdl_init(&lrep, &BACKCFG);  mdl_init(&hrep, &BACKCFG);
  rc_enc_init(&e0);
  rc_probs_init(cp, C_NPROB);
  recl = xmalloc((sz) nl * sizeof *recl);
  keepl = xmalloc((sz) nl * sizeof *keepl);

  for (l = 0; l < nl; l++) {
    rc_enc_bit(&e0, cp + C_CONT + prev, 1);
    prev = 1;
    if (lk[l].rep >= 0) {
      for (m = 0; m < nrec && recl[m] != lk[l].rep; m++) ;
      if (m >= nrec)
        FATAL_CODE(BLR_EXIT_INTERNAL, "internal: link %d replays an unrecorded run", l);
      rc_enc_bit(&e0, cp + C_SEL, 1);
      mdl_enc(&lrep, &e0, (u32) (nrec - 1 - m));
      mdl_enc(h.f + 3, &e0, lk[l].serial);
      continue;
    }
    rc_enc_bit(&e0, cp + C_SEL, 0);
    rc_enc_bit(&e0, cp + C_REC, lk[l].rec);
    rc_enc_bit(&e0, cp + C_DUP, lk[l].hdup >= 0);
    if (lk[l].hdup >= 0) {
      for (m = 0; m < nkeep && keepl[m] != lk[l].hdup; m++) ;
      if (m >= nkeep)
        FATAL_CODE(BLR_EXIT_INTERNAL, "internal: link %d replays an unkept header", l);
      mdl_enc(&hrep, &e0, (u32) (nkeep - 1 - m));
    } else rc_enc_bit(&e0, cp + C_KEEP, lk[l].keep);
    enc_link(&v, &h, &s, pg, lk + l, ARC_SOLID(o->flags) || l == 0);
    if (lk[l].rec) recl[nrec++] = l;
    if (lk[l].keep) keepl[nkeep++] = l;
  }
  rc_enc_bit(&e0, cp + C_CONT + prev, 0);

  n = rc_enc_finish(&e0);
  arc_push(&a, rc_enc_data(&e0), n);
  Fi(s.n, arc_push(&a, s.s[i].data, s.s[i].len));
  arc = arc_emit(&a, alen);

  free(recl);  free(keepl);
  rc_enc_free(&e0);  mdl_free(&lrep);  mdl_free(&hrep);
  arc_free(&a);  arc_free(&s);  vb_free(&v);  ogg_hdr_free(&h);
  free(lk);  free(pg);
  return arc;
}

/*  Tune candidates, the most often smallest first, so a short search
    still finds a good one.  */
static const u8 TRY_ALIM[] = { 40, 200, 61, 255, 90, 25, 150 };
static const u8 TRY_LR[]   = { 10, 5, 3 };
#define NALIM ((int) (sizeof TRY_ALIM))
#define NLR   ((int) (sizeof TRY_LR))

typedef struct {
  const u8 * buf;  sz len;  const char * in;  const vb_opt * o;
  u8 * best;  sz blen;  vb_tune bt;
  int left;                   /*  trial encodes still affordable  */
} search;

/*  Keep a tune if it helps. Return 0 when the budget is spent.  */
static int trial(search * s, const vb_tune * t) {
  sz alen;
  u8 * arc;
  if (s->left <= 0) return 0;
  s->left--;
  arc = pack_once(s->buf, s->len, s->in, s->o, t, &alen);
  if (!s->best || alen < s->blen) {
    free(s->best);  s->best = arc;  s->blen = alen;  s->bt = *t;
  } else free(arc);
  return 1;
}

void vb_pack(const char * in, const char * out, const vb_opt * o) {
  u8 * buf;
  sz len;
  search s;
  vb_tune t;
  int k;

  buf = slurp(in, &len);
  s.buf = buf;  s.len = len;  s.in = in;  s.o = o;
  s.best = NULL;  s.blen = 0;  s.left = o->search + 1;
  vb_tune_default(&t);
  trial(&s, &t);                        /*  the default, always  */

  /*  Start each axis from the current best tune.  */
  t = s.bt;  t.flags |= VB_TF_CLS;
  trial(&s, &t);
  t = s.bt;  t.flags |= VB_TF_MATCH;
  trial(&s, &t);
  Fk(NALIM,
    t = s.bt;  t.alim = TRY_ALIM[k];
    if (!trial(&s, &t)) break);
  Fk(NLR,
    t = s.bt;  t.lr = TRY_LR[k];
    if (!trial(&s, &t)) break);

  spew(out, s.best, s.blen);
  free(s.best);  free(buf);
}


/*  A recorded run stores an output byte range and its setup index.  */
typedef struct { sz off, len;  int setup, npk; } run_t;

void vb_unpack(const char * in, const char * out) {
  u8 * buf, * body, * jb = NULL;
  sz jc = 0;                      /*  the straddling-packet buffer  */
  archive a;
  rc_dec d0;
  u16 cp[C_NPROB];
  vb_ctx v;
  ogg_hdr h;
  model lrep, hrep;
  obuf ob;
  run_t * rec, * keep;
  sz len, si = 1;
  int prev = 0, nrec = 0, nkeep = 0, nset = 0, crec = 16, ckeep = 16;

  buf = slurp(in, &len);
  arc_parse(&a, buf, len);
  FATAL_UNLESS(!(a.flags & ARC_OPUS), "%s: not a Vorbis archive", in);
  FATAL_UNLESS(a.n >= 1 && (a.n % 3) == 1,
               "%s: %lu streams, expected 1 + 3k", in, (unsigned long) a.n);
  rc_dec_init(&d0, a.s[0].data, a.s[0].len);
  rc_probs_init(cp, C_NPROB);
  mdl_adapt();
  vb_init(&v);
  { vb_tune tu;  vb_tune_get(&tu, a.tune, a.ntune);  vb_tune_set(&v, &tu); }
  vb_level(&v, (int) ARC_LEVEL(a.flags));
  vb_slots(&v, (u32) 1 << ((a.flags & 7) + 4));  ogg_hdr_init(&h);
  mdl_init(&lrep, &BACKCFG);  mdl_init(&hrep, &BACKCFG);
  ob_init(&ob);  body = xmalloc(MAXPAY);
  rec = xmalloc((sz) crec * sizeof *rec);
  keep = xmalloc((sz) ckeep * sizeof *keep);

  for (;;) {
    rc_dec dt, dm, db;
    ogg_page q;
    sz mark, hoff = 0, hlen = 0, jn = 0, spill = 0;
    u32 carry = 0;
    int isrec, isdup, iskeep = 0, w = 0, first = 1, done = 0, cur, r, cont = 0;
    int hpk = 0;                  /*  header packets the replayed pages hold  */
    u32 ser = 0;

    /*  A spent control stream could otherwise replay forever.  */
    FATAL_UNLESS(!rc_dec_spent(&d0), "%s: the control stream ran out", in);
    if (!rc_dec_bit(&d0, cp + C_CONT + prev)) break;
    prev = 1;
    if (rc_dec_bit(&d0, cp + C_SEL)) {
      u32 back = mdl_dec(&lrep, &d0);
      u32 srl = mdl_dec(h.f + 3, &d0);
      FATAL_UNLESS(back < (u32) nrec, "%s: run reference %lu of %d",
                   in, (unsigned long) back, nrec);
      r = nrec - 1 - (int) back;
      replay(&ob, rec[r].off, rec[r].len, srl);
      continue;
    }
    isrec = rc_dec_bit(&d0, cp + C_REC);
    isdup = rc_dec_bit(&d0, cp + C_DUP);
    if (isdup) {
      u32 back = mdl_dec(&hrep, &d0);
      FATAL_UNLESS(back < (u32) nkeep, "%s: header back-reference %lu of %d",
                   in, (unsigned long) back, nkeep);
      cur = nkeep - 1 - (int) back;
      hoff = keep[cur].off;  hlen = keep[cur].len;  hpk = keep[cur].npk;
      cur = keep[cur].setup;
    } else { iskeep = rc_dec_bit(&d0, cp + C_KEEP);  cur = nset++; }

    FATAL_UNLESS(si + 2 < a.n, "%s: incomplete stream chain", in);
    rc_dec_init(&dt, a.s[si].data, a.s[si].len);
    rc_dec_init(&dm, a.s[si + 1].data, a.s[si + 1].len);
    rc_dec_init(&db, a.s[si + 2].data, a.s[si + 2].len);
    si += 3;
    if (!ARC_SOLID(a.flags) && si > 4) {
      vb_reset(&v);  ogg_hdr_free(&h);  ogg_hdr_init(&h);
    }
    vb_link(&v);  ogg_hdr_reset(&h);
    if (isdup) vb_use(&v, (u32) cur);
    mark = ob.n;

    while (!done) {
      sz at = 0, pgl;
      int j;
      FATAL_UNLESS(!rc_dec_spent(&dm) && !rc_dec_spent(&db),
                   "%s: link streams end before the final page", in);
      ogg_hdr_dec(&h, &dm, &q, first);
      FATAL_UNLESS(q.blen <= MAXPAY, "%s: a page claims %lu payload bytes", in,
                   (unsigned long) q.blen);
      memset(body, 0, q.blen);
      Fj(q.np,
        sz pl = q.plen[j];
        /*  Copy a fragment from the packet decoded on an earlier page.  */
        if (j == 0 && (q.type & 1) && cont) {
          FATAL_UNLESS(pl <= spill, "%s: page takes %lu of %lu remaining bytes",
                       in, (unsigned long) pl, (unsigned long) spill);
          if (pl) memcpy(body + at, jb + jn - spill, pl);
          spill -= pl;
          if (!(q.np == 1 && q.tail)) {
            FATAL_UNLESS(!spill, "%s: a page closes a packet %lu bytes early",
                         in, (unsigned long) spill);
            cont = 0;
          }
          at += pl;  continue;
        }
        FATAL_UNLESS(pl > 0, "%s: a page claims an empty packet", in);
        if (j == q.np - 1 && q.tail) {
          sz ex = ogg_cont_dec(&h, &dm), tot;
          FATAL_UNLESS(ex <= MAXPAY * (sz) OGG_MAXSEG,
                       "%s: a packet spills %lu bytes onto later pages", in,
                       (unsigned long) ex);
          tot = pl + ex;
          if (jc < tot) { free(jb);  jb = xmalloc(tot);  jc = tot; }
          memset(jb, 0, tot);  jn = tot;
          if (w < 3) { vb_hdr_dec(&v, &db, w, jb, tot);  w++; }
          else vb_aud_dec(&v, &db, &dm, &dt, jb, tot, q.type & 1);
          memcpy(body + at, jb, pl);
          spill = ex;  cont = 1;  at += pl;  continue;
        }
        if (w < 3) { vb_hdr_dec(&v, &db, w, body + at, pl);  w++; }
        else vb_aud_dec(&v, &db, &dm, &dt, body + at, pl, q.type & 1);
        at += pl);
      pgl = ogg_emit(&q, ob_room(&ob, OGG_HDRMIN + (sz) q.nseg + q.blen), body);
      ob.n += pgl;
      if (first) {
        ser = q.serial;
        /*  Replay headers with the current serial.  */
        if (isdup) { replay(&ob, hoff, hlen, ser);  w += hpk; }
        first = 0;
      }
      ogg_hdr_step(&h, samples(&v, &q, body, &carry));
      if (q.type & 4) done = 1;
    }
    vb_endlink(&v);

    if (isrec) {
      if (nrec == crec)
        { crec *= 2;  rec = xrealloc(rec, (sz) crec * sizeof *rec); }
      rec[nrec].off = mark;  rec[nrec].len = ob.n - mark;
      rec[nrec].setup = cur;  nrec++;
    }
    if (iskeep) {
      /*  Record header pages after the first and before the first audio.  */
      sz o2, e2, got;
      int npk = 0;
      ogg_page t;
      got = ogg_parse(&t, ob.b + mark, ob.n - mark);
      FATAL_UNLESS(got != 0, "%s: cannot reparse first link page", in);
      o2 = e2 = mark + got;
      while (e2 < ob.n) {
        got = ogg_parse(&t, ob.b + e2, ob.n - e2);
        if (!got || !allhdr(&t, ob.b + e2 + OGG_HDRMIN + t.nseg)) break;
        e2 += got;  npk += t.np;
      }
      if (nkeep == ckeep) { ckeep *= 2;
        keep = xrealloc(keep, (sz) ckeep * sizeof *keep); }
      keep[nkeep].off = o2;  keep[nkeep].len = e2 - o2;
      keep[nkeep].setup = cur;  keep[nkeep].npk = npk;  nkeep++;
    }
  }
  FATAL_UNLESS(si == a.n, "%s: %lu unused streams", in,
               (unsigned long) (a.n - si));
  spew(out, ob.b, ob.n);
  free(rec);  free(keep);  free(ob.b);  free(body);  free(jb);
  mdl_free(&lrep);  mdl_free(&hrep);
  vb_free(&v);  ogg_hdr_free(&h);
  arc_free(&a);  free(buf);
}
