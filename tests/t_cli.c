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
#include "fmt/arc.h"
#include "sys/crc.h"
#include "vorbis/vb.h"
#include "mp3/mp3.h"
#ifdef BLR_WIN32
#include <direct.h>
#define xt_mkdir(p) _mkdir(p)
#define xt_rmdir(p) _rmdir(p)
#elif !defined(BLR_DOS)
#include <sys/stat.h>
#include <unistd.h>
#define xt_mkdir(p) mkdir(p, 0700)
#define xt_rmdir(p) rmdir(p)
#endif

static const struct regression {
  const char * name, * opts;
  int status;
  const char * msg;
} REGRESS[] = {
  { "e01-sparse-empty-book-sigfpe.ogg", "", BLR_EXIT_REFUSED, NULL },
  { "e02-audio-tail-padding.ogg", "", BLR_EXIT_OK, NULL },
  { "e03-bad-page-crc.ogg", "", BLR_EXIT_REFUSED, NULL },
  { "e04-page-after-eos.ogg", "", BLR_EXIT_REFUSED, NULL },
  { "e05-link-of-two-serials.ogg", "", BLR_EXIT_OK, NULL },
  { "e06-codebooks-of-2g.ogg", "", BLR_EXIT_REFUSED,
    "codebooks of a 3039-byte setup" },
  { "opus-frame-slack-2.opus", "", BLR_EXIT_OK, NULL },
  { "opus-frame-slack.opus", "", BLR_EXIT_OK, NULL },
  { "m01-raw-lsf-preflag.mp3", "-4", BLR_EXIT_OK, NULL },
  { "m01-raw-lsf-preflag.mp3", "-10", BLR_EXIT_OK, NULL },
  { "m01-raw-lsf-preflag.mp3", "-3", BLR_EXIT_OK, NULL },
  { "m02-scfsi-after-short.mp3", "-18", BLR_EXIT_OK, NULL },
  { "m02-scfsi-after-short.mp3", "-3", BLR_EXIT_OK, NULL },
  { "m03-lookahead-raw-lsf.mp3", "-8", BLR_EXIT_OK, NULL },
  { "m03-lookahead-raw-lsf.mp3", "-3", BLR_EXIT_OK, NULL },
  { "m04-lookahead-raw.mp3", "-8", BLR_EXIT_OK, NULL },
  { "m05-free-format-short-length.mp3", "-8", BLR_EXIT_OK, NULL },
  { NULL, NULL, 0, NULL }
};

static const struct { const char * name, * msg; } ARCREG[] = {
  { "o04-claims-16m-pages.blr", "the Opus stream ran out" },
  { "v02-gaps-claim-2-40.blr", "Ogg gap" },
  { NULL, NULL }
};

static void t_options(void) {
  const char * log = xt_tmp("cli.log");
  xt_section_begin("cli options");
  CHECK(xt_run("", log) == BLR_EXIT_USAGE, "no arguments is a usage error");
  CHECK(xt_run("frob a b", log) == BLR_EXIT_USAGE,
        "an unknown verb is a usage error");
  CHECK(xt_run("e onlyone", log) == BLR_EXIT_USAGE,
        "e with one file is a usage error");
  CHECK(xt_run("--jobs=0 -b e x", log) == BLR_EXIT_USAGE,
        "--jobs=0 is a usage error");
  CHECK(xt_run("--jobs=2x -b e x", log) == BLR_EXIT_USAGE,
        "--jobs rejects trailing text");
  CHECK(xt_run("--jobs=+2 -b e x", log) == BLR_EXIT_USAGE,
        "--jobs rejects a leading sign");
  CHECK(xt_run("--bogus", log) == BLR_EXIT_USAGE,
        "an unknown option is a usage error");
  CHECK(xt_run("e t_suite-no-such-file.ogg t_suite-out.tmp", log) ==
        BLR_EXIT_IO && xt_file_contains(log, "cannot open"),
        "a missing input exits %d", BLR_EXIT_IO);
  CHECK(xt_run("-b e", log) == BLR_EXIT_USAGE,
        "-b with no files is a usage error");
  xt_unlink(log);
}

static void t_verbs(void) {
  char args[8192];
  const char * fx = xt_fixture(xt_data, "tiny.ogg");
  const char * log = xt_tmp("cli.log"), * arc = xt_tmp("cli.blr");
  const char * arc2 = xt_tmp("cli2.blr"), * bad = xt_tmp("bad.opus");
  const char * out = xt_tmp("cli.out");
  xt_section_begin("cli verbs");
  { sz n;
    u8 * original = slurp(fx, &n);
    spew(out, original, n);
    sprintf(args, "e \"%s\" \"%s\"", out, out);
    CHECK(xt_run(args, log) == BLR_EXIT_IO && xt_same_file(fx, out),
          "input cannot be overwritten through the output path");
    xfree(original); }
  sprintf(args, "-3 e \"%s\" \"%s\"", fx, arc);
  CHECK(xt_run(args, log) == 0, "e on a Vorbis file");
  sprintf(args, "d \"%s\" \"%s\"", arc, out);
  CHECK(xt_run(args, log) == 0 && xt_same_file(fx, out),
        "d gives the file back");
  sprintf(args, "--no-mmap -3 e \"%s\" \"%s\"", fx, arc2);
  CHECK(xt_run(args, log) == 0 && xt_same_file(arc, arc2),
        "--no-mmap preserves archive bytes");
  sprintf(args, "--no-mmap d \"%s\" \"%s\"", arc, out);
  CHECK(xt_run(args, log) == 0 && xt_same_file(fx, out),
        "--no-mmap decodes mapped output");
  sprintf(args, "d \"%s\" \"%s\"", fx, out);
  CHECK(xt_run(args, log) == BLR_EXIT_REFUSED &&
        xt_file_contains(log, "not a balrogg"), "d on an .ogg is refused");
  sprintf(args, "e \"%s\" \"%s\"", arc, out);
  CHECK(xt_run(args, log) == BLR_EXIT_REFUSED &&
        xt_file_contains(log, "not Ogg"), "e on an archive is refused");
  sprintf(args, "d \"%s\" t_suite-no-such-dir/x", arc);
  CHECK(xt_run(args, log) == BLR_EXIT_IO &&
        xt_file_contains(log, "cannot create"),
        "an unwritable output exits %d", BLR_EXIT_IO);
  fx = xt_fixture(xt_data, "silk_speech_12k.opus");
  sprintf(args, "e \"%s\" \"%s\"", fx, arc);
  CHECK(xt_run(args, log) == 0, "e on an Opus file");
  sprintf(args, "e \"%s\" \"%s\"", fx, arc2);
  CHECK(xt_run(args, log) == 0 && xt_same_file(arc, arc2),
        "Opus archives are reproducible across processes");
  sprintf(args, "d \"%s\" \"%s\"", arc, out);
  CHECK(xt_run(args, log) == 0 && xt_same_file(fx, out),
        "d gives the Opus file back");
  { ogg_page p;
    sz n, pg;
    u8 * b = slurp(fx, &n);
    pg = ogg_parse(&p, b, n);
    if (pg) {
      b[4] = 1;
      ogg_crc_set(b, pg);
      spew(bad, b, n);
      sprintf(args, "e \"%s\" \"%s\"", bad, arc);
      xt_unlink(arc);
      CHECK(xt_run(args, log) == BLR_EXIT_REFUSED && xt_file_size(arc) < 0,
            "Opus input with a nonzero Ogg version is refused");
    } else CHECK(0, "cannot parse the Opus fixture");
    xfree(b); }
  xt_unlink(log);  xt_unlink(arc);  xt_unlink(arc2);  xt_unlink(bad);
  xt_unlink(out);
}

