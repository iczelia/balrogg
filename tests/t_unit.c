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
#include "coder/cm.h"
#include "sys/cpu.h"
#include "sys/crc.h"
#include "fmt/arc.h"
#include "coder/model.h"
#include "fmt/ogg.h"
#include "sys/map.h"
#include <errno.h>
#if defined(HAVE_FORK) && defined(HAVE_WAITPID) && \
    defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H)
#include <sys/wait.h>
#include <unistd.h>
#define T_FORK 1
#endif
#if defined(BLR_REGION_CHECK) || defined(__SANITIZE_ADDRESS__)
#define T_RGWRITES 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define T_RGWRITES 1
#endif
#endif

static xt_rng rng;

static void t_mapping(void) {
  blr_map m;
  sz i, n = BLR_IO_CHUNK + 37;
  int bad = 0;
  xt_section_begin("zeroed mappings");
  bm_alloc(&m, n);
  Fi(n, if (m.p[i]) bad = 1;  m.p[i] = (u8) (i * 17));
  CHECK(!bad && m.p[n - 1] == (u8) ((n - 1) * 17),
        "anonymous allocation is zeroed and writable through its last byte");
  CHECK(!blr_no_mmap || m.heap, "--no-mmap forces heap allocation");
  bm_free(&m);  bm_free(&m);
  CHECK(!m.p && !m.len, "mapping can be released twice");
}

static void t_region(void) {
  u8 * p[48], * v;
  sz i, j, n;
  int bad = 0;
  xt_section_begin("region");
  v = rg_take(5000, 0);
  if (!v) { CHECK(1, "no region on this host");  return; }
  CHECK(rg_owns(v) && rg_size(v) == 5000 && !rg_owns(&n),
        "a region block is owned and knows its size");
  rg_give(v);
  Fi(48, n = 1024 + i * 2731;  p[i] = xmalloc(n);  memset(p[i], 0xff, n));
  Fi(48, xfree(p[i]));
  Fi(48,
    n = 1024 + (47 - i) * 2731;  p[i] = xcalloc(n, 1);
    Fj(n, bad |= p[i][j]));
  CHECK(!bad, "xcalloc blocks are zero after reuse");
  Fi(48, xfree(p[i]));
  v = xmalloc(1500);
  Fi(1500, v[i] = (u8) (i * 7));
  for (n = 3000; n <= ((sz) 6 << 20); n *= 2) {
    v = xrealloc(v, n);
    Fi(1500, bad |= v[i] != (u8) (i * 7));
  }
  v = xrealloc(v, 1200);
  Fi(1200, bad |= v[i] != (u8) (i * 7));
  CHECK(!bad, "xrealloc keeps a block's bytes as it grows and shrinks");
  xfree(v);
  v = xmalloc((sz) 600 << 10);  memset(v, 0x5a, (sz) 600 << 10);  xfree(v);
  v = xcalloc((sz) 600 << 10, 1);
  Fi(((sz) 600 << 10), bad |= v[i]);
  CHECK(!bad, "a large block reads as zero after its pages are given back");
  xfree(v);
  CHECK(!rg_walk(), "every block of the region is consistent");
}

#ifdef T_FORK
static int t_dies(void (*body)(void)) {
  int st;
  pid_t c;
  fflush(NULL);
  c = fork();
  if (c < 0) return 0;
  if (!c) {
    if (!freopen("/dev/null", "w", stderr)) _exit(0);
    body();  _exit(0);
  }
  while (waitpid(c, &st, 0) < 0) if (errno != EINTR) return 0;
  return !WIFEXITED(st) || WEXITSTATUS(st);
}

static void d_twice(void) { u8 * p = xmalloc(3000);  xfree(p);  xfree(p); }
static void d_wild(void) { u8 * p = xmalloc(3000);  xfree(p + 64); }
static void d_header(void) {
  volatile u8 * p = xmalloc(3000);
  p[-12] ^= 0x55;
  if (rg_walk()) abort();
}
#ifdef T_RGWRITES
static void d_past(void) {
  volatile u8 * p = xmalloc(3000);
  p[3000] = 1;  xfree((u8 *) p);
}
static void d_after(void) {
  volatile u8 * p = xmalloc(3000);
  int i;
  xfree((u8 *) p);  p[100] = 1;
  Fi(600, xfree(xmalloc(2000)));
}
#endif

