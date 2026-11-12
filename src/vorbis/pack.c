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

#include "vorbis/vorbis.h"
#include "fmt/arc.h"
#include "fmt/ogg.h"
#include "vorbis/vb.h"

#define C_CONT  0
#define C_SEL   2
#define C_REC   6
#define C_DUP   10
#define C_KEEP  14
#define C_NPROB 32

static const mdl_cfg BACKCFG = { 5, 0, 0, 2, 1, 0 };

#define MAXPAY  (OGG_MAXSEG * OGG_MAXSEG)

typedef struct {
  sz off, len, body;
  u32 serial, first;
  int np, type, tail, header;
  int skip;
} pg_t;

typedef struct {
  int a, z;
  u32 serial;
  int rep;
  int hdup;
  int rec, keep;
  int setup;
} lnk_t;

static u32 pkt_samples(const vb_ctx * v, const u8 * pk) {
  int d;
  sz j;
  u32 md = 0;
  if (pk[0] & 1) return 0;
  FATAL_UNLESS(v->cur != NULL, "vorbis: audio before setup");
  d = (int) blr_ilog(v->cur->nmd - 1);
  Fj((sz) d, md |= (u32) (pk[(1 + j) >> 3] >> ((1 + j) & 7) & 1) << j);
  FATAL_UNLESS(md < v->cur->nmd, "vorbis: mode %lu of %lu",
               (unsigned long) md, (unsigned long) v->cur->nmd);
  return vb_half(v, md);
}