static void t_batch(void) {
  static const char * const NAMES[] = {
    "short.ogg", "chain3.ogg", "silence.ogg"
  };
  char args[8192], copy[3][64], blr[3][64];
  const char * src, * small = xt_tmp("small.inv");
  const char * large = xt_tmp("large.inv");
  const char * log = xt_tmp("cli.log"), * notogg = xt_tmp("notogg.ogg");
  int i, ok = 1;
  xt_section_begin("cli batch");
  Fi(3,
    sz n;
    u8 * b;
    char tag[32];
    sprintf(tag, "batch%d.ogg", i);
    src = xt_fixture(xt_data, NAMES[i]);
    strcpy(copy[i], xt_tmp(tag));
    strcpy(blr[i], xt_batch_name(1, copy[i]));
    CHECK(!strcmp(xt_batch_name(0, blr[i]), copy[i]),
          "batch names round-trip: %s", copy[i]);
    b = slurp(src, &n);  spew(copy[i], b, n);  xfree(b));
  sprintf(args, "--no-mmap --progress -b -2 --jobs=2 e %s %s %s", copy[0],
          copy[1], copy[2]);
  CHECK(xt_run(args, log) == 0 && xt_file_contains(log, "100%"),
        "batch encode propagates progress to workers");
  Fi(3, if (xt_file_size(blr[i]) <= 0) ok = 0);
  CHECK(ok, "batch encode wrote every .blr");
  Fi(3, xt_unlink(copy[i]));
  sprintf(args, "-b d %s %s %s", blr[0], blr[1], blr[2]);
  CHECK(xt_run(args, log) == 0, "batch decode");
  Fi(3,
    src = xt_fixture(xt_data, NAMES[i]);
    CHECK(xt_same_file(src, copy[i]), "batch decode of %s", NAMES[i]);
    xt_unlink(copy[i]);  xt_unlink(blr[i]));
  spew(notogg, (const u8 *) "not an ogg file at all", 22);
  src = xt_fixture(xt_data, NAMES[0]);
  { sz n;  u8 * b = slurp(src, &n);  spew(copy[0], b, n);  xfree(b); }
  sprintf(args, "-b e %s %s", notogg, copy[0]);
  CHECK(xt_run(args, log) == BLR_EXIT_REFUSED &&
        xt_file_contains(log, "1 of 2 failed") && xt_file_size(blr[0]) > 0,
        "batch reports the refused file and encodes the rest");
  xt_unlink(notogg);  xt_unlink(copy[0]);  xt_unlink(blr[0]);

  { sz n;  u8 * b = slurp(src, &n);  spew(copy[0], b, n);  xfree(b); }
  sprintf(args, "-b e t_suite-no-such-file.ogg %s", copy[0]);
  CHECK(xt_run(args, log) == BLR_EXIT_IO && xt_file_size(blr[0]) > 0,
        "batch preserves file access errors and continues");
  xt_unlink(copy[0]);  xt_unlink(blr[0]);

  spew(small, (const u8 *) "x", 1);
  { u8 b[64];  memset(b, 'x', sizeof b);  spew(large, b, sizeof b); }
  sprintf(args, "--jobs=1 -b e %s %s", small, large);
  CHECK(xt_run(args, log) == BLR_EXIT_REFUSED &&
        xt_file_before(log, large, small), "batch starts larger files first");
  xt_unlink(small);  xt_unlink(large);  xt_unlink(log);
}

static void t_packet_edges(void) {
  const char * input = xt_tmp("edges.ogg"), * arc = xt_tmp("edges.blr");
  const char * out = xt_tmp("edges.out"), * log = xt_tmp("edges.log");
  char args[8192];
  int i, lev;
  xt_section_begin("Vorbis packet boundaries");
  Fi(7,
    sz len, at = 0, got = 0, off = 0, oldlen = 0, newlen, bodylen, pglen;
    u8 * b = slurp(xt_fixture(xt_data, i == 6 ? "silence.ogg"
                                                   : "noise_pink.ogg"), &len);
    u8 * body, * image;
    const u8 * src;
    ogg_page p;
    int j = 0, found = 0;
    while (at < len && !found) {
      got = ogg_parse(&p, b + at, len - at);
      if (!got) break;
      src = b + at + OGG_HDRMIN + p.nseg;
      off = 0;
      Fj(p.np,
        if (!p.tail && !(p.type & 1) && j > 0 && j + 1 < p.np &&
            p.plen[j] >= (i == 6 ? 1U : 16U) && !(src[off] & 1)) {
          oldlen = p.plen[j];  found = 1;  break;
        }
        off += p.plen[j]);
      if (!found) at += got;
    }
    CHECK(found, "packet boundary fixture %d has a suitable packet", i);
    if (!found) { xfree(b);  continue; }
    newlen = i == 0 ? 1 : i == 1 ? 2 : i == 2 ? oldlen / 2
               : i == 3 ? oldlen - 1 : i == 6 ? oldlen : oldlen + 3;
    src = b + at + OGG_HDRMIN + p.nseg;
    bodylen = p.blen - oldlen + newlen;
    body = xcalloc(bodylen, 1);
    memcpy(body, src, off);
    memcpy(body + off, src + off, MIN(oldlen, newlen));
    if (i == 5) memset(body + off + oldlen, 0xA5, 3);
    if (i == 6) body[off + newlen - 1] |= 0x80;
    memcpy(body + off + newlen, src + off + oldlen, p.blen - off - oldlen);
    p.plen[j] = (u32) newlen;
    ogg_pack(&p);
    image = xmalloc(len + OGG_HDRMIN + OGG_MAXSEG + bodylen);
    memcpy(image, b, at);
    pglen = xt_ogg_emit(&p, image + at, body);
    memcpy(image + at + pglen, b + at + got, len - at - got);
    spew(input, image, len - got + pglen);
    xfree(image);  xfree(body);  xfree(b);
    for (lev = 1; lev <= 9; lev += 4) {
      int ok;
      sprintf(args, "-%d e \"%s\" \"%s\"", lev, input, arc);
      ok = xt_run(args, log) == 0;
      CHECK(ok, "packet variant %d encodes at -%d", i, lev);
      if (!ok) continue;
      sprintf(args, "d \"%s\" \"%s\"", arc, out);
      CHECK(xt_run(args, log) == 0 && xt_same_file(input, out),
            "packet variant %d is lossless at -%d", i, lev);

    });
  xt_unlink(input);  xt_unlink(arc);  xt_unlink(out);  xt_unlink(log);
}

