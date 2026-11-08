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
#include "fmt/arc.h"
#include "vorbis/vorbis.h"
#include "fmt/ogg.h"
#include "opus/opus.h"
#include "mp3/mp3.h"

static const int VB_ENDS[2] = { 126, 142 };

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
  arc_free(&a);  xfree(img);  xfree(b);
}

static void t_vorbis(void) {
  char ** files = xt_files(".ogg"), ** p;
  const char * arc = xt_tmp("v.blr"), * out = xt_tmp("v.out");
  const char * arc2 = xt_tmp("v2.blr");
  long tot[2] = { 0, 0 }, in = 0;
  int i;

  xt_section_begin("vorbis round trip");
  if (!*files) CHECK(0, "no Vorbis fixtures: is BLR_TEST_DATA set?");
  for (p = files; *p; p++) {
    long sizes[2];
    Fi(2,
      vb_pack(*p, arc, VB_ENDS[i]);
      vb_unpack(arc, out);
      CHECK(xt_same_file(*p, out), "%s: not lossless in configuration %d",
            xt_basename(*p), VB_ENDS[i]);
      sizes[i] = xt_file_size(arc);
      tot[i] += sizes[i]);
    t_reemit(arc);
    if (!strcmp(xt_basename(*p), "chain3.ogg")) {
      vb_pack(*p, arc2, VB_ENDS[1]);
      CHECK(xt_same_file(arc, arc2),
            "chained archive depends on earlier encodes");
    }
    in += xt_file_size(*p);
    xt_trace("%-24s %8ld -> %8ld (%d) %8ld (%d)", xt_basename(*p),
             xt_file_size(*p), sizes[0], VB_ENDS[0], sizes[1], VB_ENDS[1]);
  }
  xt_section_begin("effort scale");
  CHECK(!*files || tot[1] < tot[0],
        "configuration %d (%ld bytes) is not smaller than %d (%ld bytes)",
        VB_ENDS[1], tot[1], VB_ENDS[0], tot[0]);
  xt_trace("vorbis: %ld -> %ld (%d), %ld (%d)", in, tot[0], VB_ENDS[0], tot[1],
           VB_ENDS[1]);
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
  Fi(n,
    char tag[32];
    sprintf(tag, "o%d.blr", i);
    arcs[i] = xmalloc(strlen(xt_tmp(tag)) + 1);
    strcpy(arcs[i], xt_tmp(tag));
    CHECK(!opus_pack(files[i], arcs[i], 245), "%s: encode at depth 0 failed",
          xt_basename(files[i]));
    CHECK(!opus_unpack(arcs[i], out), "%s: decode at depth 0 failed",
          xt_basename(files[i]));
    CHECK(xt_same_file(files[i], out), "%s: not lossless at depth 0",
          xt_basename(files[i]));
    tot[0] += xt_file_size(arcs[i]);
    CHECK(!opus_pack(files[i], arcs[i], 335) && !opus_unpack(arcs[i], out) &&
          xt_same_file(files[i], out), "%s: not lossless with the costing pass",
          xt_basename(files[i]));
    CHECK(!opus_pack(files[i], arcs[i], 263), "%s: encode at depth 6 failed",
          xt_basename(files[i]));
    tot[1] += xt_file_size(arcs[i]);
    in += xt_file_size(files[i]));
  Fi(n,
    CHECK(!opus_unpack(arcs[i], out), "%s: decode failed",
          xt_basename(files[i]));
    CHECK(xt_same_file(files[i], out), "%s: not lossless in a shared process",
          xt_basename(files[i])));
  Fi(n,
    CHECK(!opus_pack(files[i], re, 263), "%s: re-encode failed",
          xt_basename(files[i]));
    CHECK(xt_same_file(arcs[i], re), "%s: the archive depends on what was "
          "encoded before it", xt_basename(files[i]));
    xt_trace("%-24s %8ld -> %8ld", xt_basename(files[i]),
             xt_file_size(files[i]), xt_file_size(arcs[i]));
    xt_unlink(arcs[i]);  xfree(arcs[i]));
  xt_section_begin("effort scale");
  CHECK(!n || tot[1] <= tot[0], "depth 6 (%ld bytes) is larger than depth 0 "
        "(%ld bytes)", tot[1], tot[0]);
  xt_trace("opus: %ld -> %ld (depth 0), %ld (depth 6)", in, tot[0], tot[1]);
  xt_unlink(out);  xt_unlink(re);
  xfree(arcs);  xt_files_free(files);
}

