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

/*  Command-line and regression tests.  */

#include "t_harness.h"
#include "ogg.h"

static const char * const REGRESS[] = {
  "d01-codeword-length-238.blr", "d02-decode-oom.blr", "d03-decode-oom.blr",
  "d04-decode-oom.blr", "d05-decode-oom.blr", "d06-decode-oom.blr",
  "d07-decode-oom.blr", "d08-decode-oom.blr", "d09-decode-oom.blr",
  "d10-codeword-length-current.blr", "d11-codeword-length-33.blr",
  "e01-sparse-empty-book-sigfpe.ogg", "e02-audio-tail-padding.ogg",
  "e03-bad-page-crc.ogg", "e04-page-after-eos.ogg",
  "opus-frame-slack-2.opus", "opus-frame-slack.opus", NULL
};

static int has_opus;

static void t_options(void) {
  const char * log = xt_tmp("cli.log");
  xt_section_begin("cli options");
  CHECK(xt_run("-v", log) == 0
        && xt_file_contains(log, "demonically compacting OGG recompressor")
        && xt_file_contains(log, "Written by Kamila Szewczyk")
        && xt_file_contains(log, "GNU GPL version 3"), "-v banner");
  has_opus = !xt_file_contains(log, "not built");
  CHECK(xt_file_contains(log, "SIMD: ") && xt_file_contains(log, " dispatched"),
        "-v names the mixer kernel");
  CHECK(xt_run("--version", log) == 0, "--version");
  CHECK(xt_run("-h", log) == 0 && xt_file_contains(log, "usage:")
        && xt_file_contains(log, "demonically compacting OGG recompressor")
        && xt_file_contains(log, "Written by Kamila Szewczyk")
        && xt_file_contains(log, "GNU GPL version 3"), "-h banner");
  CHECK(xt_run("", log) == BLR_EXIT_USAGE, "no arguments is a usage error");
  CHECK(xt_run("frob a b", log) == BLR_EXIT_USAGE, "an unknown verb is a usage error");
  CHECK(xt_run("e onlyone", log) == BLR_EXIT_USAGE, "e with one file is a usage error");
  CHECK(xt_run("--jobs=0 -b e x", log) == BLR_EXIT_USAGE, "--jobs=0 is a usage error");
  CHECK(xt_run("--jobs=2x -b e x", log) == BLR_EXIT_USAGE,
        "--jobs rejects trailing text");
  CHECK(xt_run("--jobs=+2 -b e x", log) == BLR_EXIT_USAGE,
        "--jobs rejects a leading sign");
  CHECK(xt_run("--bogus", log) == BLR_EXIT_USAGE, "an unknown option is a usage error");
  CHECK(xt_run("e t_suite-no-such-file.ogg t_suite-out.tmp", log) == BLR_EXIT_IO
        && xt_file_contains(log, "cannot open"), "a missing input exits %d", BLR_EXIT_IO);
  CHECK(xt_run("dump t_suite-no-such-file.blr", log) == BLR_EXIT_IO, "dump on a missing file");
  CHECK(xt_run("pages t_suite-no-such-file.ogg", log) == BLR_EXIT_IO, "pages on a missing file");
  CHECK(xt_run("-b e", log) == BLR_EXIT_USAGE, "-b with no files is a usage error");
  xt_unlink(log);
}