static void t_progress(void) {
  const char * arc = xt_tmp("sample.blr");
  const char * arc2 = xt_tmp("sample2.blr"), * out = xt_tmp("sample.out");
  const char * log = xt_tmp("progress.log");
  const char * fx = xt_fixture(xt_data, "noise_pink.ogg");
  char args[8192];
  xt_section_begin("progress");
  sprintf(args, "-1 e \"%s\" \"%s\"", fx, arc);
  CHECK(xt_run(args, log) == 0, "encoding without progress");
  sprintf(args, "-p -1 e \"%s\" \"%s\"", fx, arc2);
  CHECK(xt_run(args, log) == 0 && xt_same_file(arc, arc2),
        "progress does not change archive bytes");
  fx = xt_fixture(xt_data, "silk_speech_12k.opus");
  sprintf(args, "--progress -1 e \"%s\" \"%s\"", fx, arc);
  CHECK(xt_run(args, log) == 0, "Opus encoding with progress");
  sprintf(args, "--progress d \"%s\" \"%s\"", arc, out);
  CHECK(xt_run(args, log) == 0 && xt_same_file(fx, out),
        "Opus decoding with progress");
  xt_unlink(arc);  xt_unlink(arc2);  xt_unlink(out);  xt_unlink(log);
}

static void t_archive_version(void) {
  const char * arc = xt_tmp("cli.blr"), * bad = xt_tmp("cli2.blr");
  const char * out = xt_tmp("cli.out"), * log = xt_tmp("cli.log");
  char args[8192];
  static const int versions[] = { 1, 2, 7, 0x80, 0x87, 255 };
  int i, v, j, cur;
  xt_section_begin("archive version gate");
  Fi(2,
    const char * input = xt_fixture(xt_data, i ? "silk_speech_12k.opus"
                                               : "tiny.ogg");
    sz n;  u8 * data;
    sprintf(args, "-1 e \"%s\" \"%s\"", input, arc);
    CHECK(xt_run(args, log) == 0, "codec %d writes current format", i);
    data = slurp(arc, &n);
    cur = ARC_VERSION;
    CHECK(n >= ARC_HDRLEN && data[ARC_MAGLEN] == cur &&
          data[ARC_MAGLEN + 1] == (i ? ARC_KOPUS : 0),
          "codec %d writes version %d", i, ARC_VERSION);
    Fj((int) ARRAY_LEN(versions),
      v = versions[j];
      data[ARC_MAGLEN] = (u8) v;  spew(bad, data, n);
      sprintf(args, "d \"%s\" \"%s\"", bad, out);
      CHECK(xt_run(args, log) == BLR_EXIT_REFUSED &&
            xt_file_contains(log, "unsupported archive version"),
            "codec %d rejects version %d", i, v));
    data[ARC_MAGLEN] = (u8) cur;
    { archive a;
      arc_parse(&a, data, n);
      Fj(2,
        sz len;
        u8 * image;
        a.cfg = (u8) (j ? 255 : 1);
        image = arc_emit(&a, &len);  spew(bad, image, len);  xfree(image);
        sprintf(args, "d \"%s\" \"%s\"", bad, out);
        CHECK(xt_run(args, log) == BLR_EXIT_REFUSED &&
              xt_file_contains(log, "configuration"),
              "codec %d rejects configuration byte %d", i, a.cfg));
      arc_free(&a);
    }
    xfree(data));
  xt_unlink(arc);  xt_unlink(bad);  xt_unlink(out);  xt_unlink(log);
}

void xt_run_regress(void) {
  char args[8192];
  const char * path;
  const char * log = xt_tmp("cli.log"), * arc = xt_tmp("reg.blr");
  const char * out = xt_tmp("reg.out");
  const struct regression * p;
  sz i;
  xt_section_begin("regress");
  if (!xt_binary || !xt_regress) {
    CHECK(0, "BLR and BLR_TEST_REGRESS must be set");  return;
  }
#ifdef BLR_WIN32
  _putenv("BLR_MEMCAP=512");
#else
  putenv((char *) "BLR_MEMCAP=512");
#endif
  for (p = REGRESS; p->name; p++) {
    int rc;
    path = xt_fixture(xt_regress, p->name);
    sprintf(args, "%s e \"%s\" \"%s\"", p->opts, path, arc);
    rc = xt_run(args, log);
    CHECK(rc == p->status, "%s %s: exit %d, expected %d", p->name, p->opts, rc,
          p->status);
    if (p->msg)
      CHECK(xt_file_contains(log, p->msg), "%s: no message '%s'", p->name, p->msg);
    if (rc == BLR_EXIT_OK) {
      sprintf(args, "d \"%s\" \"%s\"", arc, out);
      CHECK(xt_run(args, log) == 0 && xt_same_file(path, out),
            "%s %s: accepted but does not round-trip", p->name, p->opts);
    }
    xt_trace("%-36s %s", p->name, rc ? "refused" : "accepted");
  }
  Fi(ARRAY_LEN(ARCREG) - 1,
    path = xt_fixture(xt_regress, ARCREG[i].name);
    xt_unlink(out);
    sprintf(args, "d \"%s\" \"%s\"", path, out);
    CHECK(xt_run(args, log) == BLR_EXIT_REFUSED &&
          xt_file_contains(log, ARCREG[i].msg) && xt_file_size(out) < 0,
          "%s is not refused with '%s'", ARCREG[i].name, ARCREG[i].msg));
  xt_unlink(log);  xt_unlink(arc);  xt_unlink(out);
}

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
  if (xt_run(args, log) != 0) {
    CHECK(0, "encode for the damage test");  return;
  }
  b = slurp(arc, &n);
  Fi(ARRAY_LEN(CUT),
    sz cut = CUT[i] < 0 ? n + (sz) CUT[i] : (sz) CUT[i];
    int rc;
    if (cut >= n) continue;
    spew(bad, b, cut);
    xt_unlink(out);
    sprintf(args, "d \"%s\" \"%s\"", bad, out);
    rc = xt_run(args, log);
    CHECK(rc == BLR_EXIT_REFUSED ||
          (rc == BLR_EXIT_OK && xt_same_file(fx, out)),
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
    CHECK((rc == BLR_EXIT_REFUSED && xt_file_size(out) < 0) ||
          (rc == BLR_EXIT_OK && xt_same_file(fx, out)),
          "byte %lu flipped: exit %d", (unsigned long) at, rc));
  xfree(b);
  xt_unlink(log);  xt_unlink(arc);  xt_unlink(bad);  xt_unlink(out);
}