static void t_opus_chain(void) {
  static const char * const names[] = {
    "celt_st_128k.opus", "silk_mono_16k.opus", "hyb_mono_40k.opus"
  };
  const char * in = xt_tmp("chain.opus"), * arc = xt_tmp("chain.blr");
  const char * arc2 = xt_tmp("chain2.blr"), * out = xt_tmp("chain.out");
  u8 * joined = NULL;
  sz total = 0;
  long parts = 0;
  int i;
  xt_section_begin("chained Opus");
  Fi(3,
    sz n;
    u8 * b = slurp(xt_fixture(xt_data, names[i]), &n);
    joined = xrealloc(joined, total + n);
    memcpy(joined + total, b, n);  total += n;  xfree(b);
    CHECK(!opus_pack(xt_fixture(xt_data, names[i]), arc, 263),
          "%s: encode failed", names[i]);
    parts += xt_file_size(arc));
  spew(in, joined, total);  xfree(joined);
  CHECK(!opus_pack(in, arc, 263), "chained Opus encode failed");
  CHECK(!opus_unpack(arc, out), "chained Opus decode failed");
  CHECK(xt_same_file(in, out), "chained Opus is not lossless");
  CHECK(!opus_pack(in, arc2, 263) && xt_same_file(arc, arc2),
        "the chained archive depends on earlier encodes");
  xt_trace("opus chain: %ld -> %ld, separately %ld", (long) total,
           xt_file_size(arc), parts);
  xt_unlink(in);  xt_unlink(arc);  xt_unlink(arc2);  xt_unlink(out);
}

static void clear_eos(u8 * b, sz n, int every) {
  sz at, got;
  ogg_page p;
  for (at = 0; at < n; at += got) {
    got = ogg_parse(&p, b + at, n - at);
    FATAL_UNLESS(got, "invalid no-EOS fixture");
    if (every || at + got == n) {
      b[at + 5] &= (u8) ~4;  ogg_crc_set(b + at, got);
    }
  }
}

static void t_no_eos(void) {
  static const char * const names[] = {
    "tiny.ogg", "chain3.ogg", "chain_cont.ogg"
  };
  const char * in = xt_tmp("noeos.ogg"), * arc = xt_tmp("noeos.blr");
  const char * out = xt_tmp("noeos.out");
  int i, j, copies, k;
  xt_section_begin("Vorbis without EOS");
  Fi(3,
    sz n;
    u8 * b = slurp(xt_fixture(xt_data, names[i]), &n);
    u8 * joined = xmalloc(3 * n);
    for (copies = 0; copies <= 2; copies += 2) Fj(2,
      sz total = (sz) (copies + 1) * n;
      archive a;
      u8 * image;
      sz len;
      Fk(copies + 1, memcpy(joined + (sz) k * n, b, n));
      clear_eos(joined, total, j);
      spew(in, joined, total);
      vb_pack(in, arc, VB_ENDS[0]);  vb_unpack(arc, out);
      CHECK(xt_same_file(in, out), "%s without %s EOS, %d copies", names[i],
            j ? "any" : "a final", copies);
      image = slurp(arc, &len);
      arc_parse(&a, image, len);
      arc_write(&a, out);
      CHECK(xt_same_file(arc, out), "page-count archive write is exact");
      arc_free(&a);  xfree(image);
      t_reemit(arc));
    xfree(joined);  xfree(b));
  xt_unlink(in);  xt_unlink(arc);  xt_unlink(out);
}

static void t_acm(void) {
  static const char * const VB[] = { "vibrato.ogg", "chain3.ogg" };
  static const char * const OP[] = { "celt_st_96k_60ms.opus" };
  const char * arc = xt_tmp("max.blr"), * out = xt_tmp("max.out");
  int i;
  xt_section_begin("ACM round trip");
  Fi(2,
    const char * in = xt_fixture(xt_data, VB[i]);
    vb_pack(in, arc, 157);  vb_unpack(arc, out);
    CHECK(xt_same_file(in, out), "%s: not lossless with ACM", VB[i]);
    xt_trace("%-24s %8ld -> %8ld (ACM)", VB[i], xt_file_size(in),
             xt_file_size(arc)));
  Fi(1,
    const char * in = xt_fixture(xt_data, OP[i]);
    CHECK(!opus_pack(in, arc, 342) && !opus_unpack(arc, out) &&
          xt_same_file(in, out), "%s: not lossless with ACM", OP[i]);
    xt_trace("%-24s %8ld -> %8ld (ACM)", OP[i], xt_file_size(in),
             xt_file_size(arc)));
  xt_unlink(arc);  xt_unlink(out);
}

