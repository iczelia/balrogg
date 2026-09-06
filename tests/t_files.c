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
#include "ogg.h"
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
    Fi(2,
      level(&o, i ? 9 : 1);
      vb_pack(*p, arc, &o);
      vb_unpack(arc, out);
      CHECK(xt_same_file(*p, out), "%s: not lossless at -%d", xt_basename(*p),
            i ? 9 : 1);
      sizes[i] = xt_file_size(arc);
      tot[i] += sizes[i]);
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
  char ** files = xt_files(".opus");
  const char * out = xt_tmp("o.out"), * re = xt_tmp("o2.blr");
  char ** arcs;
  long tot[2] = { 0, 0 }, in = 0;
  int i, n = xt_files_count(files);

  xt_section_begin("opus round trip");
  if (!n) CHECK(0, "no Opus fixtures: is BLR_TEST_DATA set?");
  arcs = xmalloc((sz) (n + 1) * sizeof *arcs);
  /*  Encode at both ends of the effort scale and keep -9 output.  */
  Fi(n,
    char tag[16];
    sprintf(tag, "o%d.blr", i);
    arcs[i] = xmalloc(strlen(xt_tmp(tag)) + 1);
    strcpy(arcs[i], xt_tmp(tag));
    CHECK(!opus_pack(files[i], arcs[i], 0), "%s: encode at depth 0 failed",
          xt_basename(files[i]));
    CHECK(!opus_unpack(arcs[i], out), "%s: decode at depth 0 failed",
          xt_basename(files[i]));
    CHECK(xt_same_file(files[i], out), "%s: not lossless at depth 0",
          xt_basename(files[i]));
    tot[0] += xt_file_size(arcs[i]);
    CHECK(!opus_pack(files[i], arcs[i], 6), "%s: encode at depth 6 failed",
          xt_basename(files[i]));
    tot[1] += xt_file_size(arcs[i]);
    in += xt_file_size(files[i]));
  /*  Decode after all encodes have completed.  */
  Fi(n,
    CHECK(!opus_unpack(arcs[i], out), "%s: decode failed", xt_basename(files[i]));
    CHECK(xt_same_file(files[i], out), "%s: not lossless in a shared process",
          xt_basename(files[i])));
  /*  Re-encode to detect retained history.  */
  Fi(n,
    CHECK(!opus_pack(files[i], re, 6), "%s: re-encode failed", xt_basename(files[i]));
    CHECK(xt_same_file(arcs[i], re), "%s: the archive depends on what was "
          "encoded before it", xt_basename(files[i]));
    xt_trace("%-24s %8ld -> %8ld", xt_basename(files[i]), xt_file_size(files[i]),
             xt_file_size(arcs[i]));
    xt_unlink(arcs[i]);  free(arcs[i]));
  xt_section_begin("effort scale");
  CHECK(!n || tot[1] <= tot[0], "depth 6 (%ld bytes) is larger than depth 0 "
        "(%ld bytes)", tot[1], tot[0]);
  xt_trace("opus: %ld -> %ld (depth 0), %ld (depth 6)", in, tot[0], tot[1]);
  xt_unlink(out);  xt_unlink(re);
  free(arcs);  xt_files_free(files);
}

/*  EOF may follow a complete packet without setting the final page's EOS
    flag. Prefixing complete copies also exercises link and header replay.  */
static void t_no_eos(void) {
  static const char * const names[] = {
    "tiny.ogg", "chain3.ogg", "chain_cont.ogg"
  };
  const char * in = xt_tmp("noeos.ogg"), * arc = xt_tmp("noeos.blr");
  const char * out = xt_tmp("noeos.out");
  int i, copies, lev, k;
  xt_section_begin("Vorbis without final EOS");
  Fi(3,
    sz n, at, got, last = 0;
    u8 * b = slurp(xt_fixture(xt_data, names[i]), &n);
    u8 * joined = xmalloc(3 * n);
    ogg_page p;
    for (at = 0; at < n; at += got) {
      got = ogg_parse(&p, b + at, n - at);
      FATAL_UNLESS(got, "invalid no-EOS fixture");
      last = at;
    }
    for (copies = 0; copies <= 2; copies += 2) {
      sz tail = (sz) copies * n + last;
      Fk(copies + 1, memcpy(joined + (sz) k * n, b, n));
      joined[tail + 5] &= (u8) ~4;
      ogg_crc_set(joined + tail, n - last);
      spew(in, joined, (sz) (copies + 1) * n);
      for (lev = 1; lev <= 9; lev += 8) {
        vb_opt o;
        archive a;
        u8 * image;
        sz len;
        level(&o, lev);
        if (copies) o.flags &= (u8) ~8;
        vb_pack(in, arc, &o);  vb_unpack(arc, out);
        CHECK(xt_same_file(in, out), "%s without EOS, %d copies, -%d",
              names[i], copies, lev);
        image = slurp(arc, &len);
        arc_parse(&a, image, len);
        arc_write(&a, out);
        CHECK(xt_same_file(arc, out), "page-count archive write is exact");
        arc_free(&a);  free(image);
        t_reemit(arc);
      }
    }
    free(joined);  free(b));
  xt_unlink(in);  xt_unlink(arc);  xt_unlink(out);
}

void xt_run_files(void) {
  { const char * in = xt_fixture(xt_data, "chain3.ogg");
    const char * arc = xt_tmp("reset.blr"), * out = xt_tmp("reset.out");
    vb_opt o;
    int i;
    xt_section_begin("non-solid model reset");
    Fi(2,
      vb_opt_default(&o);
      o.flags = (u8) (i ? 0x61 : 0);  /*  32/16 slots, no solid history  */
      o.dd = o.df = 1;
      vb_pack(in, arc, &o);  vb_unpack(arc, out);
      CHECK(xt_same_file(in, out), "sparse model pages reset between links (%d)", i));
    xt_unlink(arc);  xt_unlink(out);
  }
  t_vorbis();
  t_opus();
  t_no_eos();
}