static void t_mp3_damaged(void) {
  static const int CFG[] = { 400, 405, 440, 441, 442, 443, 444, 445, 446 };
  static const char * const FX[] = { "cbr128_tags.mp3", "lsf22_mono.mp3" };
  char args[8192];
  const char * log = xt_tmp("mdmg.log"), * arc = xt_tmp("mdmg.blr");
  const char * bad = xt_tmp("mdmgbad.blr"), * out = xt_tmp("mdmg.out");
  int i, j, k;
  xt_section_begin("cli damaged mp3 archives");
  Fi(2, Fj((int) ARRAY_LEN(CFG),
    const char * fx = xt_fixture(xt_data, FX[i]);
    sz n;
    u8 * b, * d;
    b = slurp(fx, &n);
    arc_sum_set(crc32c(0, b, n));
    xfree(b);
    if (mp3_pack(fx, arc, CFG[j])) {
      CHECK(0, "%s: encode in configuration %d", FX[i], CFG[j]);  continue;
    }
    b = slurp(arc, &n);
    d = xmalloc(n);
    Fk(8,
      sz len = n, at = n / 2, z = ARC_MP3HDR + ARC_SUMLEN, e = ARC_MAGLEN + 2;
      sz top = ARC_MAGLEN + 4 + 3;
      int rc;
      memcpy(d, b, n);
      switch (k) {
      case 0: len = n / 2;  break;
      case 1: len = n - 7;  break;
      case 2: len = MIN(n, 48);  break;
      case 3: d[n / 3] ^= 0x5a;  break;
      case 4: memset(d + at, 0, MIN(64, n - at));  break;
      case 5: d[2 * n / 3] ^= 0x10;  break;
      case 6: d[top] = 0x7f;  break;
      default:
        d[top] = 0x7f;
        if (d[e] == 3 && z + 12 <= n) { d[z + 7] = 0x7f;  d[z + 11] = 0x7f; }
        break;
      }
      spew(bad, d, len);
      xt_unlink(out);
      sprintf(args, "d \"%s\" \"%s\"", bad, out);
      rc = xt_run(args, log);
      CHECK((rc == BLR_EXIT_REFUSED && xt_file_size(out) < 0) ||
            (rc == BLR_EXIT_OK && xt_same_file(fx, out)),
            "%s in configuration %d, damage %d: exit %d, output %s", FX[i],
            CFG[j], k, rc,
            xt_file_size(out) < 0 ? "removed" : rc ? "left" : "wrong"));
    xfree(b);  xfree(d)));
  xt_unlink(log);  xt_unlink(arc);  xt_unlink(bad);  xt_unlink(out);
}

static sz sum_at(const u8 * a, sz n) {
  if (!arc_is(a, n)) return 0;
  return arc_is_mp3(a, n) ? ARC_MP3HDR
       : n > ARC_HDRLEN ? ARC_HDRLEN + ARC_LENLEN : 0;
}

#if !defined(BLR_WIN32) && !defined(BLR_DOS)
static int piped(const char * arc, const char * out, const char * log) {
  char cmd[8192];
  const char * st = xt_tmp("sum.rc");
  long rc = -1;
  FILE * f;
  xt_unlink(st);
  sprintf(cmd, "{ \"%s\" d \"%s\" /dev/stdout 2> \"%s\"; echo $? > \"%s\"; } "
               "| cat > \"%s\"", xt_binary, arc, log, st, out);
  if (system(cmd) == -1) return -1;
  if ((f = fopen(st, "r"))) {
    if (fscanf(f, "%ld", &rc) != 1) rc = -1;
    fclose(f);
  }
  xt_unlink(st);
  return (int) rc;
}
#endif

static void t_checksum(void) {
  static const char * const FX[] = {
    "tiny.ogg", "chain3.ogg", "silence_st_64k.opus", "lsf22_mono.mp3",
    "cbr128_tags.mp3"
  };
  static const char * const LEV[] = { "-1", "-8", "-1", "-1", "-4" };
  static const char * const BT[] = { "sumb0.ogg", "sumb1.opus", "sumb2.mp3" };
  char args[8192], copy[3][64], blr[3][64];
  const char * log = xt_tmp("cli.log"), * arc = xt_tmp("cli.blr");
  const char * bad = xt_tmp("cli2.blr");
  const char * out = xt_tmp("cli.out"), * raw = xt_tmp("sum.mp3");
  sz n, m, at, i;
  u8 * a, * b;
  int k, ok;
  xt_rng r;
  xt_section_begin("cli checksums");
  Fk((int) ARRAY_LEN(FX),
    const char * fx = xt_fixture(xt_data, FX[k]);
    sprintf(args, "%s e \"%s\" \"%s\"", LEV[k], fx, arc);
    if (xt_run(args, log)) { CHECK(0, "%s: encode", FX[k]);  continue; }
    a = slurp(arc, &n);  b = slurp(fx, &m);
    at = sum_at(a, n);
    CHECK(at && n >= at + ARC_SUMLEN && rd32le(a + at) == crc32c(0, b, m),
          "%s: the archive records the CRC-32C of the file", FX[k]);
    xt_unlink(out);
    sprintf(args, "d \"%s\" \"%s\"", arc, out);
    CHECK(xt_run(args, log) == 0 && xt_same_file(fx, out), "%s: decodes",
          FX[k]);
    if (at) {
      a[at + 1] ^= 0x20;  spew(bad, a, n);  a[at + 1] ^= 0x20;
      xt_unlink(out);
      sprintf(args, "d \"%s\" \"%s\"", bad, out);
      CHECK(xt_run(args, log) == BLR_EXIT_REFUSED &&
            xt_file_contains(log, "checksum mismatch") &&
            xt_file_size(out) < 0,
            "%s: a wrong checksum is refused and leaves no output", FX[k]);
    }
    xfree(a);  xfree(b));
  xt_seed(&r, 20261101UL);
  b = xmalloc(8192);
  memcpy(b, "ID3\003\000\000\000\000\000\000", 10);
  for (i = 10; i < 8192; i++) b[i] = (u8) xt_next(&r, 256);
  spew(raw, b, 8192);
  sprintf(args, "-1 e \"%s\" \"%s\"", raw, arc);
  if (!xt_run(args, log)) {
    a = slurp(arc, &n);
    a[n / 2] ^= 0x01;  spew(bad, a, n);
    xt_unlink(out);
    sprintf(args, "d \"%s\" \"%s\"", bad, out);
    CHECK(xt_run(args, log) == BLR_EXIT_REFUSED &&
          xt_file_contains(log, "checksum mismatch") && xt_file_size(out) < 0,
          "a flipped stored byte is refused and leaves no output");
#if !defined(BLR_WIN32) && !defined(BLR_DOS)
    spew(bad, a, n);
    CHECK(piped(bad, out, log) == BLR_EXIT_REFUSED &&
          xt_file_contains(log, "checksum mismatch") &&
          xt_file_contains(log, "/dev/stdout is wrong"),
          "a pipe output is checked at its end");
    a[n / 2] ^= 0x01;  spew(bad, a, n);
    CHECK(piped(bad, out, log) == 0 && xt_same_file(raw, out),
          "a pipe output checks");
#endif
    xfree(a);
  } else CHECK(0, "encode of stored MP3 bytes");
  xfree(b);
#if !defined(BLR_WIN32) && !defined(BLR_DOS)
  sprintf(args, "-1 e \"%s\" \"%s\"", xt_fixture(xt_data, "lsf22_mono.mp3"), arc);
  CHECK(xt_run(args, log) == 0 && piped(arc, out, log) == 0 &&
        xt_same_file(xt_fixture(xt_data, "lsf22_mono.mp3"), out),
        "an MP3 archive decodes through a pipe");
  Fi(2,
    sprintf(args, "-1 e \"%s\" \"%s\"",
            xt_fixture(xt_data, i ? "silence_st_64k.opus" : "chain3.ogg"), arc);
    if (xt_run(args, log)) { CHECK(0, "encode for /dev/null");  continue; }
    sprintf(args, "d \"%s\" /dev/null", arc);
    CHECK(xt_run(args, log) == 0, "%s archive decodes to /dev/null",
          i ? "an Opus" : "a Vorbis");
    a = slurp(arc, &n);
    at = sum_at(a, n);
    a[at] ^= 0x01;  spew(bad, a, n);  xfree(a);
    sprintf(args, "d \"%s\" /dev/null", bad);
    CHECK(xt_run(args, log) == BLR_EXIT_REFUSED &&
          xt_file_contains(log, "checksum mismatch") &&
          xt_file_size("/dev/null") >= 0,
          "a wrong checksum is caught on /dev/null, which stays"));
#endif
  Fi(3,
    const char * fx = xt_fixture(xt_data, i == 0 ? "tiny.ogg"
                                          : i == 1 ? "silence_st_64k.opus"
                                          : "m25_8k.mp3");
    strcpy(copy[i], xt_tmp(BT[i]));
    strcpy(blr[i], xt_batch_name(1, copy[i]));
    b = slurp(fx, &n);  spew(copy[i], b, n);  xfree(b));
  sprintf(args, "-1 -b e \"%s\" \"%s\" \"%s\"", copy[0], copy[1], copy[2]);
  CHECK(xt_run(args, log) == 0, "batch encode");
  a = slurp(blr[1], &n);
  at = sum_at(a, n);
  if (at) a[at + 3] ^= 0x80;
  spew(blr[1], a, n);  xfree(a);
  Fi(3, xt_unlink(copy[i]));
  sprintf(args, "-b d \"%s\" \"%s\" \"%s\"", blr[0], blr[1], blr[2]);
  ok = xt_run(args, log) == BLR_EXIT_REFUSED &&
       xt_file_contains(log, "1 of 3 failed") &&
       xt_file_contains(log, "checksum mismatch") && xt_file_size(copy[1]) < 0;
  CHECK(ok && xt_same_file(xt_fixture(xt_data, "tiny.ogg"), copy[0]) &&
        xt_same_file(xt_fixture(xt_data, "m25_8k.mp3"), copy[2]),
        "a batch decode refuses the archive with a wrong checksum and keeps "
        "the others");
  Fi(3, xt_unlink(copy[i]);  xt_unlink(blr[i]));
  xt_unlink(log);  xt_unlink(arc);  xt_unlink(bad);  xt_unlink(out);
  xt_unlink(raw);
}

