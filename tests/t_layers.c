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

#include "t_harness.h"
#include "fmt/ogg.h"
#include "vorbis/vorbis.h"
#include "opus/acm.h"

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

static u32 pagesamples(const pinfo * q, u32 b0, u32 b1) {
  int i;
  sz at = 0;
  u32 s = 0;
  Fi(q->p.np,
    u8 f = q->p.plen[i] ? q->body[at] : 1;
    at += q->p.plen[i];
    if (!(f & 1)) s += (f >> 1 & 1 ? b1 : b0) / 2);
  return s;
}

static int isheader(const pinfo * q) {
  return q->p.np > 0 && q->p.plen[0] > 0 && q->body[0] & 1;
}

static int parse(const char * path, parsed * f) {
  u8 * key[MAXLINK];
  sz klen[MAXLINK], kset[MAXLINK], nset = 0, at = 0, got, kl, cap = 64;
  int n = 0, i, j, k, nkey = 0;
  u8 * keybuf;

  f->buf = slurp(path, &f->len);
  f->pg = xmalloc(cap * sizeof *f->pg);
  while (at < f->len) {
    pinfo * q;
    f->pg = xgrow(f->pg, &cap, (sz) n + 1, sizeof *f->pg, cap);
    q = f->pg + n;
    got = ogg_parse(&q->p, f->buf + at, f->len - at);
    if (!got) {
      CHECK(0, "%s: no Ogg page at %lu", path, (unsigned long) at);
      return 0;
    }
    q->off = at;  q->len = got;  q->first = q->skip = q->setup = 0;
    q->samples = 0;  q->body = f->buf + at + OGG_HDRMIN + q->p.nseg;
    at += got;  n++;
  }
  f->n = n;
  Fi(n, if (f->pg[i].p.type & 2) f->pg[i].first = 1);
  if (!n || !f->pg[0].first) {
    CHECK(0, "%s: does not start a bitstream", path);  return 0;
  }

  for (i = 0; i < n; i = j) {
    int dup = -1;
    u32 b0, b1;
    for (j = i + 1; j < n && !f->pg[j].first; j++) ;
    if (f->pg[i].p.blen <= 28) {
      CHECK(0, "%s: link at page %d has no identification header", path, i);
      return 0;
    }
    b0 = 1UL << (f->pg[i].body[28] & 15);
    b1 = 1UL << (f->pg[i].body[28] >> 4);
    kl = 0;
    for (k = i; k < j; k++) if (isheader(f->pg + k)) kl += f->pg[k].p.blen;
    keybuf = xmalloc(kl + 1);  kl = 0;
    for (k = i; k < j; k++)
      if (isheader(f->pg + k)) {
        memcpy(keybuf + kl, f->pg[k].body, f->pg[k].p.blen);
        kl += f->pg[k].p.blen;
      }
    Fk(nkey,
      if (klen[k] == kl && !memcmp(key[k], keybuf, kl)) { dup = k;  break; });
    if (nkey >= MAXLINK) {
      CHECK(0, "%s: over %d links", path, MAXLINK);  xfree(keybuf);  return 0;
    }
    key[nkey] = keybuf;  klen[nkey] = kl;
    kset[nkey] = dup >= 0 ? kset[dup] : nset++;
    f->pg[i].setup = dup >= 0 ? (int) kset[nkey] + 1 : 0;
    nkey++;
    for (k = i; k < j; k++) {
      f->pg[k].samples = pagesamples(f->pg + k, b0, b1);
      if (dup >= 0 && k != i && isheader(f->pg + k)) f->pg[k].skip = 1;
    }
  }
  Fi(nkey, xfree(key[i]));
  return 1;
}

static void unparse(parsed * f) { xfree(f->pg);  xfree(f->buf); }

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
    if (q.nseg != p.nseg || memcmp(q.lace, p.lace, (sz) p.nseg) ||
        q.blen != p.blen) { bad = 2;  break; }
    if (xt_ogg_emit(&q, img, buf + at + OGG_HDRMIN + p.nseg) != got ||
        memcmp(img, buf + at, got)) { bad = 3;  break; }
    npg++;  at += got;
  }
  CHECK(!bad, "%s: page %lu %s", xt_basename(path), (unsigned long) npg,
        bad == 1 ? "does not parse" : bad == 2 ? "segment table not rebuilt"
                                              : "re-emitted bytes differ");
  xfree(img);  xfree(buf);
}

