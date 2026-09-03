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

/*  Round-trip whole files through both codecs. Level -9 must beat -1. Repeated
    in-process encodes also detect state that was not reset between files.  */

#include "t_harness.h"
#include "archive.h"
#include "codec.h"
#include "opusmode.h"

/*  The options main.c derives from levels -1 and -9.  */
static void level(vb_opt * o, int lev) {
  vb_opt_default(o);
  if (lev == 1) { o->flags = (u8) (o->flags & 0x1F);  o->search = 0; }
  else { o->flags = (u8) ((o->flags & 0x1F) | (3 << 5));  o->search = 12; }
}

/*  Require parse and re-emission to reproduce the archive.  */
static void t_reemit(const char * arc) {
  archive a;
  sz len, out;
  u8 * b = slurp(arc, &len), * img;
  arc_parse(&a, b, len);
  img = arc_emit(&a, &out);
  CHECK(out == len && !memcmp(img, b, len), "%s: archive not re-emitted "
        "byte for byte", xt_basename(arc));
  CHECK(a.n >= 1 && a.n % 3 == 1, "%s: %lu streams is not 1 + 3k",
        xt_basename(arc), (unsigned long) a.n);
  arc_free(&a);  free(img);  free(b);
}

static void t_vorbis(void) {
  char ** files = xt_files(".ogg"), ** p;
  const char * arc = xt_tmp("v.blr"), * out = xt_tmp("v.out");
  const char * arc2 = xt_tmp("v2.blr");
  long tot[2] = { 0, 0 }, in = 0;
  vb_opt o;
  int i;

  xt_section_begin("vorbis round trip");
  if (!*files) CHECK(0, "no Vorbis fixtures: is BLR_TEST_DATA set?");
  for (p = files; *p; p++) {
    long sizes[2];
    for (i = 0; i < 2; i++) {
      level(&o, i ? 9 : 1);
      vb_pack(*p, arc, &o);
      vb_unpack(arc, out);
      CHECK(xt_same_file(*p, out), "%s: not lossless at -%d", xt_basename(*p),
            i ? 9 : 1);
      sizes[i] = xt_file_size(arc);
      tot[i] += sizes[i];
    }
    t_reemit(arc);
    /*  Output must not depend on files encoded earlier.  */
    vb_pack(*p, arc2, &o);
    CHECK(xt_same_file(arc, arc2), "%s: the archive depends on what was "
          "encoded before it", xt_basename(*p));
    in += xt_file_size(*p);
    xt_trace("%-24s %8ld -> %8ld (-1) %8ld (-9)", xt_basename(*p),
             xt_file_size(*p), sizes[0], sizes[1]);
  }
  xt_section_begin("effort scale");
  CHECK(!*files || tot[1] < tot[0], "-9 (%ld bytes) is not smaller than -1 "
        "(%ld bytes)", tot[1], tot[0]);
  xt_trace("vorbis: %ld -> %ld (-1), %ld (-9)", in, tot[0], tot[1]);
  xt_unlink(arc);  xt_unlink(arc2);  xt_unlink(out);
  xt_files_free(files);
}

static void t_opus(void) {
  char ** files = xt_files(".opus"), ** p;
  const char * out = xt_tmp("o.out"), * re = xt_tmp("o2.blr");
  char ** arcs;
  long tot[2] = { 0, 0 }, in = 0;
  int i, n = xt_files_count(files);

  xt_section_begin("opus round trip");
  if (!n) CHECK(0, "no Opus fixtures: is BLR_TEST_DATA set?");
  arcs = xmalloc((sz) (n + 1) * sizeof *arcs);
  /*  Encode at both ends of the effort scale and keep -9 output.  */
  for (i = 0, p = files; *p; p++, i++) {
    char tag[16];
    sprintf(tag, "o%d.blr", i);
    arcs[i] = xmalloc(strlen(xt_tmp(tag)) + 1);
    strcpy(arcs[i], xt_tmp(tag));
    CHECK(!opus_pack(*p, arcs[i], 0), "%s: encode at depth 0 failed", xt_basename(*p));
    CHECK(!opus_unpack(arcs[i], out), "%s: decode at depth 0 failed", xt_basename(*p));
    CHECK(xt_same_file(*p, out), "%s: not lossless at depth 0", xt_basename(*p));
    tot[0] += xt_file_size(arcs[i]);
    CHECK(!opus_pack(*p, arcs[i], 6), "%s: encode at depth 6 failed", xt_basename(*p));
    tot[1] += xt_file_size(arcs[i]);
    in += xt_file_size(*p);
  }
  /*  Decode after all encodes have completed.  */
  for (i = 0, p = files; *p; p++, i++) {
    CHECK(!opus_unpack(arcs[i], out), "%s: decode failed", xt_basename(*p));
    CHECK(xt_same_file(*p, out), "%s: not lossless in a shared process",
          xt_basename(*p));
  }
  /*  Re-encode to detect retained history.  */
  for (i = 0, p = files; *p; p++, i++) {
    CHECK(!opus_pack(*p, re, 6), "%s: re-encode failed", xt_basename(*p));
    CHECK(xt_same_file(arcs[i], re), "%s: the archive depends on what was "
          "encoded before it", xt_basename(*p));
    xt_trace("%-24s %8ld -> %8ld", xt_basename(*p), xt_file_size(*p),
             xt_file_size(arcs[i]));
    xt_unlink(arcs[i]);  free(arcs[i]);
  }
  xt_section_begin("effort scale");
  CHECK(!n || tot[1] <= tot[0], "depth 6 (%ld bytes) is larger than depth 0 "
        "(%ld bytes)", tot[1], tot[0]);
  xt_trace("opus: %ld -> %ld (depth 0), %ld (depth 6)", in, tot[0], tot[1]);
  xt_unlink(out);  xt_unlink(re);
  free(arcs);  xt_files_free(files);
}

void xt_run_files(void) {
  t_vorbis();
  t_opus();
}