static void t_region_dies(void) {
  u8 * v = rg_take(5000, 0);
  xt_section_begin("region damage");
  if (!v) { CHECK(1, "no region on this host");  return; }
  rg_give(v);
  CHECK(t_dies(d_twice), "a block freed twice stops the program");
  CHECK(t_dies(d_wild), "freeing a pointer into a block stops the program");
  CHECK(t_dies(d_header), "an overwritten header is found");
#ifdef T_RGWRITES
  CHECK(t_dies(d_past), "a write past a block's end is found");
  CHECK(t_dies(d_after), "a write to a freed block is found");
#endif
}
#endif

static void t_file_windows(void) {
  const char * path = xt_tmp("window.bin"), * path2 = xt_tmp("window-copy.bin");
  blr_file * f = bf_open(path, 1), * copy = bf_open(path2, 1);
  sz n = 3 * BLR_IO_CHUNK + 17, i;
  u8 * expected = xmalloc(n + 1), * actual = xmalloc(n + 1);
  static const sz marks[] = { 0, BLR_IO_CHUNK - 1, BLR_IO_CHUNK,
                              2 * BLR_IO_CHUNK + 1 };
  int bad = 0;
  xt_section_begin("file windows");
  Fi(n, expected[i] = (u8) (i * 29 + i / 251));
  bf_write(f, 0, expected, n);
  Fi(ARRAY_LEN(marks),
    sz at = marks[i];
    expected[at] ^= 0xA7;
    bf_write(f, at, expected + at, 1));
  CHECK(bf_get(f, 0) == expected[0], "backward read after overwrite");
  expected[n] = 0x37;  bf_put(f, expected[n]);
  bf_copy(copy, f, 0, n + 1);
  bf_read(copy, 0, actual, n + 1);
  CHECK(!memcmp(actual, expected, n + 1),
        "file windows preserve overwrite and append");
  for (i = n + 1; i > 0; i -= MIN(i, (sz) 7919)) {
    sz at = i - MIN(i, (sz) 7919);
    if (bf_get(f, at) != expected[at]) bad = 1;
  }
  CHECK(!bad, "reverse reads across windows");
  bf_resize(f, n + 1 + 4096);
  bf_move(f, 4096, 0, n + 1);
  bf_read(f, 4096, actual, n + 1);
  CHECK(!memcmp(actual, expected, n + 1), "overlapping backwards expansion");
  bf_move(f, 0, 4096, n + 1);  bf_resize(f, n + 1);
  bf_read(f, 0, actual, n + 1);
  CHECK(f->len == n + 1 && !memcmp(actual, expected, n + 1),
        "overlapping forward compaction and truncation");
  bf_dropcache(f);  bf_readmeta(f, BLR_IO_CHUNK - 3, actual, 17);
  CHECK(!memcmp(actual, expected + BLR_IO_CHUNK - 3, 17),
        "metadata reads span windows after dropping the cache");
  bf_close(f);  bf_close(copy);
  CHECK(xt_file_size(path) == (long) n + 1,
        "closing removes mapped output padding");
  f = bf_open(path, 0);  bf_read(f, 0, actual, n + 1);
  CHECK(!memcmp(actual, expected, n + 1), "reopened input matches the output");
  bf_dropcache(f);
  CHECK(bf_get(f, n) == expected[n], "input can be read after unmapping");
  bf_close(f);  xfree(expected);  xfree(actual);
  xt_unlink(path);  xt_unlink(path2);
}