static void t_configs(void) {
  static const char * const VB[] = {
    "vibrato.ogg", "chain3.ogg", "flooralt.ogg", "floorhi.ogg"
  };
  static const char * const OP[] = {
    "celt_st_128k.opus", "hyb_mono_40k.opus", "surround51.opus"
  };
  const char * arc = xt_tmp("cfg.blr"), * out = xt_tmp("cfg.out");
  int i, j, k, n;
  xt_section_begin("configurations");
  Fi(4,
    const char * in = xt_fixture(xt_data, VB[i]);
    Fk(vb_nconfig(),
      n = vb_config(k);
      if (vb_config_acm(n)) continue;
      vb_pack(in, arc, n);  vb_unpack(arc, out);
      CHECK(xt_same_file(in, out), "%s: not lossless in configuration %d",
            VB[i], n)));
  Fi(3,
    const char * in = xt_fixture(xt_data, OP[i]);
    Fj(opus_nconfig(),
      n = opus_config_id(j);
      if (opus_config_acm(n)) continue;
      CHECK(!opus_pack(in, arc, n) && !opus_unpack(arc, out) &&
            xt_same_file(in, out),
            "%s: not lossless in configuration %d", OP[i], n)));
  xt_unlink(arc);  xt_unlink(out);
}

static void t_classes(void) {
  static const struct { const char * name;  int k; } OC[] = {
    { "silk_speech_12k.opus", OC_SILK }, { "silk_mono_16k.opus", OC_SILK },
    { "hyb_mono_40k.opus", OC_MONO }, { "celt_st_128k.opus", OC_LOW },
    { "celt_st_96k_60ms.opus", OC_LOW }, { "silence_st_64k.opus", OC_LOW }
  };
  static const struct { const char * name;  int k; } VC[] = {
    { "sweep.ogg", VC_MONO_HI }, { "eq_mono8k.ogg", VC_MONO },
    { "tone_stereo.ogg", VC_MULTI_HI }, { "chain3.ogg", VC_MULTI },
    { "vibrato.ogg", VC_MULTI_X }, { "surround71.ogg", VC_SURR }
  };
  int i;
  xt_section_begin("level classes");
  Fi(6,
    const char * in = xt_fixture(xt_data, OC[i].name);
    int k = opus_classify(in);
    CHECK(k == OC[i].k && opus_classify(in) == k,
          "%s: Opus class %d, expected %d", OC[i].name, k, OC[i].k));
  Fi(6,
    const char * in = xt_fixture(xt_data, VC[i].name);
    int k = vb_classify(in);
    CHECK(k == VC[i].k && vb_classify(in) == k,
          "%s: Vorbis class %d, expected %d", VC[i].name, k, VC[i].k));
  CHECK(opus_classify(xt_fixture(xt_data, "sweep.ogg")) == -1,
        "a Vorbis file has no Opus class");
}

static void mp3_rt(const char * in, const char * name, const int * cfg,
                   int ncfg, long * sizes) {
  const char * arc = xt_tmp("m.blr"), * out = xt_tmp("m.out");
  int i;
  Fi(ncfg,
    CHECK(!mp3_pack(in, arc, cfg[i]) && !mp3_unpack(arc, out) &&
          xt_same_file(in, out),
          "%s: not lossless in MP3 configuration %d", name, cfg[i]);
    if (sizes) sizes[i] += xt_file_size(arc));
  xt_unlink(arc);  xt_unlink(out);
}

static void t_mp3(void) {
  char ** files = xt_files(".mp3"), ** p;
  int cfg[100], n = 0, l1 = 0, l20 = 0, i;
  long sizes[100], in = 0, lo = 0, hi = 0;
  xt_section_begin("mp3 round trip");
  if (!*files) CHECK(0, "no MP3 fixtures: is BLR_TEST_DATA set?");
  Fi(100,
    if (mp3_config(MP_CFG0 + i)) { cfg[n] = MP_CFG0 + i;  sizes[n++] = 0; });
  Fi(n,
    if (cfg[i] == mp3_level_cfg(1)) l1 = i;
    if (cfg[i] == mp3_level_cfg(20)) l20 = i);
  for (p = files; *p; p++) {
    sz len;
    u8 * b = slurp(*p, &len);
    long before[100];
    CHECK(mp3_sniff(b, len, 1), "%s: not taken for MPEG audio",
          xt_basename(*p));
    if (!strncmp(xt_basename(*p), "short_", 6))
      CHECK(!mp3_sniff(b, len, 0), "%s: fewer than 6 frames taken without "
            "the end of the file", xt_basename(*p));
    Fi(n, before[i] = sizes[i]);
    mp3_rt(*p, xt_basename(*p), cfg, n, sizes);
    lo += sizes[l1] - before[l1];
    hi += sizes[l20] - before[l20];
    in += (long) len;
    xt_trace("%-24s %8ld -> %8ld (-1) %8ld (-20)", xt_basename(*p), (long) len,
             sizes[l1] - before[l1], sizes[l20] - before[l20]);
    xfree(b);
  }
  xt_section_begin("mp3 effort scale");
  CHECK(!*files || (hi < lo && lo < in), "MP3: -20 (%ld bytes) is not below "
        "-1 (%ld bytes) below the input (%ld)", hi, lo, in);
  xt_files_free(files);
}