#ifndef BLR_DOS
static int mp3_block(const char * arc, int c, int make) {
  char p[4096];
  int k, bad = 0;
  for (k = -1; k < 10; k++) {
    strcpy(p, arc);
    if (k < 0) sprintf(strrchr(p, '.'), ".b%c", c);
    else sprintf(strrchr(p, '.'), ".b%c%d", c, k);
    if (make) bad |= xt_mkdir(p) != 0;
    else xt_rmdir(p);
  }
  return bad;
}
#endif

static void t_mp3_leftovers(void) {
#ifndef BLR_DOS
  char args[8192];
  const char * log = xt_tmp("mlo.log"), * in = xt_tmp("mlo.mp3");
  const char * arc = xt_tmp("mlo.blr");
  u8 * t = xmalloc(4096);
  xt_rng r;
  int i, rc;
  xt_section_begin("cli mp3 leftovers");
  xt_unlink(arc);
  CHECK(!mp3_block(arc, 'q', 1),
        "directories in the way of the second trial's archive");
  sprintf(args, "-2 e \"%s\" \"%s\"", xt_fixture(xt_data, "cbr128_tags.mp3"), arc);
  rc = xt_run(args, log);
  CHECK(rc == BLR_EXIT_IO && xt_file_size(arc) < 0,
        "a failed trial: exit %d, output %s", rc,
        xt_file_size(arc) < 0 ? "removed" : "left");
  mp3_block(arc, 'q', 0);
  xt_seed(&r, 3);
  memcpy(t, "ID3\3\0\0\0\0\0\0", 10);
  Fi(4086, t[10 + i] = (u8) xt_next(&r, 255));
  spew(in, t, 4096);
  xt_unlink(arc);
  CHECK(!mp3_block(arc, 'f', 1),
        "directories in the way of the fallback's archive");
  sprintf(args, "-4 e \"%s\" \"%s\"", in, arc);
  rc = xt_run(args, log);
  CHECK(rc == BLR_EXIT_IO && xt_file_size(arc) < 0,
        "a failed fallback: exit %d, output %s", rc,
        xt_file_size(arc) < 0 ? "removed" : "left");
  mp3_block(arc, 'f', 0);
  xfree(t);
  xt_unlink(log);  xt_unlink(in);  xt_unlink(arc);
#endif
}

typedef struct { u8 * b;  sz n, cap; } obuf;

static void ob_put(obuf * o, const u8 * d, sz n) {
  o->b = xgrow(o->b, &o->cap, o->n + n, 1, 65536);
  memcpy(o->b + o->n, d, n);  o->n += n;
}

static void ob_page(obuf * o, ogg_page * p, const u8 * body) {
  u8 * img = xmalloc(OGG_HDRMIN + OGG_MAXSEG + OGG_MAXSEG * OGG_MAXSEG);
  sz n;
  ogg_pack(p);
  n = xt_ogg_emit(p, img, body);
  ob_put(o, img, n);
  xfree(img);
}

static void merged_link(obuf * o, const u8 * src, sz len, int keep,
                        u32 serial) {
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
      Fi((sz) p.np, m.plen[m.np++] = p.plen[i]);
      memcpy(body + mbody, b, p.blen);  mbody += p.blen;
      if (!(b[0] & 1)) {
        m.type = (u8) (p.type & 4);  m.glo = p.glo;  m.ghi = p.ghi;
        m.serial = serial;
        m.seq = (u32) seq++;  m.tail = 0;
        ob_page(o, &m, body);  state = 2;  audio = 1;
      }
      continue;
    }
    audio++;
    if (keep && audio > keep + 1) break;
    if (keep && audio == keep + 1) p.type |= 4;
    ob_page(o, &p, b);  seq++;
  }
  xfree(body);
}