static u32 samples(const vb_ctx * v, const ogg_page * p, const u8 * body,
                   u32 * carry) {
  int i, frag = p->np > 0 && p->type & 1;
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

static int allhdr(const ogg_page * p, const u8 * body) {
  int i;
  sz at = 0;
  if (p->np == 0 || p->tail || p->type & 1) return 0;
  Fi(p->np,
    if (!p->plen[i] || !(body[at] & 1)) return 0;
    at += p->plen[i]);
  return 1;
}

#define PAGE_BYTES (OGG_HDRMIN + OGG_MAXSEG + MAXPAY)
static sz join_len(const pg_t * pg, int i, int z, sz pl) {
  sz total = pl;
  int k;
  for (k = i + 1; k <= z; k++) {
    FATAL_UNLESS(pg[k].np > 0 && pg[k].type & 1,
                 "page %d continuation is missing", i);
    total += pg[k].first;
    if (!(pg[k].np == 1 && pg[k].tail)) return total;
  }
  FATAL("continued packet exceeds its link");
  return 0;
}

static void join_pkt(blr_file * input, const pg_t * pg, int i, int z, sz off,
                     sz pl, sz total, u8 * dst) {
  sz n = pl;
  int k;
  bf_read(input, pg[i].body + off, dst, pl);
  for (k = i + 1; n < total; k++) {
    sz t;
    FATAL_UNLESS(k <= z, "continued packet exceeds its link");
    t = pg[k].first;
    if (t > total - n) t = total - n;
    bf_read(input, pg[k].body, dst + n, t);  n += t;
  }
}

static void replay(blr_file * o, sz off, sz len, u32 serial) {
  u8 * buf = xmalloc(BLR_IO_CHUNK);
  sz at = 0;
  ogg_page q;
  while (at < len) {
    sz take = MIN(BLR_IO_CHUNK, len - at), used = 0, got;
    bf_read(o, off + at, buf, take);
    while (used < take) {
      u8 * page = buf + used;
      got = ogg_parse(&q, page, take - used);
      if (!got) break;
      wr32le(page + 14, serial);
      ogg_crc_set(page, got);
      used += got;
    }
    FATAL_UNLESS(used, "replayed run is not page-aligned");
    bf_write(o, o->len, buf, used);
    at += used;
  }
  xfree(buf);
}

static int samerun(blr_file * input, sz x, sz y, sz n) {
  u8 * a = xmalloc(MIN(n, BLR_IO_CHUNK)), * b = xmalloc(MIN(n, BLR_IO_CHUNK));
  sz at = 0;
  ogg_page q;
  int same = 1;
  while (at < n && same) {
    sz take = MIN(BLR_IO_CHUNK, n - at), used = 0, got, i;
    bf_read(input, x + at, a, take);  bf_read(input, y + at, b, take);
    while (used < take && same) {
      got = ogg_parse(&q, a + used, take - used);
      if (!got) break;
      Fi(got, if ((i < 14 || (i >= 18 && i < 22) || i >= 26) &&
                   a[used + i] != b[used + i]) { same = 0;  break; });
      used += got;
    }
    if (!used) same = 0;
    at += used;
  }
  xfree(a);  xfree(b);
  return same;
}

static void runof(const pg_t * pg, const lnk_t * l, sz * off, sz * len) {
  *off = pg[l->a].off;
  *len = pg[l->z].off + pg[l->z].len - *off;
}

static int oneserial(const pg_t * pg, const lnk_t * l) {
  int i;
  for (i = l->a; i <= l->z; i++) if (pg[i].serial != l->serial) return 0;
  return 1;
}

static int hdrrun(const pg_t * pg, const lnk_t * l, sz * off, sz * len) {
  int i, n = 0;
  sz beg = 0, end = 0;
  for (i = l->a + 1; i <= l->z && pg[i].header; i++) {
    if (!n++) beg = pg[i].off;
    end = pg[i].off + pg[i].len;
  }
  *off = beg;  *len = end - beg;
  return n;
}

#include "vorbis/cfg.c"

#define VC_HIRATE 64000
#define VC_XRATE 112000

int vb_classify(const char * in) {
  u8 b[OGG_HDRMIN + OGG_MAXSEG + 30];
  const u8 * h;
  sz n;
  i32 nominal;
  int at, ch;
  FILE * f = fopen(in, "rb");
  if (!f) return VC_MULTI;
  n = fread(b, 1, sizeof b, f);
  fclose(f);
  if (n < OGG_HDRMIN || memcmp(b, "OggS", 4)) return VC_MULTI;
  at = OGG_HDRMIN + b[26];
  if ((sz) at + 30 > n || memcmp(b + at, "\001vorbis", 7)) return VC_MULTI;
  h = b + at;  ch = h[11];
  nominal = (i32) rd32le(h + 20);
  if (ch == 7 || ch == 8) return VC_SURR;
  if (ch > 1 && nominal >= (i32) VC_XRATE * ch) return VC_MULTI_X;
  return (ch == 1 ? VC_MONO : VC_MULTI) + (nominal >= (i32) VC_HIRATE * ch);
}

static void enc_link(vb_ctx * v, ogg_hdr * h, archive * s, const pg_t * pg,
                     const lnk_t * l, blr_file * input) {
  rc_enc et, em, eb;
  u8 * page = xmalloc(PAGE_BYTES);
  u8 * jb = NULL;
  u8 df[OGG_MAXSEG];
  sz jc = 0, spill = 0;
  u32 carry = 0;
  int i, j, w = 0, cont = 0;

  rc_enc_file(&et, arc_newstream(s));
  rc_enc_file(&em, arc_newstream(s));
  rc_enc_file(&eb, arc_newstream(s));
  vb_link(v);  ogg_hdr_reset(h);
  if (l->setup >= 0) vb_use(v, (u32) l->setup);
  for (i = l->a; i <= l->z; i++) {
    sz at = 0;
    ogg_page p;
    const u8 * pagebody;
    FATAL_UNLESS(ogg_read(input, pg[i].off, &p, page) == pg[i].len,
                 "input page changed");
    pagebody = page + OGG_HDRMIN + p.nseg;
    { sz o = 0;
      Fj(p.np,
        df[j] = (u8) (ogg_whole(&p, j) && p.plen[j] && !(pagebody[o] & 1));
        o += p.plen[j]); }
    if (!pg[i].skip) ogg_hdr_enc(h, &em, &p, i == l->a, df);
    Fj(p.np,
      const u8 * pk = pagebody + at;
      sz pl = p.plen[j];
      sz off = at;
      at += pl;
      if (j == 0 && p.type & 1 && cont) {
        FATAL_UNLESS(pl <= spill, "page %d takes %lu of %lu remaining bytes",
                     i, (unsigned long) pl, (unsigned long) spill);
        spill -= pl;
        if (!(p.np == 1 && p.tail)) {
          FATAL_UNLESS(!spill, "page %d closes a packet %lu bytes early", i,
                       (unsigned long) spill);
          cont = 0;
        }
        continue;
      }
      if (!pl) continue;
      if (j == p.np - 1 && p.tail) {
        sz tot;
        FATAL_UNLESS(!pg[i].skip, "page %d continues a dropped run", i);
        tot = join_len(pg, i, l->z, pl);
        FATAL_UNLESS(tot - pl <= MAXPAY * (sz) OGG_MAXSEG,
                     "page %d continues a packet too far", i);
        jb = xgrow(jb, &jc, tot, 1, tot);
        join_pkt(input, pg, i, l->z, off, pl, tot, jb);
        ogg_cont_enc(h, &em, (u32) (tot - pl));
        spill = tot - pl;  cont = 1;  pk = jb;  pl = tot;
      }
      if (pk[0] & 1) {
        if (!pg[i].skip) {
          FATAL_UNLESS(w < 3, "a link has over three header packets");
          vb_hdr_enc(v, &eb, w, pk, pl);
        }
        w++;
      } else {
        FATAL_UNLESS(w == 3, "page %d has audio before the setup header", i);
        vb_aud_enc(v, &eb, &em, &et, pk, pl, p.type & 1, df[j]);
      });
    ogg_hdr_step(h, samples(v, &p, pagebody, &carry));
    blr_progress_update(pg[i].off + pg[i].len);
  }
  FATAL_UNLESS(!cont, "continued packet is %lu bytes short",
               (unsigned long) spill);
  xfree(jb);  xfree(page);
  vb_endlink(v);
  rc_enc_finish(&et);  rc_enc_finish(&em);  rc_enc_finish(&eb);
  bf_dropcache(et.file);  bf_dropcache(em.file);  bf_dropcache(eb.file);
  rc_enc_free(&et);  rc_enc_free(&em);  rc_enc_free(&eb);
}

static archive pack_once(blr_file * input, const char * in, int cfg,
                         const ogg_gaps * gs, blr_file * output) {
  vb_tune tu;
  int lev;
  sz len = input->len;
  u8 * page = xmalloc(PAGE_BYTES);
  pg_t * pg = NULL;
  lnk_t * lk = NULL;
  vb_ctx v;
  ogg_hdr h;
  model lrep, hrep;
  rc_enc e0;
  u16 cp[C_NPROB];
  archive a;
  sz at = 0, got, cap = 0, lcap = 0;
  int np = 0, nl = 0, i, j, k, eos = 1, prev = 0;
  int nrec = 0, nkeep = 0, * recl, * keepl;

  while (at < len) {
    ogg_page p;
    pg = xgrow(pg, &cap, (sz) np + 1, sizeof *pg, 64);
    got = ogg_read(input, at, &p, page);
    FATAL_UNLESS(got != 0, "%s: no Ogg page at %lu", in, (unsigned long) at);
    pg[np].off = at;  pg[np].len = got;  pg[np].skip = 0;
    pg[np].body = at + OGG_HDRMIN + p.nseg;
    pg[np].np = p.np;  pg[np].type = p.type;  pg[np].tail = p.tail;
    pg[np].first = p.np ? p.plen[0] : 0;  pg[np].serial = p.serial;
    pg[np].header = allhdr(&p, page + OGG_HDRMIN + p.nseg);
    at += got;  np++;
  }
  xfree(page);
  FATAL_UNLESS(np > 0, "%s: not an Ogg bitstream", in);
  pg = xrealloc(pg, (sz) np * sizeof *pg);

  Fk(np,
    if (pg[k].type & 2) {
      lk = xgrow(lk, &lcap, (sz) nl + 1, sizeof *lk, 8);
      lk[nl].a = lk[nl].z = (int) k;  lk[nl].serial = pg[k].serial;
      lk[nl].rep = lk[nl].hdup = -1;  lk[nl].rec = lk[nl].keep = 0;
      lk[nl].setup = -1;  nl++;  eos = 0;
    }
    FATAL_UNLESS(nl > 0, "%s: page %lu precedes any bitstream", in,
                 (unsigned long) k);
    FATAL_UNLESS(!eos, "%s: page %lu follows end of stream", in,
                 (unsigned long) k);
    lk[nl - 1].z = (int) k;
    if (pg[k].type & 4) eos = 1);

  Fi(nl,
    sz xo, xn, yo, yn;
    runof(pg, lk + i, &xo, &xn);
    if (!oneserial(pg, lk + i)) continue;
    Fj(i,
      if (lk[j].rep >= 0) continue;
      runof(pg, lk + j, &yo, &yn);
      if (xn == yn && samerun(input, xo, yo, xn)) {
        lk[i].rep = j;  lk[j].rec = 1;  break;
      });
    if (lk[i].rep >= 0) continue;
    if (hdrrun(pg, lk + i, &xo, &xn) > 0)
      Fj(i,
        if (lk[j].rep >= 0 || lk[j].hdup >= 0) continue;
        if (hdrrun(pg, lk + j, &yo, &yn) > 0 && xn == yn &&
            samerun(input, xo, yo, xn)) {
          lk[i].hdup = j;  lk[j].keep = 1;  break;
        });
    if (lk[i].hdup >= 0)
      for (k = lk[i].a + 1; k <= lk[i].z && pg[k].header; k++)
        pg[k].skip = 1);

  lev = vb_config_tune(cfg, &tu);
  arc_init(&a, 0, cfg - VB_CFG0);  a.gaps = gs->n != 0;
  a.size = input->len + ogg_gaps_len(gs);
  arc_begin(&a, output);
  mdl_adapt();
  vb_init(&v);  vb_tune_set(&v, &tu);  v.olen = a.size;  vb_level(&v, lev);
  ogg_hdr_init(&h);
  mdl_init(&lrep, &BACKCFG);  mdl_init(&hrep, &BACKCFG);
  rc_enc_file(&e0, arc_newstream(&a));
  rc_probs_init(cp, C_NPROB);
  recl = xmalloc((sz) nl * sizeof *recl);
  keepl = xmalloc((sz) nl * sizeof *keepl);

  Fi(nl,
    rc_enc_bit(&e0, cp + C_CONT + prev, 1);
    prev = 1;
    if (lk[i].rep >= 0) {
      Fj(nrec, if (recl[j] == lk[i].rep) break);
      if (j >= nrec)
        FATAL_CODE(BLR_EXIT_INTERNAL,
                   "internal: link %d replays an unrecorded run", i);
      rc_enc_bit(&e0, cp + C_SEL, 1);
      mdl_enc(&lrep, &e0, (u32) (nrec - 1 - j));
      mdl_enc(h.f + 3, &e0, lk[i].serial);
      continue;
    }
    rc_enc_bit(&e0, cp + C_SEL, 0);
    rc_enc_bit(&e0, cp + C_REC, lk[i].rec);
    rc_enc_bit(&e0, cp + C_DUP, lk[i].hdup >= 0);
    if (lk[i].hdup >= 0) {
      Fj(nkeep, if (keepl[j] == lk[i].hdup) break);
      if (j >= nkeep)
        FATAL_CODE(BLR_EXIT_INTERNAL,
                   "internal: link %d replays an unkept header", i);
      mdl_enc(&hrep, &e0, (u32) (nkeep - 1 - j));
    } else rc_enc_bit(&e0, cp + C_KEEP, lk[i].keep);
    { u32 pages = 0;
      for (k = lk[i].a; k <= lk[i].z; k++) if (!pg[k].skip) pages++;
      rc_enc_bits(&e0, pages, 32); }
    { u32 had = v.nsu;
      lk[i].setup = lk[i].hdup >= 0 ? lk[lk[i].hdup].setup : -1;
      enc_link(&v, &h, &a, pg, lk + i, input);
      if (v.nsu > had) lk[i].setup = (int) v.nsu - 1; }
    if (lk[i].rec) recl[nrec++] = i;
    if (lk[i].keep) keepl[nkeep++] = i);
  rc_enc_bit(&e0, cp + C_CONT + prev, 0);

  rc_enc_finish(&e0);
  if (gs->n) ogg_gaps_enc(gs, input->parent, arc_newstream(&a));
  arc_finish(&a);

  xfree(recl);  xfree(keepl);
  rc_enc_free(&e0);  mdl_free(&lrep);  mdl_free(&hrep);
  vb_free(&v);  ogg_hdr_free(&h);
  xfree(lk);  xfree(pg);
  return a;
}

void vb_pack(const char * in, const char * out, int cfg) {
  blr_file * raw = bf_open(in, 0), * input, * output;
  ogg_gaps gs;
  archive a;

  ogg_gaps_find(&gs, raw, 1);
  input = ogg_gaps_view(&gs, raw);
  output = bf_open(out, 1);
  blr_progress_begin(in, "encoding", input->len);
  a = pack_once(input, in, cfg, &gs, output);
  bf_resize(output, arc_size(&a));
  arc_free(&a);
  if (input != raw) bf_close(input);
  bf_close(raw);  bf_close(output);  ogg_gaps_free(&gs);
  blr_progress_end();
}

typedef struct { sz off, len;  int setup, npk; } run_t;

void vb_unpack(const char * in, const char * out) {
  sz progress_base = 0;
  u8 * pagebuf, * body, * jb = NULL, * page = xmalloc(PAGE_BYTES);
  blr_file * input = bf_open(in, 0);
  sz jc = 0, dirty = 0, crec = 0, ckeep = 0;
  archive a;
  rc_dec d0;
  u16 cp[C_NPROB];
  vb_ctx v;
  ogg_hdr h;
  model lrep, hrep;
  blr_file * ob;
  run_t * rec = NULL, * keep = NULL;
  sz len, si = 1, gap;
  int prev = 0, nrec = 0, nkeep = 0;

  len = input->len;
  arc_read(&a, input);
  blr_progress_begin(in, "decoding", len);
  FATAL_UNLESS(!a.opus, "%s: not a Vorbis archive", in);
  gap = a.gaps;
  FATAL_UNLESS(a.n >= 1 + gap && (a.n - gap) % 3 == 1,
               "%s: %lu streams, expected %d + 3k", in, (unsigned long) a.n,
               (int) (1 + gap));
  rc_dec_file(&d0, a.s[0].file, a.s[0].off, a.s[0].len);
  rc_probs_init(cp, C_NPROB);
  mdl_adapt();
  vb_init(&v);  v.olen = a.size;
  { vb_tune tu;
    int lev;
    FATAL_UNLESS(cfg_find(VB_CFG0 + a.cfg) != NULL,
                 "%s: no Vorbis configuration %d", in, VB_CFG0 + a.cfg);
    lev = vb_config_tune(VB_CFG0 + a.cfg, &tu);
    vb_tune_set(&v, &tu);  vb_level(&v, lev); }
  ogg_hdr_init(&h);
  mdl_init(&lrep, &BACKCFG);  mdl_init(&hrep, &BACKCFG);
  ob = bf_open(out, 1);  bf_cap(ob, (sz) a.size);
  pagebuf = xcalloc(PAGE_BYTES, 1);
  body = pagebuf + OGG_HDRMIN + OGG_MAXSEG;

  for (;;) {
    rc_dec dt, dm, db;
    ogg_page q;
    sz mark, hoff = 0, hlen = 0, jn = 0, spill = 0;
    u32 carry = 0;
    int isrec, isdup, iskeep = 0, w = 0, first = 1, done = 0, cur, r,
        cont = 0, hpk = 0, nopen;
    u8 df[OGG_MAXSEG];
    u32 ser = 0, pages = 0, had = v.nsu;

    FATAL_UNLESS(!rc_dec_spent(&d0), "%s: the control stream ran out", in);
    if (!rc_dec_bit(&d0, cp + C_CONT + prev)) break;
    prev = 1;
    if (rc_dec_bit(&d0, cp + C_SEL)) {
      u32 back = mdl_dec(&lrep, &d0);
      u32 srl = mdl_dec(h.f + 3, &d0);
      FATAL_UNLESS(back < (u32) nrec, "%s: run reference %lu of %d",
                   in, (unsigned long) back, nrec);
      r = nrec - 1 - (int) back;
      replay(ob, rec[r].off, rec[r].len, srl);
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
    } else { iskeep = rc_dec_bit(&d0, cp + C_KEEP);  cur = -1; }

    pages = (u32) rc_dec_bits(&d0, 32);
    FATAL_UNLESS(pages, "%s: a link has no coded pages", in);
    FATAL_UNLESS(si + 2 < a.n - gap, "%s: incomplete stream chain", in);
    rc_dec_file(&dt, a.s[si].file, a.s[si].off, a.s[si].len);
    rc_dec_file(&dm, a.s[si + 1].file, a.s[si + 1].off, a.s[si + 1].len);
    rc_dec_file(&db, a.s[si + 2].file, a.s[si + 2].off, a.s[si + 2].len);
    si += 3;
    vb_link(&v);  ogg_hdr_reset(&h);
    if (cur >= 0) vb_use(&v, (u32) cur);
    mark = ob->len;

    while (!done) {
      sz at = 0, pgl;
      int j;
      FATAL_UNLESS(!rc_dec_spent(&dm) && !rc_dec_spent(&db),
                   "%s: link streams end before the final page", in);
      nopen = ogg_hdr_dec(&h, &dm, &q, first, df);
      FATAL_UNLESS(nopen || q.blen <= MAXPAY,
                   "%s: a page claims %lu payload bytes", in,
                   (unsigned long) q.blen);
      memset(body, 0, nopen ? dirty : MAX(dirty, q.blen));
      Fj(q.np,
        sz pl = q.plen[j];
        if (j == 0 && q.type & 1 && cont) {
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
        if (df[j]) {
          FATAL_UNLESS(w >= 3,
                       "%s: a header packet claims to carry its length", in);
          FATAL_UNLESS(at < MAXPAY,
                       "%s: a page has no room for its packets", in);
          vb_aud_dec(&v, &db, &dm, &dt, body + at, MAXPAY - at, q.type & 1,
                     &pl);
          FATAL_UNLESS(pl > 0, "%s: an empty audio packet", in);
          q.plen[j] = (u32) pl;  at += pl;  continue;
        }
        if (!pl) continue;
        FATAL_UNLESS(pl <= MAXPAY - at,
                     "%s: a page's packets exceed its room", in);
        if (j == q.np - 1 && q.tail) {
          sz ex = ogg_cont_dec(&h, &dm), tot;
          FATAL_UNLESS(ex <= MAXPAY * (sz) OGG_MAXSEG,
                       "%s: a packet spills %lu bytes onto later pages", in,
                       (unsigned long) ex);
          tot = pl + ex;
          jb = xgrow(jb, &jc, tot, 1, tot);
          memset(jb, 0, tot);  jn = tot;
          if (w < 3) { vb_hdr_dec(&v, &db, w, jb, tot);  w++; }
          else vb_aud_dec(&v, &db, &dm, &dt, jb, tot, q.type & 1, NULL);
          memcpy(body + at, jb, pl);
          spill = ex;  cont = 1;  at += pl;  continue;
        }
        if (w < 3) { vb_hdr_dec(&v, &db, w, body + at, pl);  w++; }
        else vb_aud_dec(&v, &db, &dm, &dt, body + at, pl, q.type & 1, NULL);
        at += pl);
      if (nopen) ogg_pack(&q);
      dirty = at;
      { const u8 * pq = ogg_wrap(&q, body, &pgl);
        bf_write(ob, ob->len, pq, pgl); }
      if (first) {
        ser = q.serial;
        if (isdup) { replay(ob, hoff, hlen, ser);  w += hpk; }
        first = 0;
      }
      ogg_hdr_step(&h, samples(&v, &q, body, &carry));
      blr_progress_update(progress_base + MIN(dt.pos, dt.len)
                          + MIN(dm.pos, dm.len) + MIN(db.pos, db.len));
      done = !--pages;
      FATAL_UNLESS(!(q.type & 4) || done,
                   "%s: page count extends past end of stream", in);
    }
    FATAL_UNLESS(!cont, "%s: link ends in a continued packet", in);
    vb_endlink(&v);
    if (v.nsu > had) cur = (int) v.nsu - 1;
    progress_base += dt.len + dm.len + db.len;
    bf_dropcache(dt.file);  bf_dropcache(dm.file);  bf_dropcache(db.file);
    rc_dec_free(&dt);  rc_dec_free(&dm);  rc_dec_free(&db);

    if (isrec) {
      rec = xgrow(rec, &crec, (sz) nrec + 1, sizeof *rec, 16);
      rec[nrec].off = mark;  rec[nrec].len = ob->len - mark;
      rec[nrec].setup = cur;  nrec++;
    }
    if (iskeep) {
      sz o2, e2, got;
      int npk = 0;
      ogg_page t;
      got = ogg_read(ob, mark, &t, page);
      FATAL_UNLESS(got != 0, "%s: cannot reparse first link page", in);
      o2 = e2 = mark + got;
      while (e2 < ob->len) {
        got = ogg_read(ob, e2, &t, page);
        if (!got || !allhdr(&t, page + OGG_HDRMIN + t.nseg)) break;
        e2 += got;  npk += t.np;
      }
      keep = xgrow(keep, &ckeep, (sz) nkeep + 1, sizeof *keep, 16);
      keep[nkeep].off = o2;  keep[nkeep].len = e2 - o2;
      keep[nkeep].setup = cur;  keep[nkeep].npk = npk;  nkeep++;
    }
  }
  FATAL_UNLESS(si == a.n - gap, "%s: %lu unused streams", in,
               (unsigned long) (a.n - gap - si));
  if (gap)
    ogg_gaps_dec(ob, a.s[a.n - 1].file, a.s[a.n - 1].off, a.s[a.n - 1].len);
  FATAL_UNLESS(ob->len == a.size,
               "%s: %lu bytes decoded, the archive records %lu", in,
               (unsigned long) ob->len, (unsigned long) a.size);
  bf_close(ob);  ob = NULL;
  blr_progress_end();
  xfree(rec);  xfree(keep);  bf_close(ob);  xfree(pagebuf);  xfree(jb);
  xfree(page);
  mdl_free(&lrep);  mdl_free(&hrep);
  vb_free(&v);  ogg_hdr_free(&h);
  rc_dec_free(&d0);  arc_free(&a);  bf_close(input);
}