static void t_verbs(void) {
  char args[8192];
  const char * fx = xt_fixture(xt_data, "tiny.ogg");
  const char * log = xt_tmp("cli.log"), * arc = xt_tmp("cli.blr");
  const char * arc2 = xt_tmp("cli2.blr"), * bad = xt_tmp("bad.opus");
  const char * out = xt_tmp("cli.out");
  xt_section_begin("cli verbs");
  {
    sz n;
    u8 * original = slurp(fx, &n);
    spew(out, original, n);
    sprintf(args, "e \"%s\" \"%s\"", out, out);
    CHECK(xt_run(args, log) == BLR_EXIT_IO && xt_same_file(fx, out),
          "input cannot be overwritten through the output path");
    free(original);
  }
  sprintf(args, "-3 e \"%s\" \"%s\"", fx, arc);
  CHECK(xt_run(args, log) == 0, "e on a Vorbis file");
  sprintf(args, "d \"%s\" \"%s\"", arc, out);
  CHECK(xt_run(args, log) == 0 && xt_same_file(fx, out), "d gives the file back");
  sprintf(args, "dump \"%s\"", arc);
  CHECK(xt_run(args, log) == 0 && xt_file_contains(log, "streams")
        && xt_file_contains(log, "level 2"), "dump prints the layout");
  sprintf(args, "pages \"%s\"", fx);
  CHECK(xt_run(args, log) == 0 && xt_file_contains(log, "reframes")
        && !xt_file_contains(log, "MISMATCH"), "pages reframes every page");
  /*  Refuse the wrong file type for each command.  */
  sprintf(args, "d \"%s\" \"%s\"", fx, out);
  CHECK(xt_run(args, log) == BLR_EXIT_REFUSED && xt_file_contains(log, "not a balrogg"),
        "d on an .ogg is refused");
  sprintf(args, "e \"%s\" \"%s\"", arc, out);
  CHECK(xt_run(args, log) == BLR_EXIT_REFUSED && xt_file_contains(log, "not Ogg"),
        "e on an archive is refused");
  sprintf(args, "pages \"%s\"", arc);
  CHECK(xt_run(args, log) == BLR_EXIT_REFUSED, "pages on a non-Ogg file is refused");
  sprintf(args, "d \"%s\" t_suite-no-such-dir/x", arc);
  CHECK(xt_run(args, log) == BLR_EXIT_IO && xt_file_contains(log, "cannot create"),
        "an unwritable output exits %d", BLR_EXIT_IO);
  if (has_opus) {
    fx = xt_fixture(xt_data, "silk_speech_12k.opus");
    sprintf(args, "e \"%s\" \"%s\"", fx, arc);
    CHECK(xt_run(args, log) == 0, "e on an Opus file");
    sprintf(args, "e \"%s\" \"%s\"", fx, arc2);
    CHECK(xt_run(args, log) == 0 && xt_same_file(arc, arc2),
          "Opus archives are reproducible across processes");
    sprintf(args, "d \"%s\" \"%s\"", arc, out);
    CHECK(xt_run(args, log) == 0 && xt_same_file(fx, out), "d gives the Opus file back");
    sprintf(args, "dump \"%s\"", arc);
    CHECK(xt_run(args, log) == 0 && xt_file_contains(log, "Opus mode"),
          "dump names the Opus mode");
    {
      ogg_page p;
      sz n, pg;
      u8 * b = slurp(fx, &n);
      pg = ogg_parse(&p, b, n);
      if (pg) {
        b[4] = 1;
        ogg_crc_set(b, pg);
        spew(bad, b, n);
        sprintf(args, "e \"%s\" \"%s\"", bad, arc);
        CHECK(xt_run(args, log) == BLR_EXIT_REFUSED
              && xt_file_contains(log, "unsupported Ogg version"),
              "Opus input with a nonzero Ogg version is refused");
      } else CHECK(0, "cannot parse the Opus fixture");
      free(b);
    }
  }
  xt_unlink(log);  xt_unlink(arc);  xt_unlink(arc2);  xt_unlink(bad);
  xt_unlink(out);
}