static void t_constructed(void) {
  char args[8192];
  const char * fx = xt_fixture(xt_data, "lowbr.ogg");
  const char * log = xt_tmp("cli.log"), * in = xt_tmp("con.ogg");
  const char * arc = xt_tmp("con.blr"), * out = xt_tmp("con.out");
  int i;
  ogg_page p;
  sz n, at, got, last = 0;
  u8 * b;
  obuf o;
  xt_section_begin("cli constructed files");
  b = slurp(fx, &n);

  for (at = 0; at < n; at += got) {
    got = ogg_parse(&p, b + at, n - at);
    if (!got) break;
    last = at;
  }
  o.b = NULL;  o.n = o.cap = 0;
  ob_put(&o, b, last);
  got = ogg_parse(&p, b + last, n - last);
  p.type = (u8) (p.type & ~4);
  ob_page(&o, &p, b + last + OGG_HDRMIN + p.nseg);
  spew(in, o.b, o.n);
  sprintf(args, "e \"%s\" \"%s\"", in, arc);
  CHECK(xt_run(args, log) == 0,
        "a file without an end-of-stream page encodes");
  sprintf(args, "d \"%s\" \"%s\"", arc, out);
  CHECK(xt_run(args, log) == 0 && xt_same_file(in, out),
        "a file without an end-of-stream page round-trips");
  xfree(o.b);

  o.b = NULL;  o.n = o.cap = 0;
  merged_link(&o, b, n, 0, 0x1000);
  merged_link(&o, b, n, 2, 0x1001);
  spew(in, o.b, o.n);
  sprintf(args, "-3 e \"%s\" \"%s\"", in, arc);
  CHECK(xt_run(args, log) == 0, "a merged header page encodes");
  sprintf(args, "d \"%s\" \"%s\"", arc, out);
  CHECK(xt_run(args, log) == 0 && xt_same_file(in, out),
        "a chain with merged header pages round-trips");
  xfree(o.b);

  Fi(2,
    int inserted = 0;
    u8 * pad = xcalloc(OGG_MAXSEG * OGG_MAXSEG, 1);
    u8 * body = xmalloc(OGG_MAXSEG * OGG_MAXSEG);
    o.b = NULL;  o.n = o.cap = 0;
    for (at = 0; at < n; at += got) {
      const u8 * src;
      got = ogg_parse(&p, b + at, n - at);
      if (!got) break;
      src = b + at + OGG_HDRMIN + p.nseg;
      if (!inserted && p.np && !(src[0] & 1)) {
        ogg_page first = p;
        int j;
        memcpy(pad, src, p.plen[0]);
        first.type = 0;  first.glo = first.ghi = (u32) -1;
        first.np = 1;  first.plen[0] = OGG_MAXSEG * OGG_MAXSEG;
        first.tail = 1;
        ob_page(&o, &first, pad);
        for (j = p.np; j > 0; j--) p.plen[j] = p.plen[j - 1];
        p.plen[0] = (u32) i;  p.np++;  p.type |= 1;
        body[0] = 0;
        memcpy(body + i, src, p.blen);
        src = body;
        inserted = 1;
      }
      if (inserted) p.seq++;
      ob_page(&o, &p, src);
    }
    spew(in, o.b, o.n);
    sprintf(args, "-5 e \"%s\" \"%s\"", in, arc);
    CHECK(inserted && xt_run(args, log) == 0,
          "padded continuation with %d-byte closure encodes", i);
    sprintf(args, "d \"%s\" \"%s\"", arc, out);
    CHECK(xt_run(args, log) == 0 && xt_same_file(in, out),
          "padded continuation with %d-byte closure is lossless", i);
    xfree(o.b);  xfree(body);  xfree(pad));
  xfree(b);
  xt_unlink(log);  xt_unlink(in);  xt_unlink(arc);  xt_unlink(out);
}

static void put_length(u8 * a, sz at, sz len) { wr64le(a + at - ARC_LENLEN, len); }

static void t_recorded_length(void) {
  char args[8192];
  const char * log = xt_tmp("cli.log"), * in = xt_tmp("rep.ogg");
  const char * arc = xt_tmp("rep.blr"), * bad = xt_tmp("rep2.blr");
  const char * out = xt_tmp("rep.out");
  sz n, an, at, got, len = 0;
  u8 * b = slurp(xt_fixture(xt_data, "tiny.ogg"), &n), * a;
  ogg_page p;
  obuf o;
  int k;
  xt_section_begin("cli recorded length");
  o.b = NULL;  o.n = o.cap = 0;
  Fk(64,
    for (at = 0; at < n && (got = ogg_parse(&p, b + at, n - at)) != 0;
         at += got) {
      p.serial = 0x5000 + (u32) k;
      ob_page(&o, &p, b + at + OGG_HDRMIN + p.nseg);
    });
  spew(in, o.b, o.n);
  sprintf(args, "-17 e \"%s\" \"%s\"", in, arc);
  if (xt_run(args, log)) { CHECK(0, "64 copies of a link encode");  goto done; }
  a = slurp(arc, &an);
  at = sum_at(a, an);
  len = (sz) rd64le(a + at - ARC_LENLEN);
  CHECK(len == o.n, "the archive records the length of the original");
  CHECK(an < 2 * n, "the copies are replayed");
  put_length(a, at, 2 * n);  spew(bad, a, an);
  xt_unlink(out);
  sprintf(args, "d \"%s\" \"%s\"", bad, out);
  CHECK(xt_run(args, log) == BLR_EXIT_REFUSED &&
        xt_file_contains(log, "exceed the length the archive records") &&
        xt_file_size(out) < 0,
        "replays past the recorded length are refused and leave no output");
  put_length(a, at, o.n + 1);  spew(bad, a, an);
  CHECK(xt_run(args, log) == BLR_EXIT_REFUSED &&
        xt_file_contains(log, "bytes decoded, the archive records") &&
        xt_file_size(out) < 0,
        "an output short of the recorded length is refused and leaves no "
        "output");
  xfree(a);
done:
  xfree(b);  xfree(o.b);
  xt_unlink(log);  xt_unlink(in);  xt_unlink(arc);  xt_unlink(bad);
  xt_unlink(out);
}

static sz page_at(const u8 * b, sz n, int k) {
  sz at = 0, got;
  ogg_page p;
  while (k-- > 0 && (got = ogg_parse(&p, b + at, n - at)) != 0) at += got;
  return at;
}

static void t_cut_links(void) {
  char args[8192];
  const char * log = xt_tmp("cli.log"), * in = xt_tmp("cut.ogg");
  const char * arc = xt_tmp("cut.blr"), * out = xt_tmp("cut.out");
  const char * ref = xt_tmp("cutref.blr");
  sz na, nb, a1, a2, a3, cut;
  u8 * a = slurp(xt_fixture(xt_data, "lowbr.ogg"), &na);
  u8 * b = slurp(xt_fixture(xt_data, "tiny.ogg"), &nb);
  ogg_page p;
  obuf o;
  int i;
  xt_section_begin("cli cut links");
  a1 = page_at(a, na, 1);  a2 = page_at(a, na, 2);  a3 = page_at(a, na, 3);
  cut = page_at(b, nb, 1);

  Fi(3,
    const char * dst = i < 2 ? arc : ref;
    o.b = NULL;  o.n = o.cap = 0;
    if (i < 2) ob_put(&o, b, cut);
    ob_put(&o, a, na);
    if (i) ob_put(&o, b, nb);
    ob_put(&o, a, a3);
    spew(in, o.b, o.n);
    sprintf(args, "-4 e \"%s\" \"%s\"", in, dst);
    CHECK(xt_run(args, log) == 0, "cut-link chain %d encodes", i);
    sprintf(args, "d \"%s\" \"%s\"", dst, out);
    CHECK(xt_run(args, log) == 0 && xt_same_file(in, out),
          "cut-link chain %d round-trips", i);
    xfree(o.b));
  CHECK(xt_file_size(arc) <= xt_file_size(ref) + (long) cut,
        "a cut link leaves later header references on their own setup");

  o.b = NULL;  o.n = o.cap = 0;
  ob_put(&o, a, na);
  ob_put(&o, a, a1);
  CHECK(ogg_parse(&p, a + a1, na - a1) == a2 - a1 && p.np == 2,
        "lowbr.ogg carries its comment and setup on page 1");
  { const u8 * body = a + a1 + OGG_HDRMIN + p.nseg;
    p.np = 1;  ob_page(&o, &p, body); }
  ob_put(&o, a + a2, a3 - a2);
  spew(in, o.b, o.n);
  sprintf(args, "e \"%s\" \"%s\"", in, arc);
  CHECK(xt_run(args, log) == BLR_EXIT_REFUSED &&
        xt_file_contains(log, "audio before the setup header"),
        "audio before the setup header is refused");
  xfree(o.b);
  xfree(a);  xfree(b);
  xt_unlink(log);  xt_unlink(in);  xt_unlink(arc);  xt_unlink(ref);
  xt_unlink(out);
}

