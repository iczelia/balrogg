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

/*  Round-trip each Vorbis layer with fresh models over real files.  */

#include "t_harness.h"
#include "ogg.h"
#include "vorbis.h"

#define MAXLINK  64
#define MAXPAY   (OGG_MAXSEG * OGG_MAXSEG)

typedef struct {
  ogg_page p;
  const u8 * body;
  sz off, len;
  int first, skip, setup;
  u32 samples;
} pinfo;

typedef struct {
  u8 * buf;  sz len;
  pinfo * pg;  int n;
} parsed;

/*  Read packet type and two-mode block size before the audio layer runs.  */

static u32 pagesamples(const pinfo * q, u32 b0, u32 b1) {
  int i;
  sz at = 0;
  u32 s = 0;
  for (i = 0; i < q->p.np; i++) {
    u8 f = q->p.plen[i] ? q->body[at] : 1;
    at += q->p.plen[i];
    if (!(f & 1)) s += (((f >> 1) & 1) ? b1 : b0) / 2;
  }
  return s;
}

static int isheader(const pinfo * q) {
  return q->p.np > 0 && q->p.plen[0] > 0 && (q->body[0] & 1);
}

/*  Parse pages and links using codec.c's header-repeat rule.  */
static int parse(const char * path, parsed * f) {
  u8 * key[MAXLINK];
  sz klen[MAXLINK], kset[MAXLINK], nset = 0, at = 0, got, kl, cap = 64;
  int n = 0, i, j, m, nkey = 0, eos = 1;
  u8 * k;

  f->buf = slurp(path, &f->len);
  f->pg = xmalloc(cap * sizeof *f->pg);
  while (at < f->len) {
    pinfo * q;
    if ((sz) n == cap) { cap *= 2;  f->pg = xrealloc(f->pg, cap * sizeof *f->pg); }
    q = f->pg + n;
    got = ogg_parse(&q->p, f->buf + at, f->len - at);
    if (!got) { CHECK(0, "%s: no Ogg page at %lu", path, (unsigned long) at);  return 0; }
    q->off = at;  q->len = got;  q->first = q->skip = q->setup = 0;
    q->samples = 0;  q->body = f->buf + at + OGG_HDRMIN + q->p.nseg;
    at += got;  n++;
  }
  f->n = n;
  for (i = 0; i < n; i++) {
    if ((f->pg[i].p.type & 2) && eos) { f->pg[i].first = 1;  eos = 0; }
    if (f->pg[i].p.type & 4) eos = 1;
  }
  if (!n || !f->pg[0].first) { CHECK(0, "%s: does not start a bitstream", path);  return 0; }

  for (i = 0; i < n; i = j) {
    int dup = -1;
    u32 b0, b1;
    for (j = i + 1; j < n && !f->pg[j].first; j++) ;
    if (f->pg[i].p.blen <= 28) { CHECK(0, "%s: link at page %d has no identification header", path, i);  return 0; }
    b0 = 1UL << (f->pg[i].body[28] & 15);  b1 = 1UL << (f->pg[i].body[28] >> 4);
    kl = 0;
    for (m = i; m < j; m++) if (isheader(f->pg + m)) kl += f->pg[m].p.blen;
    k = xmalloc(kl + 1);  kl = 0;
    for (m = i; m < j; m++)
      if (isheader(f->pg + m)) { memcpy(k + kl, f->pg[m].body, f->pg[m].p.blen);  kl += f->pg[m].p.blen; }
    for (m = 0; m < nkey; m++)
      if (klen[m] == kl && !memcmp(key[m], k, kl)) { dup = m;  break; }
    if (nkey >= MAXLINK) { CHECK(0, "%s: over %d links", path, MAXLINK);  free(k);  return 0; }
    key[nkey] = k;  klen[nkey] = kl;
    /*  Point repeated headers to their prior setup. Zero means a new setup.  */
    kset[nkey] = dup >= 0 ? kset[dup] : nset++;
    f->pg[i].setup = dup >= 0 ? (int) kset[nkey] + 1 : 0;
    nkey++;
    for (m = i; m < j; m++) {
      f->pg[m].samples = pagesamples(f->pg + m, b0, b1);
      if (dup >= 0 && m != i && isheader(f->pg + m)) f->pg[m].skip = 1;
    }
  }
  for (i = 0; i < nkey; i++) free(key[i]);
  return 1;
}

static void unparse(parsed * f) { free(f->pg);  free(f->buf); }