static void t_pages(const char * path, parsed * f) {
  ogg_hdr h;
  ogg_page q;
  rc_enc e;
  rc_dec d;
  sz olen, got;
  u8 * img, df[OGG_MAXSEG] = { 0 };
  int i, bad = 0, nc = 0;

  ogg_hdr_init(&h);  rc_enc_init(&e);
  Fi(f->n,
    if (f->pg[i].first) ogg_hdr_reset(&h);
    if (!f->pg[i].skip) {
      ogg_hdr_enc(&h, &e, &f->pg[i].p, f->pg[i].first, df);  nc++;
    }
    ogg_hdr_step(&h, f->pg[i].samples));
  olen = rc_enc_finish(&e);

  ogg_hdr_free(&h);  ogg_hdr_init(&h);
  rc_dec_init(&d, rc_enc_data(&e), olen);
  img = xmalloc(OGG_HDRMIN + OGG_MAXSEG + MAXPAY);
  Fi(f->n,
    if (bad) break;
    if (f->pg[i].first) ogg_hdr_reset(&h);
    if (!f->pg[i].skip) {
      ogg_hdr_dec(&h, &d, &q, f->pg[i].first, df);
      got = q.blen == f->pg[i].p.blen ? xt_ogg_emit(&q, img, f->pg[i].body)
                                      : 0;
      if (got != f->pg[i].len || memcmp(img, f->buf + f->pg[i].off, got))
        bad = i + 1;
    }
    ogg_hdr_step(&h, f->pg[i].samples));
  CHECK(!bad, "%s: page %d rebuilt wrong", xt_basename(path), bad - 1);
  xt_trace("%s: %d pages (%d coded) -> %lu header bytes", xt_basename(path),
           f->n, nc, (unsigned long) olen);
  ogg_hdr_free(&h);  rc_enc_free(&e);  xfree(img);
}

static int lookup_quotients(const vb_setup * s) {
  u32 i, j;
  Fi(s->nbk,
    const vb_book * b = s->bk + i;
    if (b->look != 1) continue;
    Fj(b->ent,
      u32 q = (u32) ((uint64_t) j * b->divmul >> b->divshift);
      if (q != j / b->nv) return 0);
    if ((u32) ((uint64_t) 0xFFFFFF * b->divmul >> b->divshift) !=
        0xFFFFFF / b->nv) return 0);
  return 1;
}

static int fz_files;

static int implied_digits(const vb_setup * s, int * settles) {
  u32 i, j, k;
  Fi(s->nbk,
    const vb_book * b = s->bk + i;
    u32 w = 1;
    if (b->look != 1) continue;
    if ((b->fzm & (VB_FZ1 | VB_FZ2)) != (VB_FZ1 | VB_FZ2)) *settles = 1;
    if (b->nv > 1) Fk(b->dim - 1, w *= b->nv);
    Fj(b->ent,
      u32 t = j, pw = 1;
      if (!b->len[j]) continue;
      Fk(b->dim,
        i32 d = (i32) b->mult[t % b->nv] - (i32) b->off;
        u8 f = b->fz ? b->fz[k * w + j % pw] : b->fzm;
        u8 need = (u8) ((d ? VB_FZNZ : VB_FZ0) |
                        (d > 0 ? VB_FZP : d < 0 ? VB_FZN : 0) |
                        (d == 1 || d == -1 ? VB_FZ1 : d ? VB_FZ2 : 0));
        if ((f & need) != need || (b->fzm & need) != need) return 0;
        if ((f & (VB_FZ0 | VB_FZNZ)) != (VB_FZ0 | VB_FZNZ) ||
            (d && (f & (VB_FZ1 | VB_FZ2)) != (VB_FZ1 | VB_FZ2)))
          *settles = 1;
        t /= b->nv;  pw *= b->nv)));
  return 1;
}

