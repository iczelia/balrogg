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

/*  Self-contained range-coder, value-coder, and container tests.  */

#include "t_harness.h"
#include "archive.h"
#include "model.h"
#include "ogg.h"

static xt_rng rng;

/*  blr_ilog has a builtin and a loop form; both must agree at the edges.  */
static void t_ilog(void) {
  static const u32 v[] = { 0, 1, 2, 3, 4, 7, 8, 255, 256, 0x7FFFFFFFUL,
                           0x80000000UL, 0xFFFFFFFFUL };
  static const u32 want[] = { 0, 1, 2, 2, 3, 3, 4, 8, 9, 31, 32, 32 };
  sz i;
  xt_section_begin("ilog");
  Fi(sizeof v / sizeof *v,
     CHECK(blr_ilog(v[i]) == want[i], "ilog(%lu) = %lu, expected %lu",
           (unsigned long) v[i], (unsigned long) blr_ilog(v[i]),
           (unsigned long) want[i]));
}

static void t_crc(void) {
  static const u8 check[] = "123456789";
  u8 short_page[OGG_HDRMIN] = { 0 };
  xt_section_begin("Ogg CRC");
  CHECK(ogg_crc(check, sizeof check - 1) == 0x89A1897FUL,
        "known CRC vector");
  CHECK(!ogg_crc_ok(short_page, OGG_HDRMIN - 1),
        "short page accepted by CRC check");
}

/*  Verify decoder seeding from short streams.  */
static void t_short_init(void) {
  static const u8 b[4] = { 0x80, 0x12, 0x34, 0x56 };
  static const u32 want[5] = { 0, 0x80000000UL, 0x80120000UL, 0x80123400UL,
                               0x80123456UL };
  rc_dec d;
  sz i;
  xt_section_begin("rc short init");
  Fi(5, {
    rc_dec_init(&d, b, i);
    CHECK(d.code == want[i] && d.range == 0xFFFFFFFFUL && d.pos == i,
          "rc_dec_init on a %lu-byte stream", (unsigned long) i);
  });
}

/*  Round-trip normal, saturated, and carry-heavy bit sequences.  */
static int roundtrip(sz nslots, sz n, int mode) {
  u16 * pe = xmalloc(nslots * sizeof *pe), * pd = xmalloc(nslots * sizeof *pd);
  u8 * slot = xmalloc(n), * bit = xmalloc(n);
  rc_enc e;  rc_dec d;
  sz i, len;
  int ok = 1;
  rc_probs_init(pe, nslots);  rc_probs_init(pd, nslots);
  rc_enc_init(&e);
  Fi(n, {
    slot[i] = (u8) xt_next(&rng, (u32) nslots);
    bit[i] = (u8) (mode == 0 ? xt_next(&rng, 2) : mode == 1 ? 0 : mode == 2 ? 1
                             : (xt_next(&rng, 100) < 97));
    rc_enc_bit(&e, pe + slot[i], bit[i]);
  });
  len = rc_enc_finish(&e);
  rc_dec_init(&d, rc_enc_data(&e), len);
  Fi(n, if (rc_dec_bit(&d, pd + slot[i]) != bit[i]) { ok = 0;  break; });
  rc_enc_free(&e);
  free(pe);  free(pd);  free(slot);  free(bit);
  return ok;
}

static void t_coder(void) {
  static const sz ns[4] = { 1, 2, 8, 64 };
  static const sz nb[8] = { 1, 2, 3, 4, 5, 17, 200, 5000 };
  sz i, rounds = 400 * (sz) xt_level;
  int bad = 0;
  xt_section_begin("rc round trip");
  Fi(rounds, if (!roundtrip(ns[xt_next(&rng, 4)], nb[xt_next(&rng, 8)],
                            (int) (i % 4))) bad++);
  CHECK(!bad, "%d of %lu sequences failed", bad, (unsigned long) rounds);
}

/*  The count-capped pair has to agree with itself too, and its clamp has to
    keep every probability off both rails.  */