static int page_list(const u8 * b, sz n, sz * at, int max) {
  ogg_page p;
  sz got;
  int k = 0;
  at[0] = 0;
  while (k < max && at[k] < n &&
         (got = ogg_parse(&p, b + at[k], n - at[k])) != 0) {
    at[k + 1] = at[k] + got;  k++;
  }
  return k;
}

static void junk(obuf * o, xt_rng * r, sz n) {
  sz i;
  Fi(n, u8 c = (u8) (i % 7 ? xt_next(r, 256) : 0);  ob_put(o, &c, 1));
}

static int odd_rt(const char * in, const char * what, int gaps) {
  char args[8192];
  const char * log = xt_tmp("odd.log"), * arc = xt_tmp("odd.blr");
  const char * out = xt_tmp("odd.out");
  int ok;
  sz n;
  u8 * a;
  xt_unlink(arc);  xt_unlink(out);
  sprintf(args, "e \"%s\" \"%s\"", in, arc);
  if (xt_run(args, log)) {
    CHECK(xt_file_size(arc) < 0, "%s: a refused encode leaves its output", what);
    xt_unlink(log);
    return 0;
  }
  a = slurp(arc, &n);
  CHECK(n > ARC_MAGLEN + 1 && a[ARC_MAGLEN] == ARC_VERSION &&
        (a[ARC_MAGLEN + 1] & ARC_KGAPS) == (gaps ? ARC_KGAPS : 0),
        "%s: archive kind %d, %s gaps", what,
        n > ARC_MAGLEN + 1 ? a[ARC_MAGLEN + 1] : -1, gaps ? "with" : "without");
  xfree(a);
  sprintf(args, "d \"%s\" \"%s\"", arc, out);
  ok = xt_run(args, log) == 0 && xt_same_file(in, out);
  xt_unlink(arc);  xt_unlink(out);  xt_unlink(log);
  return ok;
}

static void t_odd_pages(void) {
  const char * in = xt_tmp("odd.ogg");
  sz na, nb, no, at[64];
  u8 * a = slurp(xt_fixture(xt_data, "lowbr.ogg"), &na);
  u8 * b = slurp(xt_fixture(xt_data, "tiny.ogg"), &nb);
  u8 * op = slurp(xt_fixture(xt_data, "silk_mono_16k.opus"), &no);
  int np = page_list(a, na, at, 63), i, k;
  ogg_page p;
  obuf o;
  xt_rng r;
  xt_section_begin("cli odd pages");
  xt_seed(&r, 7);

  o.b = NULL;  o.n = o.cap = 0;
  Fk(np,
    ob_put(&o, a + at[k], at[k + 1] - at[k]);
    if (k < 2) junk(&o, &r, k ? 82 : 966));
  junk(&o, &r, 17);
  spew(in, o.b, o.n);
  CHECK(odd_rt(in, "bytes between Vorbis pages", 1),
        "bytes between Vorbis pages do not round-trip");
  xfree(o.b);

  CHECK(odd_rt(xt_fixture(xt_data, "lowbr.ogg"), "lowbr.ogg", 0),
        "lowbr.ogg does not round-trip");
  o.b = NULL;  o.n = o.cap = 0;
  ob_put(&o, a, na);  ob_put(&o, a + at[np - 1], at[np] - at[np - 1]);
  o.b[o.n - 1] ^= 0x40;
  spew(in, o.b, o.n);
  CHECK(odd_rt(in, "a bad trailing page", 1),
        "a trailing page with a bad CRC does not round-trip");
  xfree(o.b);

  o.b = NULL;  o.n = o.cap = 0;
  ob_put(&o, a, at[np - 1]);
  ogg_parse(&p, a + at[np - 1], na - at[np - 1]);
  p.type = (u8) (p.type & ~4);
  ob_page(&o, &p, a + at[np - 1] + OGG_HDRMIN + p.nseg);
  p.type = 4;  p.seq++;  p.np = 1;  p.plen[0] = 0;  p.tail = 0;
  ob_page(&o, &p, a);
  spew(in, o.b, o.n);
  CHECK(odd_rt(in, "an empty packet", 0),
        "an empty packet on the last page does not round-trip");
  xfree(o.b);

  o.b = NULL;  o.n = o.cap = 0;
  ob_put(&o, a, na);
  ogg_parse(&p, a + at[np - 1], na - at[np - 1]);
  Fk(3,
    p.seq++;  p.glo += 1024;
    ob_page(&o, &p, a + at[np - 1] + OGG_HDRMIN + p.nseg));
  ob_put(&o, b, nb);  junk(&o, &r, 40);  ob_put(&o, a, na);
  spew(in, o.b, o.n);
  CHECK(odd_rt(in, "pages after the end of stream", 1),
        "pages after the end of stream do not round-trip");
  xfree(o.b);

  np = page_list(op, no, at, 63);
  o.b = NULL;  o.n = o.cap = 0;
  Fk(np,
    ob_put(&o, op + at[k], at[k + 1] - at[k]);
    if (k == 1 || k == np / 2) junk(&o, &r, 300));
  spew(in, o.b, o.n);
  CHECK(odd_rt(in, "bytes between Opus pages", 1),
        "bytes between Opus pages do not round-trip");
  xfree(o.b);
  o.b = NULL;  o.n = o.cap = 0;
  ob_put(&o, op, no);
  i = np / 2;
  o.b[at[i] + 5] |= 2;  ogg_crc_set(o.b + at[i], at[i + 1] - at[i]);
  spew(in, o.b, o.n);
  CHECK(odd_rt(in, "a stray Opus BOS", 0),
        "an Opus audio page with BOS does not round-trip");

  ob_put(&o, op, at[1]);
  o.b[no + 18] = 2;  o.b[no + 14] ^= 1;  ogg_crc_set(o.b + no, at[1]);
  spew(in, o.b, o.n);
  CHECK(!odd_rt(in, "an unsupported second link", 0),
        "an unsupported channel mapping in a later link is accepted");
  xfree(o.b);
  xfree(a);  xfree(b);  xfree(op);
  xt_unlink(in);
}