static void t_chunks(void) {
  const char * path = xt_tmp("chunks.blr"), * path2 = xt_tmp("chunks-copy.blr");
  blr_file * output = bf_open(path, 1), * st[3], * input;
  archive a, b, c;
  u8 * data = xmalloc(BLR_IO_CHUNK + 257), * got = xmalloc(BLR_IO_CHUNK + 257);
  u8 * img, * re;
  sz i, n, rn;
  int ok;
  xt_section_begin("interleaved archive chunks");
  Fi(BLR_IO_CHUNK + 257, data[i] = (u8) (i * 71 + i / 113));
  arc_init(&a, 0, 9);  arc_begin(&a, output);
  Fi(3, st[i] = arc_newstream(&a));
  bf_write(st[2], 0, data, BLR_IO_CHUNK + 257);
  bf_write(st[1], 0, data, BLR_IO_CHUNK + 17);
  bf_write(st[2], st[2]->len, data, BLR_IO_CHUNK);
  bf_write(st[0], 0, data, 19);
  arc_finish(&a);  n = output->len;
  CHECK(arc_size(&a) == n, "chunk framing size accounting");
  arc_free(&a);  bf_close(output);
  input = bf_open(path, 0);  arc_read(&b, input);
  CHECK(b.n == 3 && b.s[2].len == 2 * BLR_IO_CHUNK + 257,
        "chunk parser restores logical stream lengths");
  bf_read(b.s[1].file, 0, got, BLR_IO_CHUNK + 17);
  ok = !memcmp(got, data, BLR_IO_CHUNK + 17);
  bf_read(b.s[2].file, BLR_IO_CHUNK + 257, got, BLR_IO_CHUNK);
  CHECK(ok && !memcmp(got, data, BLR_IO_CHUNK),
        "logical reads span physically interleaved chunks");
  arc_write(&b, path2);
  CHECK(xt_same_file(path, path2),
        "chunk file writer preserves physical layout");
  img = slurp(path, &n);  arc_parse(&c, img, n);  re = arc_emit(&c, &rn);
  CHECK(n == rn && !memcmp(img, re, n), "chunk layout re-emits byte for byte");
  xfree(re);  arc_free(&c);  xfree(img);  arc_free(&b);  bf_close(input);
  xfree(data);  xfree(got);  xt_unlink(path);  xt_unlink(path2);
}