/*  Rebuild each page's lacing and CRC, then require the original bytes.  */
static void t_frame(const char * path) {
  ogg_page p, q;
  sz len, at = 0, got, npg = 0;
  u8 * img, * buf = slurp(path, &len);
  int bad = 0;
  img = xmalloc(OGG_HDRMIN + OGG_MAXSEG + MAXPAY);
  while (at < len && !bad) {
    got = ogg_parse(&p, buf + at, len - at);
    if (!got) { bad = 1;  break; }
    q = p;  memset(q.lace, 0xAA, sizeof q.lace);  q.nseg = -1;  q.blen = 0;
    ogg_pack(&q);
    if (q.nseg != p.nseg || memcmp(q.lace, p.lace, (sz) p.nseg) || q.blen != p.blen)
      { bad = 2;  break; }
    if (ogg_emit(&q, img, buf + at + OGG_HDRMIN + p.nseg) != got
        || memcmp(img, buf + at, got)) { bad = 3;  break; }
    npg++;  at += got;
  }
  CHECK(!bad, "%s: page %lu %s", xt_basename(path), (unsigned long) npg,
        bad == 1 ? "does not parse" : bad == 2 ? "segment table not rebuilt"
                                              : "re-emitted bytes differ");
  free(img);  free(buf);
}

static void t_pages(const char * path, parsed * f) {
  ogg_hdr h;
  ogg_page q;
  rc_enc e;
  rc_dec d;
  sz olen, got;
  u8 * img;
  int i, bad = 0, nc = 0;

  ogg_hdr_init(&h);  rc_enc_init(&e);
  for (i = 0; i < f->n; i++) {
    if (f->pg[i].first) ogg_hdr_reset(&h);
    if (!f->pg[i].skip) { ogg_hdr_enc(&h, &e, &f->pg[i].p, f->pg[i].first);  nc++; }
    ogg_hdr_step(&h, f->pg[i].samples);
  }
  olen = rc_enc_finish(&e);

  ogg_hdr_free(&h);  ogg_hdr_init(&h);
  rc_dec_init(&d, rc_enc_data(&e), olen);
  img = xmalloc(OGG_HDRMIN + OGG_MAXSEG + MAXPAY);
  for (i = 0; i < f->n && !bad; i++) {
    if (f->pg[i].first) ogg_hdr_reset(&h);
    if (!f->pg[i].skip) {
      ogg_hdr_dec(&h, &d, &q, f->pg[i].first);
      got = q.blen == f->pg[i].p.blen ? ogg_emit(&q, img, f->pg[i].body) : 0;
      if (got != f->pg[i].len || memcmp(img, f->buf + f->pg[i].off, got)) bad = i + 1;
    }
    ogg_hdr_step(&h, f->pg[i].samples);
  }
  CHECK(!bad, "%s: page %d rebuilt wrong", xt_basename(path), bad - 1);
  xt_trace("%s: %d pages (%d coded) -> %lu header bytes", xt_basename(path),
           f->n, nc, (unsigned long) olen);
  ogg_hdr_free(&h);  rc_enc_free(&e);  free(img);
}

/*  Round-trip header packets through their shared models.  */
static void t_setup(const char * path, parsed * f) {
  vb_ctx v;
  rc_enc e;
  rc_dec d;
  sz olen;
  u8 * img;
  int i, j, w = 0, bad = 0, np = 0;

  vb_init(&v);  vb_level(&v, CM_NLEV - 1);  rc_enc_init(&e);
  for (i = 0; i < f->n; i++) {
    sz atp = 0;
    if (f->pg[i].first) { vb_link(&v);  w = 0; }
    if (!isheader(f->pg + i)) continue;
    if (f->pg[i].p.tail) { CHECK(0, "%s: a header packet spans two pages", path);  break; }
    for (j = 0; j < f->pg[i].p.np; j++) {
      if (!f->pg[i].skip) {
        if (w >= 3) { CHECK(0, "%s: a link has over three header packets", path);  break; }
        vb_hdr_enc(&v, &e, w, f->pg[i].body + atp, f->pg[i].p.plen[j]);  np++;
      }
      atp += f->pg[i].p.plen[j];  w++;
    }
  }
  olen = rc_enc_finish(&e);

  vb_free(&v);  vb_init(&v);  vb_level(&v, CM_NLEV - 1);
  rc_dec_init(&d, rc_enc_data(&e), olen);
  img = xmalloc(MAXPAY);
  w = 0;
  for (i = 0; i < f->n && !bad; i++) {
    sz atp = 0;
    if (f->pg[i].first) { vb_link(&v);  w = 0; }
    if (!isheader(f->pg + i)) continue;
    for (j = 0; j < f->pg[i].p.np && !bad; j++) {
      if (!f->pg[i].skip) {
        vb_hdr_dec(&v, &d, w, img, f->pg[i].p.plen[j]);
        if (memcmp(img, f->pg[i].body + atp, f->pg[i].p.plen[j])) bad = i + 1;
      }
      atp += f->pg[i].p.plen[j];  w++;
    }
  }
  CHECK(!bad, "%s: a header packet on page %d rebuilt wrong", xt_basename(path), bad - 1);
  xt_trace("%s: %d header packets -> %lu bytes", xt_basename(path), np,
           (unsigned long) olen);
  vb_free(&v);  rc_enc_free(&e);  free(img);
}