static void t_setup(const char * path, parsed * f) {
  vb_ctx v;
  rc_enc e;
  rc_dec d;
  sz olen;
  u8 * img;
  int i, j, w = 0, bad = 0, np = 0, divbad = 0, fzbad = 0, settles = 0;

  vb_init(&v);  vb_level(&v, CM_NLEV - 1);  rc_enc_init(&e);
  Fi(f->n,
    sz atp = 0;
    if (f->pg[i].first) { vb_link(&v);  w = 0; }
    if (!isheader(f->pg + i)) continue;
    if (f->pg[i].p.tail) {
      CHECK(0, "%s: a header packet spans two pages", path);  break;
    }
    Fj(f->pg[i].p.np,
      if (!f->pg[i].skip) {
        if (w >= 3) {
          CHECK(0, "%s: a link has over three header packets", path);  break;
        }
        vb_hdr_enc(&v, &e, w, f->pg[i].body + atp, f->pg[i].p.plen[j]);
        np++;
        if (w == 2 && !lookup_quotients(v.cur)) divbad = 1;
        if (w == 2 && !implied_digits(v.cur, &settles)) fzbad = 1;
      }
      atp += f->pg[i].p.plen[j];  w++));
  olen = rc_enc_finish(&e);

  vb_free(&v);  vb_init(&v);  vb_level(&v, CM_NLEV - 1);
  rc_dec_init(&d, rc_enc_data(&e), olen);
  img = xmalloc(MAXPAY);
  w = 0;
  Fi(f->n,
    if (bad) break;
    sz atp = 0;
    if (f->pg[i].first) { vb_link(&v);  w = 0; }
    if (!isheader(f->pg + i)) continue;
    Fj(f->pg[i].p.np,
      if (bad) break;
      if (!f->pg[i].skip) {
        vb_hdr_dec(&v, &d, w, img, f->pg[i].p.plen[j]);
        if (memcmp(img, f->pg[i].body + atp, f->pg[i].p.plen[j]))
          bad = i + 1;
      }
      atp += f->pg[i].p.plen[j];  w++));
  CHECK(!bad, "%s: a header packet on page %d rebuilt wrong",
        xt_basename(path), bad - 1);
  CHECK(!divbad, "%s: codebook reciprocal differs from division",
        xt_basename(path));
  CHECK(!fzbad, "%s: a codebook's implied digits exclude a used entry",
        xt_basename(path));
  fz_files += settles;
  xt_trace("%s: %d header packets -> %lu bytes", xt_basename(path), np,
           (unsigned long) olen);
  vb_free(&v);  rc_enc_free(&e);  xfree(img);
}