static void t_ilog(void) {
  static const u32 v[] = { 0, 1, 2, 3, 4, 7, 8, 255, 256, 0x7FFFFFFFUL,
                           0x80000000UL, 0xFFFFFFFFUL };
  static const u32 want[] = { 0, 1, 2, 2, 3, 3, 4, 8, 9, 31, 32, 32 };
  sz i;
  xt_section_begin("ilog");
  Fi(ARRAY_LEN(v),
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

static void t_crc32c(void) {
  static const u8 check[] = "123456789";
  u8 v[4][32], * b = xmalloc(65536 + 16);
  const u32 want[4] = {
    0x8A9136AAUL, 0x62A8AB43UL, 0x46DD794EUL, 0x113FDB5CUL
  };
  static const sz edge[12] = {
    767, 768, 769, 775, 776, 24575, 24576, 24577, 24583, 25351, 49152, 65536
  };
  const char * name;
  crc32c_fn f;
  int i, j, k;
  xt_section_begin("CRC-32C");
  Fi(32,
    v[0][i] = 0;  v[1][i] = 0xFF;  v[2][i] = (u8) i;  v[3][i] = (u8) (31 - i));
  Fi(65536 + 16, b[i] = (u8) xt_next(&rng, 256));
  CHECK(crc32c(0, check, 9) == 0xE3069283UL, "crc32c of 123456789 is %08lx",
        (unsigned long) crc32c(0, check, 9));
  for (k = 0; (f = crc32c_impl(k, &name)); k++) {
    int ok = 1;
    CHECK(f(0, check, 9) == 0xE3069283UL && f(0, check, 0) == 0,
          "%s: check value", name);
    Fi(4, CHECK(f(0, v[i], 32) == want[i], "%s: RFC 3720 vector %d", name, i));
    Fi(400 + 12,
      sz at = xt_next(&rng, 16);
      sz n = i < 100 ? (sz) i : i < 250 ? xt_next(&rng, 4097)
           : i < 400 ? xt_next(&rng, 65537) : edge[i - 400];
      sz cut = n ? xt_next(&rng, (u32) n + 1) : 0;
      u32 r = crc32c_sw(0, b + at, n);
      if (f(0, b + at, n) != r ||
          f(f(0, b + at, cut), b + at + cut, n - cut) != r) ok = 0);
    CHECK(ok, "%s: equal to the portable CRC on random lengths, offsets and "
          "splits", name);
    xt_trace("CRC-32C implementation %s", name);
  }
  CHECK(k >= 1, "no CRC-32C implementation");
  Fj(2,
    CHECK(crc32c(0, b + j, 65536) == crc32c_sw(0, b + j, 65536),
          "crc32c dispatches to an equal implementation"));
  xfree(b);
}

static void t_short_init(void) {
  static const u8 b[4] = { 0x80, 0x12, 0x34, 0x56 };
  static const u32 want[5] = { 0, 0x80000000UL, 0x80120000UL, 0x80123400UL,
                               0x80123456UL };
  rc_dec d;
  sz i;
  xt_section_begin("rc short init");
  Fi(5,
    rc_dec_init(&d, b, i);
    CHECK(d.code == want[i] && d.range == 0xFFFFFFFFUL && d.pos == i,
          "rc_dec_init on a %lu-byte stream", (unsigned long) i));
}

static int roundtrip(sz nslots, sz n, int mode) {
  u16 * pe = xmalloc(nslots * sizeof *pe), * pd = xmalloc(nslots * sizeof *pd);
  u8 * slot = xmalloc(n), * bit = xmalloc(n);
  rc_enc e;  rc_dec d;
  sz i, len;
  int ok = 1;
  rc_probs_init(pe, nslots);  rc_probs_init(pd, nslots);
  rc_enc_init(&e);
  Fi(n,
    slot[i] = (u8) xt_next(&rng, (u32) nslots);
    bit[i] = (u8) (mode == 0 ? xt_next(&rng, 2) : mode == 1 ? 0 : mode == 2 ? 1
                             : xt_next(&rng, 100) < 97);
    rc_enc_bit(&e, pe + slot[i], bit[i]));
  len = rc_enc_finish(&e);
  rc_dec_init(&d, rc_enc_data(&e), len);
  Fi(n, if (rc_dec_bit(&d, pd + slot[i]) != bit[i]) { ok = 0;  break; });
  rc_enc_free(&e);
  xfree(pe);  xfree(pd);  xfree(slot);  xfree(bit);
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
  Fi(N,
    slot[i] = (u8) xt_next(&rng, NS);
    bit[i] = (u8) (slot[i] < 4 ? 1 : slot[i] < 8 ? 0 : xt_next(&rng, 2));
    rc_enc_bit_ad(&e, pe + slot[i], ce + slot[i], RC_ALIM, bit[i]));
  len = rc_enc_finish(&e);
  rc_dec_init(&d, rc_enc_data(&e), len);
  Fi(N,
    if (rc_dec_bit_ad(&d, pd + slot[i], cd + slot[i], RC_ALIM) != bit[i])
      bad++);
  Fi(NS, if (!pe[i] || pe[i] == 0xFFFF) rail++);
  CHECK(!bad, "%d bits decoded wrong", bad);
  CHECK(!memcmp(pe, pd, sizeof pe) && !memcmp(ce, cd, sizeof ce),
        "encoder and decoder model state differ");
  CHECK(!rail, "%d probabilities reached a rail", rail);
  CHECK(ce[0] == RC_ALIM, "count capped at %d, not %d", ce[0], RC_ALIM);
  rc_enc_free(&e);  xfree(bit);  xfree(slot);
}

static void t_packed(void) {
  static const int limit[] = { 0, 1, RC_ALIM, RC_CNTMAX };
  u32 i, j;
  int bad = 0;
  xt_section_begin("rc packed initialization");
  Fi(ARRAY_LEN(limit),
    Fj(2,
      u8 count = 0;
      u32 p = rc_adapt(RC_PINIT, &count, limit[i], (int) j);
      if (rc_adapt_packed(0, limit[i], (int) j) !=
          ((p ^ RC_PINIT) | (u32) count << 16)) bad++));
  CHECK(!bad, "%d lazy initializations differ", bad);
}

static void t_container(void) {
  archive a, b2;
  u8 * img, * blob;
  sz i, j, k, len, len2;
  int bad = 0;
  xt_section_begin("container");
  blob = xmalloc(70000);
  Fi(70000, blob[i] = (u8) xt_next(&rng, 256));
  Fi(7,
    arc_init(&a, i & 1, (int) i * 37);
    Fj(i, arc_push(&a, blob, j == 0 ? 1 : j * j * 700));
    img = arc_emit(&a, &len);
    arc_parse(&b2, img, len);
    if (b2.n != a.n || b2.opus != a.opus || b2.cfg != a.cfg) bad++;
    else
      Fk(a.n,
        u8 * got = xmalloc(b2.s[k].len);
        bf_read(b2.s[k].file, 0, got, b2.s[k].len);
        if (b2.s[k].len != a.s[k].len ||
            memcmp(got, a.s[k].data, a.s[k].len)) bad++;
        xfree(got));
    { u8 * img2 = arc_emit(&b2, &len2);
      if (len2 != len || memcmp(img, img2, len)) bad++;
      xfree(img2); }
    { const char * path = xt_tmp("container.blr");
      blr_file * file;
      archive streamed;
      u8 * image;
      arc_write(&a, path);
      file = bf_open(path, 0);
      arc_read(&streamed, file);
      image = arc_emit(&streamed, &len2);
      if (len2 != len || memcmp(img, image, len)) bad++;
      xfree(image);  arc_free(&streamed);  bf_close(file);  xt_unlink(path); }
    arc_free(&a);  arc_free(&b2);  xfree(img));
  xfree(blob);
  CHECK(!bad, "%d shapes failed to round-trip", bad);
}

static const mdl_cfg CFG[] = {
  { 3, 0, 0,  2, 0, 0 },  { 5, 1, 2,  8, 0, 1 },
  { 5, 1, 1,  1, 0, 1 },
  { 5, 1, 2,  8, 0, 2 },  { 3, 0, 0,  8, 1, 0 },
  { 4, 0, 0, 32, 1, 0 },
  { 2, 0, 0,  1, 0, 0 },  { 2, 1, 1,  1, 1, 0 },
  { 4, 1, 1,  8, 1, 2 },  { 5, 0, 0, 32, 1, 1 },
  { 3, 1, 2,  2, 1, 1 }
};

#define NCFG ((int) ARRAY_LEN(CFG))
#define NVAL 1024

static u32 vals[NVAL];
static int nval;

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
    vals[nval++] = s >> 8 & mask;
  }
}