/*  Round-trip audio packets through all three per-link streams.  */
static void t_audio(const char * path, parsed * f) {
  vb_ctx v;
  ogg_hdr h;
  ogg_page q;
  rc_enc eb, em, et;
  rc_dec db, dm, dt;
  sz blen, mlen, tlen, nb, at, got;
  u8 * img;
  int i, j, w = 0, bad = 0, na = 0;

  vb_init(&v);  vb_level(&v, CM_NLEV - 1);  ogg_hdr_init(&h);
  rc_enc_init(&eb);  rc_enc_init(&em);  rc_enc_init(&et);
  for (i = 0; i < f->n; i++) {
    sz atp = 0;
    if (f->pg[i].first) {
      vb_link(&v);  ogg_hdr_reset(&h);  w = 0;
      if (f->pg[i].setup) vb_use(&v, (u32) f->pg[i].setup - 1);
    }
    if (!f->pg[i].skip) ogg_hdr_enc(&h, &em, &f->pg[i].p, f->pg[i].first);
    ogg_hdr_step(&h, f->pg[i].samples);
    for (j = 0; j < f->pg[i].p.np; j++) {
      const u8 * pk = f->pg[i].body + atp;
      sz pl = f->pg[i].p.plen[j];
      atp += pl;
      if (!pl) continue;
      if (pk[0] & 1) {
        if (!f->pg[i].skip && w < 3) vb_hdr_enc(&v, &eb, w, pk, pl);
        w++;
      } else { vb_aud_enc(&v, &eb, &em, &et, pk, pl, f->pg[i].p.type & 1);  na++; }
    }
  }
  blen = rc_enc_finish(&eb);  mlen = rc_enc_finish(&em);  tlen = rc_enc_finish(&et);

  vb_free(&v);  vb_init(&v);  vb_level(&v, CM_NLEV - 1);
  ogg_hdr_free(&h);  ogg_hdr_init(&h);
  rc_dec_init(&db, rc_enc_data(&eb), blen);
  rc_dec_init(&dm, rc_enc_data(&em), mlen);
  rc_dec_init(&dt, rc_enc_data(&et), tlen);
  img = xmalloc(OGG_HDRMIN + OGG_MAXSEG + MAXPAY);
  w = 0;
  for (i = 0; i < f->n && !bad; i++) {
    sz atp = 0;
    if (f->pg[i].first) {
      vb_link(&v);  ogg_hdr_reset(&h);  w = 0;
      if (f->pg[i].setup) vb_use(&v, (u32) f->pg[i].setup - 1);
    }
    if (!f->pg[i].skip) {
      ogg_hdr_dec(&h, &dm, &q, f->pg[i].first);
      got = q.blen == f->pg[i].p.blen ? ogg_emit(&q, img, f->pg[i].body) : 0;
      if (got != f->pg[i].len || memcmp(img, f->buf + f->pg[i].off, got)) { bad = i + 1;  break; }
    }
    ogg_hdr_step(&h, f->pg[i].samples);
    for (j = 0; j < f->pg[i].p.np && !bad; j++) {
      const u8 * pk = f->pg[i].body + atp;
      sz pl = f->pg[i].p.plen[j];
      atp += pl;
      if (!pl) continue;
      if (pk[0] & 1) {
        if (!f->pg[i].skip && w < 3) vb_hdr_dec(&v, &db, w, img, pl);
        w++;  continue;
      }
      memset(img, 0, pl);
      nb = vb_aud_dec(&v, &db, &dm, &dt, img, pl, f->pg[i].p.type & 1);
      /*  Compare all consumed bits before writer padding.  */
      for (at = 0; at < nb; at++)
        if (((img[at >> 3] >> (at & 7)) & 1) != ((pk[at >> 3] >> (at & 7)) & 1))
          { bad = i + 1;  break; }
    }
  }
  CHECK(!bad, "%s: page %d rebuilt wrong", xt_basename(path), bad - 1);
  xt_trace("%s: %d audio packets -> %lu + %lu + %lu bytes", xt_basename(path),
           na, (unsigned long) blen, (unsigned long) mlen, (unsigned long) tlen);
  vb_free(&v);  ogg_hdr_free(&h);
  rc_enc_free(&eb);  rc_enc_free(&em);  rc_enc_free(&et);
  free(img);
}

void xt_run_layers(void) {
  char ** files = xt_files(".ogg"), ** p;
  if (!*files) {
    xt_section_begin("layers");
    CHECK(0, "no Vorbis fixtures: is BLR_TEST_DATA set?");
  }
  xt_section_begin("framing");
  for (p = files; *p; p++) t_frame(*p);
  for (p = files; *p; p++) {
    parsed f;
    xt_section_begin("page headers");
    if (!parse(*p, &f)) { unparse(&f);  continue; }
    t_pages(*p, &f);
    xt_section_begin("header packets");
    t_setup(*p, &f);
    xt_section_begin("audio packets");
    t_audio(*p, &f);
    unparse(&f);
  }
  xt_files_free(files);
}