static sz t_audio(const char * path, parsed * f, const vb_tune * tune,
                  int level, int dl) {
  vb_ctx v;
  ogg_hdr h;
  ogg_page q;
  rc_enc eb, em, et;
  rc_dec db, dm, dt;
  sz blen, mlen, tlen, nb, k, got;
  u8 * img, * pk2, df[OGG_MAXSEG];
  int i, j, w = 0, bad = 0, na = 0, nopen = 0, nd = 0;

  vb_init(&v);  vb_tune_set(&v, tune);  vb_level(&v, level);  ogg_hdr_init(&h);
  rc_enc_init(&eb);  rc_enc_init(&em);  rc_enc_init(&et);
  Fi(f->n,
    sz atp = 0;
    if (f->pg[i].first) {
      vb_link(&v);  ogg_hdr_reset(&h);  w = 0;
      if (f->pg[i].setup) vb_use(&v, (u32) f->pg[i].setup - 1);
    }
    Fj(f->pg[i].p.np,
      df[j] = (u8) (dl && ogg_whole(&f->pg[i].p, j) && f->pg[i].p.plen[j] &&
                    !(f->pg[i].body[atp] & 1));
      nd += df[j];  atp += f->pg[i].p.plen[j]);
    atp = 0;
    if (!f->pg[i].skip) ogg_hdr_enc(&h, &em, &f->pg[i].p, f->pg[i].first, df);
    ogg_hdr_step(&h, f->pg[i].samples);
    Fj(f->pg[i].p.np,
      const u8 * pk = f->pg[i].body + atp;
      sz pl = f->pg[i].p.plen[j];
      atp += pl;
      if (!pl) continue;
      if (pk[0] & 1) {
        if (!f->pg[i].skip && w < 3) vb_hdr_enc(&v, &eb, w, pk, pl);
        w++;
      } else {
        vb_aud_enc(&v, &eb, &em, &et, pk, pl, f->pg[i].p.type & 1, df[j]);
        na++;
      }));
  blen = rc_enc_finish(&eb);  mlen = rc_enc_finish(&em);
  tlen = rc_enc_finish(&et);

  vb_free(&v);  vb_init(&v);  vb_tune_set(&v, tune);  vb_level(&v, level);
  ogg_hdr_free(&h);  ogg_hdr_init(&h);
  rc_dec_init(&db, rc_enc_data(&eb), blen);
  rc_dec_init(&dm, rc_enc_data(&em), mlen);
  rc_dec_init(&dt, rc_enc_data(&et), tlen);
  img = xmalloc(OGG_HDRMIN + OGG_MAXSEG + MAXPAY);
  pk2 = xmalloc(MAXPAY);
  w = 0;
  Fi(f->n,
    if (bad) break;
    sz atp = 0;
    if (f->pg[i].first) {
      vb_link(&v);  ogg_hdr_reset(&h);  w = 0;
      if (f->pg[i].setup) vb_use(&v, (u32) f->pg[i].setup - 1);
    }
    memset(df, 0, sizeof df);
    if (!f->pg[i].skip) {
      nopen = ogg_hdr_dec(&h, &dm, &q, f->pg[i].first, df);
      if (!nopen) {
        got = q.blen == f->pg[i].p.blen ? xt_ogg_emit(&q, img, f->pg[i].body)
                                        : 0;
        if (got != f->pg[i].len || memcmp(img, f->buf + f->pg[i].off, got)) {
          bad = i + 1;  break;
        }
      }
    }
    ogg_hdr_step(&h, f->pg[i].samples);
    Fj(f->pg[i].p.np,
      if (bad) break;
      const u8 * pk = f->pg[i].body + atp;
      sz pl = f->pg[i].p.plen[j];
      atp += pl;
      if (!pl) continue;
      if (pk[0] & 1) {
        if (!f->pg[i].skip && w < 3) vb_hdr_dec(&v, &db, w, img, pl);
        w++;  continue;
      }
      if (df[j]) {
        sz got2 = 0;
        memset(pk2, 0, MAXPAY);
        vb_aud_dec(&v, &db, &dm, &dt, pk2, MAXPAY, f->pg[i].p.type & 1,
                   &got2);
        if (got2 != pl || memcmp(pk2, pk, pl)) { bad = i + 1;  break; }
        q.plen[j] = (u32) got2;
        continue;
      }
      memset(img, 0, pl);
      nb = vb_aud_dec(&v, &db, &dm, &dt, img, pl, f->pg[i].p.type & 1, NULL);
      Fk(nb,
        if ((img[k >> 3] >> (k & 7) & 1) != (pk[k >> 3] >> (k & 7) & 1))
          { bad = i + 1;  break; }));
    if (!bad && nopen && !f->pg[i].skip) {
      ogg_pack(&q);
      got = q.blen == f->pg[i].p.blen ? xt_ogg_emit(&q, img, f->pg[i].body)
                                      : 0;
      if (got != f->pg[i].len || memcmp(img, f->buf + f->pg[i].off, got))
        bad = i + 1;
    });
  CHECK(!bad, "%s: page %d rebuilt wrong (effort %d, flags 0x%02x, "
        "extensions 0x%02x/0x%02x/0x%02x, lengths %d)", xt_basename(path),
        bad - 1, level, tune->flags, tune->x, tune->x2, tune->x3, dl);
  CHECK(!dl || nd > 0, "%s: no audio packet carries its length",
        xt_basename(path));
  xt_trace("%s: %d audio packets -> %lu + %lu + %lu bytes", xt_basename(path),
           na, (unsigned long) blen, (unsigned long) mlen,
           (unsigned long) tlen);
  vb_free(&v);  ogg_hdr_free(&h);
  rc_enc_free(&eb);  rc_enc_free(&em);  rc_enc_free(&et);
  xfree(img);  xfree(pk2);
  return blen + mlen + tlen;
}

static void t_rule_state(void) {
  vb_res r;
  vb_rule p;
  int fa, s;
  u32 k;
  memset(&r, 0, sizeof r);  memset(r.th, 0xFF, sizeof r.th);
  r.type = 2;  r.psz = 32;  r.ncl = 5;
  r.th[0][0] = r.th[0][1] = 0;  r.th[1][0] = 4096;  r.th[1][1] = 0;
  r.th[2][0] = 8192;  r.th[2][1] = 0;
  r.th[3][0] = 8192;  r.th[3][1] = 99 * 4096;
  vb_rule_begin(&p, &r, 3, 1, 64, 2);
  s = vb_rule_state(&p, 0, 4096, &fa);
  CHECK(s >= 2 && fa, "an unmet rule at the first position of the first "
        "channel (state %d)", s);
  Fk(31,
    vb_rule_state(&p, 0, 4096, &fa);
    vb_rule_put(&p, k % 2 ? 0 : 8192, fa));
  s = vb_rule_state(&p, 0, 4096, &fa);
  CHECK(s >= 2 && !fa && (s - 2) / 10 == 0 && (s - 2) % 5 == 1,
        "the last position must take one step (state %d)", s);
  vb_rule_put(&p, 4096, fa);
  vb_rule_end(&p, &r, 3);
  CHECK(r.th[3][0] == 8192 && r.th[3][1] == 99 * 4096, "class 3's bounds hold");
  vb_rule_begin(&p, &r, 1, 1, 0, 2);
  vb_rule_state(&p, 0, 4096, &fa);  vb_rule_put(&p, 4096, fa);
  s = vb_rule_state(&p, 0, 4096, &fa);
  CHECK(s == 1, "a met rule (state %d)", s);
  vb_rule_end(&p, &r, 1);
  vb_rule_begin(&p, &r, 4, 0, 0, 2);
  CHECK(vb_rule_state(&p, 0, 4096, &fa) == 0,
        "the rule is off before a class's last pass");
  vb_rule_end(&p, &r, 4);
  CHECK(r.th[4][0] == 0 && r.th[4][1] == 0,
        "a class without codebooks learns zero bounds");
}