typedef struct { u32 a, b; } accum;

static u32 drive(const mdl_cfg * c, u32 v, accum * s) {
  if (c->order >= 2) { s->b += v;  v = s->b; }
  if (c->order >= 1) { s->a += v;  v = s->a; }
  return v;
}

static void t_model(void) {
  int i, k;
  xt_section_begin("value coder");
  Fk(NCFG,
    model me, md;
    accum s;
    rc_enc e;
    rc_dec d;
    sz len;
    int wrong = 0;
    gen(CFG + k);
    mdl_init(&me, CFG + k);  mdl_init(&md, CFG + k);
    rc_enc_init(&e);  s.a = s.b = 0;
    Fi(nval,
      if (i == nval / 2) { mdl_reset(&me);  s.a = s.b = 0; }
      mdl_enc(&me, &e, drive(CFG + k, vals[i], &s)));
    len = rc_enc_finish(&e);
    rc_dec_init(&d, rc_enc_data(&e), len);  s.a = s.b = 0;
    Fi(nval,
      if (i == nval / 2) { mdl_reset(&md);  s.a = s.b = 0; }
      if (drive(CFG + k, vals[i], &s) != mdl_dec(&md, &d)) wrong++);
    CHECK(!wrong, "cfg %d (depth %d): %d values decoded wrong", k,
          CFG[k].depth, wrong);
    CHECK(me.n == md.n && !memcmp(me.p, md.p, me.n * sizeof *me.p) &&
          !memcmp(me.cn, md.cn, me.n), "cfg %d: model memory diverged", k);
    xt_trace("cfg %-2d depth=%d %s -> %lu bytes", k, CFG[k].depth,
             CFG[k].sgn ? "signed" : "unsigned", (unsigned long) len);
    rc_enc_free(&e);  mdl_free(&me);  mdl_free(&md));
}