static void t_coder_ad(void) {
  enum { N = 20000, NS = 16 };
  u16 pe[NS], pd[NS];
  u8 ce[NS], cd[NS], * bit = xmalloc(N), * slot = xmalloc(N);
  rc_enc e;  rc_dec d;
  sz i, len;
  int bad = 0, rail = 0;
  xt_section_begin("rc adaptive round trip");
  rc_probs_init(pe, NS);  rc_probs_init(pd, NS);
  memset(ce, 0, sizeof ce);  memset(cd, 0, sizeof cd);
  rc_enc_init(&e);
  Fi(N, {
    slot[i] = (u8) xt_next(&rng, NS);
    bit[i] = (u8) (slot[i] < 4 ? 1 : slot[i] < 8 ? 0 : xt_next(&rng, 2));
    rc_enc_bit_ad(&e, pe + slot[i], ce + slot[i], RC_ALIM, bit[i]);
  });
  len = rc_enc_finish(&e);
  rc_dec_init(&d, rc_enc_data(&e), len);
  Fi(N, if (rc_dec_bit_ad(&d, pd + slot[i], cd + slot[i], RC_ALIM) != bit[i]) bad++);
  Fi(NS, if (!pe[i] || pe[i] == 0xFFFF) rail++);
  CHECK(!bad, "%d bits decoded wrong", bad);
  CHECK(!memcmp(pe, pd, sizeof pe) && !memcmp(ce, cd, sizeof ce),
        "encoder and decoder model state differ");
  CHECK(!rail, "%d probabilities reached a rail", rail);
  CHECK(ce[0] == RC_ALIM, "count capped at %d, not %d", ce[0], RC_ALIM);
  rc_enc_free(&e);  free(bit);  free(slot);
}

static void t_varint(void) {
  static const u32 edge[] = { 0, 1, 0x3F, 0x40, 0x3FFF, 0x4000, 0x3FFFFF,
                              0x400000, 0x3FFFFFFFUL };
  u8 b[ARC_VARINT_MAX];
  sz i, pos, n;
  int bad = 0;
  xt_section_begin("varint");
  Fi(sizeof edge / sizeof *edge, {
    n = arc_varint_put(b, edge[i]);  pos = 0;
    if (n != arc_varint_len(edge[i]) || arc_varint_get(b, n, &pos) != edge[i]
        || pos != n) bad++;
  });
  Fi(200000, {
    u32 v = xt_next(&rng, 0x40000000UL);
    n = arc_varint_put(b, v);  pos = 0;
    if (arc_varint_get(b, n, &pos) != v || pos != n) bad++;
  });
  CHECK(!bad, "%d values failed to round-trip", bad);
  CHECK(arc_varint_len(0) == 1 && arc_varint_len(0x3FFFFFFFUL) == 4,
        "varint widths");
}

/*  Round-trip synthetic containers with varied streams and tune blobs.  */
static void t_container(void) {
  archive a, b2;
  u8 * img, * blob;
  sz i, j, k, ns, len, len2;
  int bad = 0;
  xt_section_begin("container");
  blob = xmalloc(70000);
  Fi(70000, blob[i] = (u8) xt_next(&rng, 256));
  for (ns = 0; ns <= 6; ns++) {
    arc_init(&a, 0x09);
    if (ns & 1) { a.ntune = 3;  a.tune[0] = 61;  a.tune[1] = 7;  a.tune[2] = 1; }
    Fj(ns, arc_push(&a, blob, j == 0 ? 1 : j * j * 700));
    img = arc_emit(&a, &len);
    arc_parse(&b2, img, len);
    if (b2.n != a.n || b2.flags != a.flags || b2.ntune != a.ntune
        || memcmp(b2.tune, a.tune, a.ntune)) bad++;
    else
      Fk(a.n, if (b2.s[k].len != a.s[k].len
                  || memcmp(b2.s[k].data, a.s[k].data, a.s[k].len)) bad++);
    { u8 * img2 = arc_emit(&b2, &len2);
      if (len2 != len || memcmp(img, img2, len)) bad++;
      free(img2); }
    arc_free(&a);  arc_free(&b2);  free(img);
  }
  free(blob);
  CHECK(!bad, "%d shapes failed to round-trip", bad);
  CHECK(ARC_BLOCK(0x09) == 32768UL && ARC_SOLID(0x09) == 1
        && ARC_LEVEL(0x69) == 3, "flags decoding");
}