/*  Encode and decode three fixture copies in place.  */
static void t_batch(void) {
  static const char * const NAMES[] = { "short.ogg", "chain3.ogg", "silence.ogg" };
  char args[8192], copy[3][64], blr[3][64];
  const char * src, * small = xt_tmp("small.inv"), * large = xt_tmp("large.inv");
  const char * log = xt_tmp("cli.log"), * notogg = xt_tmp("notogg.ogg");
  int i, ok = 1;
  xt_section_begin("cli batch");
  Fi(3,
    sz n;
    u8 * b;
    char tag[16];
    sprintf(tag, "batch%d.ogg", i);
    src = xt_fixture(xt_data, NAMES[i]);
    strcpy(copy[i], xt_tmp(tag));
    strcpy(blr[i], xt_batch_name(1, copy[i]));
    CHECK(!strcmp(xt_batch_name(0, blr[i]), copy[i]),
          "batch names round-trip: %s", copy[i]);
    b = slurp(src, &n);  spew(copy[i], b, n);  free(b));
  sprintf(args, "-b -2 --jobs=2 e %s %s %s", copy[0], copy[1], copy[2]);
  CHECK(xt_run(args, log) == 0, "batch encode");
  Fi(3, if (xt_file_size(blr[i]) <= 0) ok = 0);
  CHECK(ok, "batch encode wrote every .blr");
  Fi(3, xt_unlink(copy[i]));
  sprintf(args, "-b d %s %s %s", blr[0], blr[1], blr[2]);
  CHECK(xt_run(args, log) == 0, "batch decode");
  Fi(3,
    src = xt_fixture(xt_data, NAMES[i]);
    CHECK(xt_same_file(src, copy[i]), "batch decode of %s", NAMES[i]);
    xt_unlink(copy[i]);  xt_unlink(blr[i]));
  /*  A batch with a refused file reports it and carries on.  */
  spew(notogg, (const u8 *) "not an ogg file at all", 22);
  src = xt_fixture(xt_data, NAMES[0]);
  { sz n;  u8 * b = slurp(src, &n);  spew(copy[0], b, n);  free(b); }
  sprintf(args, "-b e %s %s", notogg, copy[0]);
  CHECK(xt_run(args, log) == BLR_EXIT_REFUSED && xt_file_contains(log, "1 of 2 failed")
        && xt_file_size(blr[0]) > 0, "batch reports the refused file and encodes the rest");
  xt_unlink(notogg);  xt_unlink(copy[0]);  xt_unlink(blr[0]);

  { sz n;  u8 * b = slurp(src, &n);  spew(copy[0], b, n);  free(b); }
  sprintf(args, "-b e t_suite-no-such-file.ogg %s", copy[0]);
  CHECK(xt_run(args, log) == BLR_EXIT_IO && xt_file_size(blr[0]) > 0,
        "batch preserves file access errors and continues");
  xt_unlink(copy[0]);  xt_unlink(blr[0]);

  /*  A serial batch exposes the scheduler's order.  */
  spew(small, (const u8 *) "x", 1);
  { u8 b[64];  memset(b, 'x', sizeof b);  spew(large, b, sizeof b); }
  sprintf(args, "--jobs=1 -b e %s %s", small, large);
  CHECK(xt_run(args, log) == BLR_EXIT_REFUSED
        && xt_file_before(log, large, small), "batch starts larger files first");
  xt_unlink(small);  xt_unlink(large);  xt_unlink(log);
}