static void t_mp3_damage(void) {
  static const char * const A = "cbr128_tags.mp3";
  static const char * const B = "lsf22_mono.mp3";
  const char * in = xt_tmp("dmg.mp3");
  int cfg[3], i, j;
  sz na, nb;
  u8 * a, * b, * t;
  xt_rng r;
  xt_section_begin("mp3 damaged input");
  a = slurp(xt_fixture(xt_data, A), &na);
  b = slurp(xt_fixture(xt_data, B), &nb);
  t = xmalloc(na + nb + 4096);
  cfg[0] = MP_CFG0;  cfg[1] = mp3_level_cfg(10);  cfg[2] = mp3_level_cfg(20);
  xt_seed(&r, 7);
  Fi(6,
    sz n = 0;
    switch (i) {
    case 0: n = na * 2 / 3;  memcpy(t, a, n);  break;
    case 1:
      Fj(1000, t[j] = (u8) xt_next(&r, 256));
      memcpy(t + 1000, a, na);  n = na + 1000;  break;
    case 2: memcpy(t, a, na);  memcpy(t + na, b, nb);  n = na + nb;  break;
    case 3:
      memcpy(t, b, nb);  n = nb;
      for (j = 100; j < (int) n; j += 211) t[j] ^= (u8) (1 + xt_next(&r, 255));
      break;
    case 4: n = 4096;  Fj((int) n, t[j] = (u8) xt_next(&r, 256));  break;
    case 5: n = 0;  break;
    }
    spew(in, t, n);
    mp3_rt(in, i == 0 ? "a cut stream" : i == 1 ? "junk before frames"
               : i == 2 ? "joined streams" : i == 3 ? "flipped bytes"
               : i == 4 ? "random bytes" : "an empty file", cfg, 3, NULL));
  xt_unlink(in);
  xfree(t);  xfree(a);  xfree(b);
}

static void t_mp3_short_frames(void) {
  static const u8 H[4] = { 0xFF, 0xF3, 0x14, 0x04 };
  const char * in = xt_tmp("short.mp3");
  int cfg[5], i;
  u8 * t = xcalloc(1200, 24);
  xt_section_begin("mp3 short frames");
  cfg[0] = MP_CFG0 + 40;  cfg[1] = MP_CFG0 + 41;  cfg[2] = MP_CFG0 + 42;
  cfg[3] = MP_CFG0 + 43;  cfg[4] = MP_CFG0 + 44;
  Fi(1200, memcpy(t + 24 * i, H, 4));
  spew(in, t, 1200 * 24);
  mp3_rt(in, "frames of 24 bytes", cfg, 5, NULL);
  xt_unlink(in);
  xfree(t);
}

static void t_mp3_windows(void) {
  static const char * const F[] = {
    "cbr128_tags.mp3", "vbr_v4.mp3", "free_32k.mp3", "crc_lr48k.mp3"
  };
  const char * in = xt_tmp("win.mp3");
  int cfg[4], i, j;
  sz w = mp3_window, n = 0, cap = 1 << 16;
  u8 * t = xmalloc(cap);
  xt_rng r;
  xt_section_begin("mp3 windows");
  cfg[0] = MP_CFG0;  cfg[1] = MP_CFG0 + 5;
  cfg[2] = MP_CFG0 + 45;  cfg[3] = MP_CFG0 + 46;
  xt_seed(&r, 11);
  Fj(3, Fi(4,
    sz k;
    u8 * b = slurp(xt_fixture(xt_data, F[i]), &k);
    t = xgrow(t, &cap, n + k + 300, 1, cap);
    memcpy(t + n, b, k);  n += k;
    if (j) { int q;  for (q = 0; q < 300; q++) t[n++] = (u8) xt_next(&r, 256); }
    xfree(b)));
  spew(in, t, n);
  Fi(3,
    mp3_window = (sz) 4096 << (2 * i);
    mp3_rt(in, "a file of many windows", cfg, 4, NULL));
  mp3_window = w;
  xt_unlink(in);
  xfree(t);
}

void xt_run_files(void) {
  t_vorbis();
  t_opus();
  t_opus_chain();
  t_no_eos();
  t_acm();
  t_configs();
  t_classes();
  t_mp3();
  t_mp3_damage();
  t_mp3_short_frames();
  t_mp3_windows();
}