static void t_training_direction(cm_plain_fn kernel, const char * name) {
  static const int rates[] = { 16, 17, 31 };
  int i, bit;
  xt_section_begin(name);
  Fi(3, for (bit = 0; bit <= 1; bit++) {
    cm c;
    rc_enc e;
    u32 p = 0;
    memset(&c, 0, sizeof c);
    cm_new(&c, 1, 4, 1, rates[i]);
    rc_enc_init(&e);  cm_bind(&c, &e, NULL);
    kernel(&c, 0, 0, 0, &p, bit);
    CHECK(c.st[0].w[2] == (bit ? 8 : -8) * rates[i],
          "rate %d, bit %d: bias weight %d, expected %d", rates[i], bit,
          c.st[0].w[2], (bit ? 8 : -8) * rates[i]);
    rc_enc_free(&e);  cm_free(&c);
  });
}

static void t_plain_kernel(cm_bit_fn reference, cm_plain_fn plain,
                            int lr, const char * name) {
  cm a, b, c;
  rc_enc ea, eb;
  rc_dec d;
  blr_file * input = NULL;
  xt_rng r;
  u32 pa[64] = { 0 }, pb[64] = { 0 }, pc[64] = { 0 };
  sz la, lb;
  int i, j, same = 1, wrong = 0;
  xt_section_begin(name);
  xt_trace("learning rate %d", lr);
  memset(&a, 0, sizeof a);  memset(&b, 0, sizeof b);  memset(&c, 0, sizeof c);
  cm_new(&a, 3, 12, 8, lr);  cm_new(&b, 3, 12, 8, lr);
  cm_new(&c, 3, 12, 8, lr);
  Fi(3, Fj(8 * CM_NI,
    static const short weights[] = {
      32767, -32768, 129, -257, -1, 1, 1234, -2345
    };
    a.st[i].w[j] = b.st[i].w[j] = c.st[i].w[j] = weights[j % CM_NI]));
  rc_enc_init(&ea);  rc_enc_init(&eb);
  cm_bind(&a, &ea, NULL);  cm_bind(&b, &eb, NULL);
  Fj(2,
    xt_seed(&r, 17);
    Fi(8192,
      int st = (int) xt_next(&r, 3), sel = (int) xt_next(&r, 8);
      u32 h = xt_next(&r, 3000);
      int k = (int) xt_next(&r, 64);
      int bit = (int) ((xt_next(&r, 100) < 80) ^ (h & 1));
      if (!j) {
        reference(&a, st, sel, h, pa + k, bit);
        if (plain(&b, st, sel, h, pb + k, bit) != bit) wrong++;
      } else if (plain(&c, st, sel, h, pc + k, !bit) != bit) wrong++);
    if (!j) {
      la = rc_enc_finish(&ea);  lb = rc_enc_finish(&eb);
      CHECK(la == lb && !memcmp(rc_enc_data(&ea), rc_enc_data(&eb), la),
            "plain archive differs from the general kernel");
      input = bf_memory(rc_enc_data(&eb), lb);
      rc_dec_file(&d, input, 0, lb);
      d.avail = d.pos;
      cm_bind(&c, NULL, &d);
    });
  CHECK(!wrong, "plain kernel returned %d wrong bits", wrong);
  CHECK(d.base > 0, "plain decoder did not refill");
  Fi(3,
    if (memcmp(a.st[i].w, b.st[i].w, 8 * CM_NI * sizeof(short)) ||
        memcmp(b.st[i].w, c.st[i].w, 8 * CM_NI * sizeof(short))) same = 0;
    if (memcmp(a.st[i].sm, b.st[i].sm, 256 * sizeof *a.st[i].sm) ||
        memcmp(b.st[i].sm, c.st[i].sm, 256 * sizeof *b.st[i].sm)) same = 0;
    if (memcmp(a.st[i].hist, b.st[i].hist, (sz) 1 << 12) ||
        memcmp(b.st[i].hist, c.st[i].hist, (sz) 1 << 12)) same = 0);
  CHECK(same && !memcmp(pa, pb, sizeof pa) && !memcmp(pb, pc, sizeof pb),
        "plain kernel model states differ");
  rc_dec_free(&d);  bf_close(input);
  rc_enc_free(&ea);  rc_enc_free(&eb);
  cm_free(&a);  cm_free(&b);  cm_free(&c);
}