static int headers(const parsed * f, const u8 ** id, sz * idl, const u8 ** su,
                   sz * sl) {
  int i, j, w = 0;
  Fi(f->n,
    sz at = 0;
    if (!isheader(f->pg + i) || (i && f->pg[i].first)) break;
    Fj(f->pg[i].p.np,
      if (w == 0) { *id = f->pg[i].body + at;  *idl = f->pg[i].p.plen[j]; }
      if (w == 2) {
        *su = f->pg[i].body + at;  *sl = f->pg[i].p.plen[j];  return 1;
      }
      at += f->pg[i].p.plen[j];  w++));
  return 0;
}

static sz vd_trip(const u8 * id, sz idl, const u8 * su, sz sl) {
  vb_ctx v;
  rc_enc e;
  rc_dec d;
  sz n;
  int ok;
  u8 * img = xmalloc(MAX(idl, sl));
  vb_init(&v);  vb_level(&v, CM_NLEV - 1);  rc_enc_init(&e);
  vb_hdr_enc(&v, &e, 0, id, idl);  vb_hdr_enc(&v, &e, 2, su, sl);
  n = rc_enc_finish(&e);
  vb_free(&v);  vb_init(&v);  vb_level(&v, CM_NLEV - 1);
  rc_dec_init(&d, rc_enc_data(&e), n);
  vb_hdr_dec(&v, &d, 0, img, idl);  ok = !memcmp(img, id, idl);
  vb_hdr_dec(&v, &d, 2, img, sl);  ok = ok && !memcmp(img, su, sl);
  vb_free(&v);  rc_enc_free(&e);  xfree(img);
  return ok ? n : 0;
}

static void t_dynest(void) {
  i32 L[2][21];
  u8 est[21];
  int i, n = 0;
  xt_section_begin("dynamic allocation estimate");
  memset(L, 0, sizeof L);
  opa_dynest(L[0], L[1], 1, 0, 21, 3, est);
  Fi(21, n += est[i]);
  CHECK(!n, "a flat spectrum takes no boosts");
  L[0][10] = 4 * 256;
  opa_dynest(L[0], L[1], 1, 0, 21, 3, est);
  n = 0;  Fi(21, n += i != 10 && est[i]);
  CHECK(est[10] == 13 && !n, "a 24 dB peak in band 10 of a 20 ms frame takes "
        "6.5 boosts there alone (%d)", est[10]);
  opa_dynest(L[0], L[1], 2, 0, 21, 3, est);
  CHECK(est[10] == 13, "a peak in one of two channels, half the excess over "
        "twice the width, takes as many (%d)", est[10]);
  L[1][10] = 4 * 256;
  opa_dynest(L[0], L[1], 2, 0, 21, 3, est);
  CHECK(est[10] == 26, "a peak in both channels takes twice as many (%d)",
        est[10]);
  L[1][10] = 0;
  memset(est, 9, sizeof est);  L[0][18] = 4 * 256;
  opa_dynest(L[0], L[1], 1, 17, 20, 3, est);
  n = 0;  Fi(21, n += (i < 17 || i >= 20) && est[i]);
  CHECK(!n && est[18] > 0, "bands outside the coded range take none (%d)",
        est[18]);
}