static void t_codec_choices(void) {
  static const char * const floors[] = { "flooralt.ogg", "floorhi.ogg" };
  const char * input = xt_tmp("edges.ogg"), * arc = xt_tmp("edges.blr");
  const char * out = xt_tmp("edges.out"), * log = xt_tmp("edges.log");
  char args[8192];
  int variant, lev;
  xt_section_begin("valid codec choices");
  for (variant = -1; variant < 9; variant++) {
    if (variant < 2) {
      sz n, at = 0, got;
      u8 * b = slurp(xt_fixture(xt_data, floors[MAX(0, variant)]), &n);
      obuf o = { NULL, 0, 0 };
      ogg_page p;
      while ((got = ogg_parse(&p, b + at, n - at)) != 0) {
        const u8 * src = b + at + OGG_HDRMIN + p.nseg;
        if (p.type & 4) {
          u8 body[600], packet[3];
          int i, j;
          CHECK(p.plen[0] == sizeof packet, "three-byte floor fixture");
          memcpy(packet, src, sizeof packet);
          if (variant < 0) packet[2] &= (u8) ~12;
          Fi(200, memcpy(body + 3 * i, packet, 3));
          p.np = 200;
          Fi(p.np, p.plen[i] = 3);
          Fj(100,
            p.seq = (u32) j + 2;  p.type = j == 99 ? 4 : 0;
            p.glo = (u32) (j + 1) * 6400 - 32;  p.ghi = 0;
            ob_page(&o, &p, body));
        } else ob_page(&o, &p, src);
        at += got;  if (at == n) break;
      }
      spew(input, o.b, o.n);  xfree(o.b);  xfree(b);
    } else {
      sz len, at = 0, got;
      u8 * b = slurp(xt_fixture(xt_data, "celt_st_128k.opus"), &len);
      u8 * body = xmalloc(2 * 65025), * pad = xmalloc(61440);
      obuf o = { NULL, 0, 0 };
      ogg_page p;
      u32 seq = 0;
      int changed = 0;
      while ((got = ogg_parse(&p, b + at, len - at)) != 0) {
        const u8 * src = b + at + OGG_HDRMIN + p.nseg;
        if (variant < 5 && p.seq == 1) {
          sz n = variant == 2 ? 4095 : variant == 3 ? 4096 : 7062;
          CHECK(p.np == 1 && p.blen < n, "OpusTags fixture fits padding");
          memcpy(body, src, p.blen);  memset(body + p.blen, 0, n - p.blen);
          p.plen[0] = (u32) n;  src = body;  changed = 1;
        } else if (variant >= 5 && p.seq == 2) {
          sz old = p.plen[0], n, extra, nhdr = 2, i;
          CHECK(p.np > 1 && (src[0] & 3) == 0, "single-frame Opus fixture");
          n = variant == 8 ? 61440 : old + 2 + (sz) (variant + 6) * 255;
          extra = n - old - 2;
          pad[0] = src[0] | 3;  pad[1] = 0x41;
          Fi(extra / 255, pad[nhdr++] = 255);
          pad[nhdr++] = (u8) (extra % 255);
          memcpy(pad + nhdr, src + 1, old - 1);
          memset(pad + nhdr + old - 1, 0, n - nhdr - old + 1);
          if (variant == 8) {
            ogg_page first = p;
            first.type = 0;  first.np = 1;  first.plen[0] = (u32) n;
            first.tail = 0;  first.glo = 960;  first.ghi = 0;
            first.seq = seq++;
            ob_page(&o, &first, pad);
            for (i = 1; i < (sz) p.np; i++) p.plen[i - 1] = p.plen[i];
            p.np--;  src += old;
          } else {
            memcpy(body, pad, n);  memcpy(body + n, src + old, p.blen - old);
            p.plen[0] = (u32) n;  src = body;
          }
          changed = 1;
        }
        p.seq = seq++;  ob_page(&o, &p, src);  at += got;
        if (at == len) break;
      }
      CHECK(changed && at == len, "Opus variant %d constructed", variant);
      spew(input, o.b, o.n);  xfree(o.b);  xfree(body);  xfree(pad);  xfree(b);
    }
    for (lev = 1; lev <= 9; lev += 4) {
      int ok;
      sprintf(args, "-%d e \"%s\" \"%s\"", lev, input, arc);
      ok = xt_run(args, log) == 0;
      CHECK(ok, "valid codec variant %d encodes at -%d", variant, lev);
      if (!ok) continue;
      if (variant < 2) {
        static long canonical[2];
        long size = xt_file_size(arc);
        if (variant < 0) canonical[lev == 9] = size;
        else CHECK(size <= canonical[lev == 9] + 128,
                   "floor variant %d retains canonical compression at -%d",
                   variant, lev);
        CHECK(size < xt_file_size(input) / 20,
              "floor variant %d compresses repeated exceptional packets at -%d",
              variant, lev);
      }
      sprintf(args, "d \"%s\" \"%s\"", arc, out);
      CHECK(xt_run(args, log) == 0 && xt_same_file(input, out),
            "valid codec variant %d is lossless at -%d", variant, lev);
    }
  }
  xt_unlink(input);  xt_unlink(arc);  xt_unlink(out);  xt_unlink(log);
}

static void t_levels(void) {
  static const char * const NAMES[] = {
    "lowbr.ogg", "sweep.ogg", "chain3.ogg", "vibrato.ogg",
    "silk_speech_12k.opus", "silk_mono_16k.opus", "hyb_mono_40k.opus",
    "celt_st_128k.opus"
  };
  static const char * const BAD[] = { "-0", "-21", "-100", "-04x" };
  const char * arc = xt_tmp("lev.blr");
  const char * out = xt_tmp("lev.out"), * log = xt_tmp("lev.log");
  char args[8192];
  int i, lev;
  xt_section_begin("cli levels");
  Fi(8,
    const char * in = xt_fixture(xt_data, NAMES[i]);
    for (lev = 1; lev <= 20; lev++) {
      sprintf(args, "-%d e \"%s\" \"%s\"", lev, in, arc);
      CHECK(xt_run(args, log) == 0, "%s encodes at -%d", NAMES[i], lev);
      sprintf(args, "d \"%s\" \"%s\"", arc, out);
      CHECK(xt_run(args, log) == 0 && xt_same_file(in, out),
            "%s is lossless at -%d", NAMES[i], lev);
    });
  Fi(4,
    sprintf(args, "%s e \"%s\" \"%s\"", BAD[i], xt_fixture(xt_data, NAMES[0]), arc);
    CHECK(xt_run(args, log) == BLR_EXIT_USAGE, "%s is a usage error", BAD[i]));
  xt_unlink(arc);  xt_unlink(out);  xt_unlink(log);
}

void xt_run_cli(void) {
  if (!xt_binary || !xt_data) {
    xt_section_begin("cli");
    CHECK(0, "BLR and BLR_TEST_DATA must be set");
    return;
  }
  t_options();
  t_verbs();
  t_batch();
  t_damaged();
  t_mp3_damaged();
  t_checksum();
  t_mp3_leftovers();
  t_constructed();
  t_recorded_length();
  t_cut_links();
  t_odd_pages();
  t_packet_edges();
  t_progress();
  t_archive_version();
  t_codec_choices();
  t_levels();
}