#if defined(HAVE_SSE2) || defined(HAVE_AVX2)
static void t_kernels(cm_bit_fn reference, cm_bit_fn kernel,
                       int lr, const char * name) {
  cm a, b;
  rc_enc ea, eb;
  xt_rng r;
  u32 pa[64], pb[64];
  sz la, lb;
  int i, same = 1;
  xt_section_begin(name);
  xt_trace("learning rate %d", lr);
  memset(&a, 0, sizeof a);  memset(&b, 0, sizeof b);
  cm_new(&a, 3, 12, 8, lr);  cm_new(&b, 3, 12, 8, lr);
  rc_enc_init(&ea);  rc_enc_init(&eb);
  cm_bind(&a, &ea, NULL);  cm_bind(&b, &eb, NULL);
  memset(pa, 0, sizeof pa);  memset(pb, 0, sizeof pb);
  xt_seed(&r, 3);
  Fi(200000,
    int st = (int) xt_next(&r, 3), sel = (int) xt_next(&r, 8);
    u32 h = xt_next(&r, 3000);
    int k = (int) xt_next(&r, 64);
    int bit = (int) ((xt_next(&r, 100) < 80) ^ (h & 1));
    reference(&a, st, sel, h, pa + k, bit);
    kernel(&b, st, sel, h, pb + k, bit));
  la = rc_enc_finish(&ea);  lb = rc_enc_finish(&eb);
  CHECK(la == lb && !memcmp(rc_enc_data(&ea), rc_enc_data(&eb), la),
        "kernel output differs (%lu, %lu bytes)",
        (unsigned long) la, (unsigned long) lb);
  Fi(3,
    if (memcmp(a.st[i].w, b.st[i].w, 8 * CM_NI * sizeof(short))) same = 0;
    if (memcmp(a.st[i].sm, b.st[i].sm, 256 * sizeof *a.st[i].sm)) same = 0;
    if (memcmp(a.st[i].hist, b.st[i].hist, (sz) 1 << 12)) same = 0);
  CHECK(same, "kernel state differs");
  CHECK(!memcmp(pa, pb, sizeof pa),
        "kernel probabilities differ");
  xt_trace("kernels agree over 200000 bits, %lu coded bytes",
           (unsigned long) la);
  rc_enc_free(&ea);  rc_enc_free(&eb);
  cm_free(&a);  cm_free(&b);
}
#endif

void xt_run_unit(void) {
  int lr;
  xt_seed(&rng, 20260810UL);
  t_ilog();
  t_crc();
  t_crc32c();
  t_short_init();
  t_coder();
  t_coder_ad();
  t_packed();
  t_container();
  t_mapping();  t_file_windows();
  blr_no_mmap = 1;
  t_mapping();  t_file_windows();
  blr_no_mmap = 0;
  t_region();
#ifdef T_FORK
  t_region_dies();
#endif
  t_chunks();
  t_model();
  t_training_direction(cm_plain_scalar, "scalar training direction");
#if defined(HAVE_SSE2)
  if (blr_cpu_sse2())
    t_training_direction(cm_plain_sse2, "SSE2 training direction");
#endif
#if defined(HAVE_AVX2)
  if (blr_cpu_avx2())
    t_training_direction(cm_plain_avx2, "AVX2 training direction");
#endif
  for (lr = 1; lr <= 31; lr++) {
    t_plain_kernel(cm_bit_scalar, cm_plain_scalar, lr, "scalar plain kernel");
#if defined(HAVE_SSE2)
    if (blr_cpu_sse2()) {
      t_kernels(cm_bit_scalar, cm_bit_sse2, lr, "SSE2 mixer kernel");
      t_plain_kernel(cm_bit_scalar, cm_plain_sse2, lr, "SSE2 plain kernel");
    }
#endif
#if defined(HAVE_AVX2)
    if (blr_cpu_avx2()) {
      t_kernels(cm_bit_scalar, cm_bit_avx2, lr, "AVX2 mixer kernel");
      t_plain_kernel(cm_bit_scalar, cm_plain_avx2, lr, "AVX2 plain kernel");
    }
#endif
  }
}