static void t_regress(void) {
  char args[8192];
  const char * path;
  const char * log = xt_tmp("cli.log"), * arc = xt_tmp("reg.blr");
  const char * out = xt_tmp("reg.out");
  const char * const * p;
  xt_section_begin("regress");
  if (!xt_regress) { CHECK(0, "BLR_TEST_REGRESS is not set");  return; }
  /*  Refuse excessive allocations through the cap or parser guards.  */
#ifdef BLR_WIN32
  _putenv("BLR_MEMCAP=512");
#else
  putenv((char *) "BLR_MEMCAP=512");
#endif
  for (p = REGRESS; *p; p++) {
    sz n = strlen(*p);
    int enc = !(n > 4 && !strcmp(*p + n - 4, ".blr")), rc;
    path = xt_fixture(xt_regress, *p);
    sprintf(args, "%s \"%s\" \"%s\"", enc ? "e" : "d", path, arc);
    rc = xt_run(args, log);
    CHECK(rc == BLR_EXIT_OK || rc == BLR_EXIT_REFUSED, "%s: exit %d", *p, rc);
    /*  A Vorbis-only build refuses the Opus inputs, and must say so.  */
    if (enc && !has_opus && strstr(*p, "opus"))
      CHECK(rc == BLR_EXIT_REFUSED, "%s: accepted without an Opus mode", *p);
    if (rc == BLR_EXIT_OK && enc) {
      sprintf(args, "d \"%s\" \"%s\"", arc, out);
      CHECK(xt_run(args, log) == 0 && xt_same_file(path, out),
            "%s: accepted but does not round-trip", *p);
    }
    xt_trace("%-36s %s", *p, rc ? "refused" : "accepted");
  }
  xt_unlink(log);  xt_unlink(arc);  xt_unlink(out);
}

/*  Reject damaged archives without crashing.  */
static void t_damaged(void) {
  char args[8192];
  const char * fx = xt_fixture(xt_data, "short.ogg");
  const char * log = xt_tmp("cli.log"), * arc = xt_tmp("dmg.blr");
  const char * bad = xt_tmp("dmgbad.blr"), * out = xt_tmp("dmg.out");
  static const long CUT[] = { 9, 10, 12, 20, -2, -1 };
  u8 * b;
  sz n, i, k;
  xt_section_begin("cli damaged archives");
  sprintf(args, "-2 e \"%s\" \"%s\"", fx, arc);
  if (xt_run(args, log) != 0) { CHECK(0, "encode for the damage test");  return; }
  b = slurp(arc, &n);
  Fi(sizeof CUT / sizeof *CUT,
    sz cut = CUT[i] < 0 ? n + (sz) CUT[i] : (sz) CUT[i];
    int rc;
    if (cut >= n) continue;
    spew(bad, b, cut);
    xt_unlink(out);
    sprintf(args, "d \"%s\" \"%s\"", bad, out);
    rc = xt_run(args, log);
    CHECK(rc == BLR_EXIT_REFUSED || (rc == BLR_EXIT_OK && xt_same_file(fx, out)),
          "archive truncated to %lu bytes: exit %d", (unsigned long) cut, rc));
  Fk(2,
    sz at = k ? n - 3 : (sz) 12;
    int rc;
    if (at >= n) continue;
    b[at] ^= 0x55;
    spew(bad, b, n);
    b[at] ^= 0x55;
    xt_unlink(out);
    sprintf(args, "d \"%s\" \"%s\"", bad, out);
    rc = xt_run(args, log);
    CHECK(rc == BLR_EXIT_REFUSED || rc == BLR_EXIT_OK, "byte %lu flipped: exit %d",
          (unsigned long) at, rc);
    /*  Undetectable corruption may change output, so check only the exit path.  */);
  free(b);
  xt_unlink(log);  xt_unlink(arc);  xt_unlink(bad);  xt_unlink(out);
}

/*  Test a missing EOS marker and chained links with mixed header/audio pages.  */
typedef struct { u8 * b;  sz n, cap; } obuf;

static void ob_put(obuf * o, const u8 * d, sz n) {
  while (o->n + n > o->cap) { o->cap = o->cap ? 2 * o->cap : 65536;  o->b = xrealloc(o->b, o->cap); }
  memcpy(o->b + o->n, d, n);  o->n += n;
}

static void ob_page(obuf * o, ogg_page * p, const u8 * body) {
  u8 * img = xmalloc(OGG_HDRMIN + OGG_MAXSEG + OGG_MAXSEG * OGG_MAXSEG);
  sz n;
  ogg_pack(p);
  n = ogg_emit(p, img, body);
  ob_put(o, img, n);
  free(img);
}