static void t_vdict(void) {
  parsed f, g;
  const u8 * id = NULL, * su = NULL, * mid = NULL, * msu = NULL;
  sz idl = 0, sl = 0, midl = 0, msl = 0, whole, book, miss, k;
  u8 * alt;
  xt_section_begin("setup dictionary");
  if (!parse(xt_fixture(xt_data, "tone_stereo.ogg"), &f)) {
    unparse(&f);  return;
  }
  if (!parse(xt_fixture(xt_data, "flooralt.ogg"), &g)) {
    unparse(&f);  unparse(&g);  return;
  }
  CHECK(headers(&f, &id, &idl, &su, &sl) &&
        headers(&g, &mid, &midl, &msu, &msl),
        "dictionary fixtures have their headers");
  if (!su || !msu) { unparse(&f);  unparse(&g);  return; }
  alt = xmalloc(sl);  memcpy(alt, su, sl);
  for (k = sl * 8 - 1; k > 0 && !(alt[k >> 3] >> (k & 7) & 1); k--) ;
  alt[(k - 41) >> 3] ^= (u8) (1 << ((k - 41) & 7));
  whole = vd_trip(id, idl, su, sl);  book = vd_trip(id, idl, alt, sl);
  miss = vd_trip(mid, midl, msu, msl);
  CHECK(whole && book && miss, "dictionary setups rebuild exactly");
  CHECK(whole <= 16, "a dictionary setup of %lu bytes codes as an index "
        "(%lu bytes)", (unsigned long) sl, (unsigned long) whole);
  CHECK(whole * 4 < book && book * 4 < sl,
        "dictionary codebooks code as indices (%lu of %lu bytes)",
        (unsigned long) book, (unsigned long) sl);
  xt_trace("setup dictionary: whole %lu, books %lu of %lu, miss %lu of %lu "
           "bytes", (unsigned long) whole, (unsigned long) book,
           (unsigned long) sl, (unsigned long) miss, (unsigned long) msl);
  xfree(alt);  unparse(&f);  unparse(&g);
}

static void t_vdict_old(void) {
  parsed f;
  const u8 * id = NULL, * su = NULL;
  sz idl = 0, sl = 0, full;
  xt_section_begin("setup dictionary of older releases");
  if (!parse(xt_fixture(xt_data, "lv101_22k.ogg"), &f)) {
    unparse(&f);  return;
  }
  CHECK(headers(&f, &id, &idl, &su, &sl) && su,
        "the 1.0.1 fixture has its headers");
  if (!su) { unparse(&f);  return; }
  full = vd_trip(id, idl, su, sl);
  CHECK(full && full <= 16, "the 1.0.1 setup of %lu bytes rebuilds from an "
        "index (%lu bytes)", (unsigned long) sl, (unsigned long) full);
  unparse(&f);
}

void xt_run_layers(void) {
  char ** files = xt_files(".ogg"), ** p;
  int rdiff = 0, rn = 0;
  if (!*files) {
    xt_section_begin("layers");
    CHECK(0, "no Vorbis fixtures: is BLR_TEST_DATA set?");
  }
  xt_section_begin("framing");
  for (p = files; *p; p++) t_frame(*p);
  for (p = files; *p; p++) {
    parsed f;
    vb_tune t;
    int i;
    xt_section_begin("page headers");
    if (!parse(*p, &f)) { unparse(&f);  continue; }
    t_pages(*p, &f);
    xt_section_begin("header packets");
    t_setup(*p, &f);
    xt_section_begin("audio packets");
    { static const int CFG[] = { 126, 154, 109, 111, 113, 142 };
      Fi(6,
        int lev = vb_config_tune(CFG[i], &t);
        t_audio(*p, &f, &t, lev, i & 1)); }
    { int lev = vb_config_tune(157, &t);  t_audio(*p, &f, &t, lev, 1); }
    xt_section_begin("classword rule");
    { static const int CFG[] = { 157, 155 };
      Fi(2,
        int lev = vb_config_tune(CFG[i], &t);
        sz a, b;
        t.x2 &= (u8) ~VB_T2_RULE;  a = t_audio(*p, &f, &t, lev, 0);
        t.x2 |= VB_T2_RULE;  b = t_audio(*p, &f, &t, lev, 0);
        rdiff += a != b;  rn++); }
    unparse(&f);
  }
  xt_files_free(files);
  CHECK(rdiff > 0, "the classword rule changes %d of %d streams", rdiff, rn);
  CHECK(fz_files > 0, "codebooks settle residue decisions in %d fixtures",
        fz_files);
  xt_section_begin("classword rule state");
  t_rule_state();
  t_vdict();
  t_vdict_old();
  t_dynest();
}