/*  Exercise each value-coder axis and compare both output and model state.  */

static const mdl_cfg CFG[] = {
  /*  Page-header shapes and the remaining supported axes.  */
  { 3, 0, 0,  2, 0, 0 },  { 5, 1, 2,  8, 0, 1 },
  { 5, 1, 1,  1, 0, 1 },  { 5, 1, 2,  8, 0, 1 },
  { 5, 1, 2,  8, 0, 2 },  { 3, 0, 0,  8, 1, 0 },
  { 4, 0, 0, 32, 1, 0 },
  { 2, 0, 0,  1, 0, 0 },  { 2, 1, 1,  1, 1, 0 },
  { 4, 1, 1,  8, 1, 2 },  { 5, 0, 0, 32, 1, 1 },
  { 3, 1, 2,  2, 1, 1 }
};

#define NCFG ((int) (sizeof CFG / sizeof *CFG))
#define NVAL 1024

static u32 vals[NVAL];
static int nval;

/*  Cover every length and sign, boundary magnitudes, and changing histories.  */
static void gen(const mdl_cfg * c) {
  u32 top = 1UL << c->depth, mask, i, s = 12345;
  nval = 0;
  for (i = 0; i < top && i < 32 && nval < NVAL - 8; i++) {
    u32 m = 1UL << i;
    s = s * 1103515245UL + 12345;
    vals[nval++] = 0;
    vals[nval++] = m;
    vals[nval++] = m | (m - 1);
    vals[nval++] = m | (s & (m - 1));
    if (c->sgn) { vals[nval++] = 0 - m;  vals[nval++] = 0 - (m | (m - 1)); }
  }
  mask = top >= 32 ? 0xFFFFFFFFUL : (1UL << top) - 1;
  while (nval < NVAL) {
    s = s * 1103515245UL + 12345;
    vals[nval++] = (s >> 8) & mask;
  }
}

/*  Integrate drive values for first-order and second-order predictors.  */
typedef struct { u32 a, b; } accum;

static u32 drive(const mdl_cfg * c, u32 v, accum * s) {
  if (c->order >= 2) { s->b += v;  v = s->b; }
  if (c->order >= 1) { s->a += v;  v = s->a; }
  return v;
}

static void t_model(void) {
  int i, k;
  xt_section_begin("value coder");
  for (k = 0; k < NCFG; k++) {
    model me, md;
    accum s;
    rc_enc e;
    rc_dec d;
    sz len;
    int wrong = 0;
    gen(CFG + k);
    mdl_init(&me, CFG + k);  mdl_init(&md, CFG + k);
    rc_enc_init(&e);  s.a = s.b = 0;
    for (i = 0; i < nval; i++) {
      /*  Halfway through, a chain-link boundary: histories and predictor go,
          probabilities stay.  */
      if (i == nval / 2) { mdl_reset(&me);  s.a = s.b = 0; }
      mdl_enc(&me, &e, drive(CFG + k, vals[i], &s));
    }
    len = rc_enc_finish(&e);
    rc_dec_init(&d, rc_enc_data(&e), len);  s.a = s.b = 0;
    for (i = 0; i < nval; i++) {
      if (i == nval / 2) { mdl_reset(&md);  s.a = s.b = 0; }
      if (drive(CFG + k, vals[i], &s) != mdl_dec(&md, &d)) wrong++;
    }
    CHECK(!wrong, "cfg %d (depth %d): %d values decoded wrong", k,
          CFG[k].depth, wrong);
    CHECK(me.n == md.n && !memcmp(me.p, md.p, me.n * sizeof *me.p)
          && !memcmp(me.cn, md.cn, me.n), "cfg %d: model memory diverged", k);
    xt_trace("cfg %-2d depth=%d %s -> %lu bytes", k, CFG[k].depth,
             CFG[k].sgn ? "signed" : "unsigned", (unsigned long) len);
    rc_enc_free(&e);  mdl_free(&me);  mdl_free(&md);
  }
}

void xt_run_unit(void) {
  xt_seed(&rng, 20260810UL);
  t_ilog();
  t_crc();
  t_short_init();
  t_coder();
  t_coder_ad();
  t_varint();
  t_container();
  t_model();
}