/*  Merge later headers with the first audio page and retain `keep` more pages.  */
static void merged_link(obuf * o, const u8 * src, sz len, int keep, u32 serial) {
  ogg_page p, m;
  sz at = 0, got, mbody = 0, i;
  u8 * body = xmalloc(4 * OGG_MAXSEG * OGG_MAXSEG);
  int state = 0, audio = 0, seq = 0;
  memset(&m, 0, sizeof m);
  while (at < len) {
    const u8 * b;
    got = ogg_parse(&p, src + at, len - at);
    if (!got) break;
    b = src + at + OGG_HDRMIN + p.nseg;
    at += got;
    p.serial = serial;  p.seq = (u32) seq;
    if (state == 0) { ob_page(o, &p, b);  seq++;  state = 1;  continue; }
    if (state == 1) {
      /*  Accumulate packets until the first audio page is folded in.  */
      Fi((sz) p.np, m.plen[m.np++] = p.plen[i]);
      memcpy(body + mbody, b, p.blen);  mbody += p.blen;
      if (!(b[0] & 1)) {
        m.type = (u8) (p.type & 4);  m.glo = p.glo;  m.ghi = p.ghi;  m.serial = serial;
        m.seq = (u32) seq++;  m.tail = 0;
        ob_page(o, &m, body);  state = 2;  audio = 1;
      }
      continue;
    }
    /*  `keep` more audio pages after the merged one, the last made EOS.  */
    audio++;
    if (keep && audio > keep + 1) break;
    if (keep && audio == keep + 1) p.type |= 4;
    ob_page(o, &p, b);  seq++;
  }
  free(body);
}

static void t_constructed(void) {
  char args[8192];
  const char * fx = xt_fixture(xt_data, "lowbr.ogg");
  const char * log = xt_tmp("cli.log"), * in = xt_tmp("con.ogg");
  const char * arc = xt_tmp("con.blr"), * out = xt_tmp("con.out");
  ogg_page p;
  sz n, at, got, last = 0;
  u8 * b;
  obuf o;
  xt_section_begin("cli constructed files");
  b = slurp(fx, &n);

  /*  No end-of-stream page.  */
  for (at = 0; at < n; at += got) { got = ogg_parse(&p, b + at, n - at);  if (!got) break;  last = at; }
  o.b = NULL;  o.n = o.cap = 0;
  ob_put(&o, b, last);
  got = ogg_parse(&p, b + last, n - last);
  p.type = (u8) (p.type & ~4);
  ob_page(&o, &p, b + last + OGG_HDRMIN + p.nseg);
  spew(in, o.b, o.n);
  sprintf(args, "e \"%s\" \"%s\"", in, arc);
  CHECK(xt_run(args, log) == BLR_EXIT_REFUSED && xt_file_contains(log, "end-of-stream"),
        "a file without an end-of-stream page is refused");
  free(o.b);

  /*  Two links sharing a merged header page, of different lengths.  */
  o.b = NULL;  o.n = o.cap = 0;
  merged_link(&o, b, n, 0, 0x1000);
  merged_link(&o, b, n, 2, 0x1001);
  spew(in, o.b, o.n);
  sprintf(args, "-3 e \"%s\" \"%s\"", in, arc);
  CHECK(xt_run(args, log) == 0, "a merged header page encodes");
  sprintf(args, "d \"%s\" \"%s\"", arc, out);
  CHECK(xt_run(args, log) == 0 && xt_same_file(in, out),
        "a chain with merged header pages round-trips");
  free(o.b);  free(b);
  xt_unlink(log);  xt_unlink(in);  xt_unlink(arc);  xt_unlink(out);
}

void xt_run_cli(void) {
  if (!xt_binary || !xt_data) {
    xt_section_begin("cli");
    xt_trace("BLR or BLR_TEST_DATA is unset: the command-line tests are skipped");
    return;
  }
  t_options();
  t_verbs();
  t_batch();
  t_damaged();
  t_constructed();
  t_regress();
}
