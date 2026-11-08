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
#include "fmt/cmt.h"
#include "coder/rc.h"
#include "fmt/ogg.h"
#include "vorbis/vorbis.h"
#include "opus/opus.h"

typedef struct { u8 * b;  sz n, cap; } bytes;

static void append(bytes * b, const void * p, sz n) {
  b->b = xgrow(b->b, &b->cap, b->n + n, 1, 16);
  if (n) memcpy(b->b + b->n, p, n);
  b->n += n;
}

static void word(bytes * b, u32 v, int be) {
  u8 buf[4];
  int i;
  Fi(4, buf[i] = (u8) (v >> ((be ? 3 - i : i) * 8)));
  append(b, buf, 4);
}

static void string(bytes * b, const void * s, sz n) {
  word(b, (u32) n, 0);  append(b, s, n);
}

static void tags(bytes * b, int opus, const bytes * fields, u32 count) {
  static const char vendor[] = "Lavf test; Xiph.Org libVorbis I; libopus";
  append(b, opus ? "OpusTags" : "\003vorbis", opus ? 8 : 7);
  string(b, vendor, sizeof vendor - 1);  word(b, count, 0);
  append(b, fields->b, fields->n);
}

static void encode64(bytes * out, const u8 * b, sz n) {
  static const char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  sz i;
  for (i = 0; i < n; i += 3) {
    u32 v = (u32) b[i] << 16;
    u8 s[4];
    if (n - i > 1) v |= (u32) b[i + 1] << 8;
    if (n - i > 2) v |= b[i + 2];
    s[0] = (u8) alphabet[v >> 18];  s[1] = (u8) alphabet[v >> 12 & 63];
    s[2] = n - i > 1 ? (u8) alphabet[v >> 6 & 63] : '=';
    s[3] = n - i > 2 ? (u8) alphabet[v & 63] : '=';
    append(out, s, 4);
  }
}

static void art(bytes * out, sz n, int mime) {
  bytes raw = { 0 };
  xt_rng r;
  sz i;
  const char * type = mime ? "image/png" : "image/jpeg";
  word(&raw, 3, 1);  word(&raw, (u32) strlen(type), 1);
  append(&raw, type, strlen(type));
  word(&raw, 11, 1);  append(&raw, "Front cover", 11);
  word(&raw, 256, 1);  word(&raw, 256, 1);
  word(&raw, 24, 1);  word(&raw, 0, 1);
  word(&raw, (u32) n, 1);  xt_seed(&r, 0x37A61);
  Fi(n, u8 v = (u8) xt_next(&r, 256);  append(&raw, &v, 1));
  encode64(out, raw.b, raw.n);  xfree(raw.b);
}

static void memread(void * ctx, sz at, u8 * b, sz n) {
  bytes * s = ctx;
  FATAL_UNLESS(at <= s->n && n <= s->n - at,
               "test: comment read out of bounds");
  memcpy(b, s->b + at, n);
}

static void memwrite(void * ctx, const u8 * b, sz n) { append(ctx, b, n); }
static int encbit(void * ctx, u32 prob, int b) {
  rc_enc_bit_raw(ctx, prob, b);  return b;
}
static int decbit(void * ctx, u32 prob, int b) {
  (void) b;  return rc_dec_bit_raw(ctx, prob);
}

static sz roundtrip(bytes * b, int opus) {
  rc_enc e;
  rc_dec d;
  cmt_io c;
  bytes out = { 0 };
  sz n;
  rc_enc_init(&e);
  c.enc = 1;  c.input = b;  c.read = memread;
  c.coder = &e;  c.bit = encbit;  c.output = NULL;  c.write = NULL;
  cmt_code(&c, b->n, opus);  n = rc_enc_finish(&e);
  rc_dec_init(&d, rc_enc_data(&e), n);
  c.enc = 0;  c.input = NULL;  c.read = NULL;
  c.coder = &d;  c.bit = decbit;  c.output = &out;  c.write = memwrite;
  cmt_code(&c, b->n, opus);
  CHECK(out.n == b->n && (!b->n || !memcmp(out.b, b->b, b->n)),
        "codec %d preserves %lu comment bytes", opus, (unsigned long) b->n);
  xfree(out.b);  rc_dec_free(&d);  rc_enc_free(&e);
  return n;
}

static void t_text(void) {
  bytes fields = { 0 }, b = { 0 }, large = { 0 };
  static const char text[] =
    "ARTIST=Some artist / ALBUM=Some artist / TITLE=Some title\n";
  static const char odd[] = "aRtIsT=Gr\303\274\303\237e\0\377";
  int k, i;
  xt_section_begin("structured comments and text history");
  Fi(4000, append(&large, text, sizeof text - 1));
  string(&fields, text, sizeof text - 1);
  string(&fields, odd, sizeof odd - 1);  string(&fields, "", 0);
  string(&fields, "no equals sign", 14);  string(&fields, large.b, large.n);
  Fk(3,
    static const int flags[3] = { 0, CMT_OPUS, CMT_OPUS | CMT_ODICT };
    int opus = flags[k];
    sz size;
    b.n = 0;  tags(&b, opus & CMT_OPUS, &fields, 5);
    append(&b, "\001\0opaque trailer\377", 17);
    size = roundtrip(&b, opus);
    CHECK(size < b.n / 10, "codec %d compresses repeated text", opus);
    memset(b.b + (opus & CMT_OPUS ? 8 : 7), 255, 4);  roundtrip(&b, opus);
    b.n = 0;  tags(&b, opus & CMT_OPUS, &fields, 0xFFFFFFFFUL);
    roundtrip(&b, opus);
    b.n = 0;  tags(&b, opus & CMT_OPUS, &fields, 5);  b.n--;
    roundtrip(&b, opus);
    Fi(25, b.n = (sz) i;  roundtrip(&b, opus)));
  xfree(fields.b);  xfree(b.b);  xfree(large.b);
}

static void t_bytes(void) {
  static const sz sizes[] = {
    0, 1, 3, 4, 255, 16383, 16384, 16385, 65535, 65536, 65537
  };
  bytes b = { 0 }, fields = { 0 }, value = { 0 };
  xt_rng r;
  sz j;
  int i, k;
  xt_section_begin("binary comments and block boundaries");
  xt_seed(&r, 0x548911);
  Fi((int) ARRAY_LEN(sizes),
    b.n = 0;
    Fj(sizes[i], u8 v = (u8) xt_next(&r, 256);  append(&b, &v, 1));
    roundtrip(&b, i & 1);
    if (b.n) {
      sz n = b.n;
      u8 * copy = xmalloc(n);
      memcpy(copy, b.b, n);  append(&b, copy, n);  xfree(copy);
      roundtrip(&b, i & 1);
    });
  Fi(40 * xt_level,
    u32 count = xt_next(&r, 9);
    fields.n = b.n = 0;
    Fk((int) count,
      sz n = xt_next(&r, 1024);
      value.n = 0;
      if (k & 1) append(&value, "TITLE=", 6);
      Fj(n, u8 v = (u8) xt_next(&r, 256);  append(&value, &v, 1));
      string(&fields, value.b, value.n));
    tags(&b, i & 1, &fields, count);  roundtrip(&b, i & 1));
  xfree(b.b);  xfree(fields.b);  xfree(value.b);
}

static void t_pictures(void) {
  bytes fields = { 0 }, b = { 0 }, value = { 0 }, pic = { 0 };
  int i, j, k;
  xt_section_begin("picture transforms and base64 fallback");
  Fi(2,
    Fj(3,
      pic.n = 0;  art(&pic, 65536 + (sz) j, 1);
      Fk(7,
        sz size;
        fields.n = b.n = value.n = 0;
        append(&value, "metadata_block_picture=", 23);
        append(&value, pic.b, pic.n);
        if (k == 1) value.b[23] = '?';
        if (k == 2) value.n--;
        if (k == 3) append(&value, "\n", 1);
        if (k == 4) value.b[24] = '=';
        if (k == 5) value.b[31] = '/';
        if (k == 6) {
          if (value.b[value.n - 2] == '=') value.b[value.n - 3] = 'B';
          else if (value.b[value.n - 1] == '=') value.b[value.n - 2] = 'B';
        }
        string(&fields, value.b, value.n);  tags(&b, i, &fields, 1);
        append(&b, "\001", 1);  size = roundtrip(&b, i);
        if (!k)
          CHECK(size < pic.n * 4 / 5,
                "codec %d removes base64 overhead (%d)", i, j))));
  pic.n = value.n = fields.n = b.n = 0;  art(&pic, 0, 0);
  append(&value, "METADATA_BLOCK_PICTURE=", 23);  append(&value, pic.b, pic.n);
  string(&fields, value.b, value.n);  tags(&b, 1, &fields, 1);
  roundtrip(&b, 1);
  xfree(fields.b);  xfree(b.b);  xfree(value.b);  xfree(pic.b);
}

static void mangled(bytes * b, const char * s) {
  char t[128];
  sz n = strlen(s);
  memcpy(t, s, n);
  if (n) t[0] = 'Q';
  string(b, t, n);
}

static void t_sdict(void) {
  static const char * const vendors[] = {
    "libopus 1.5.2, libopusenc 0.2.1", "libopus 1.1.2", "Lavf61.7.100",
    "Xiphophorus libVorbis I 20010813",
    "Xiph.Org libVorbis I 20150105 "
    "(\342\233\204\342\233\204\342\233\204\342\233\204)",
    "Xiph.Org libVorbis I 20200704 (Reducing Environment) ",
    "libopus 1.5.2, libopusenc 0.2", "libopus 1.5.3", ""
  };
  static const char * const known[] = {
    "ENCODER=opusenc from opus-tools 0.2",
    "ENCODER=opusenc from opus-tools 0.1.10", "encoder=Lavc61.19.100 libopus",
    "encoder=Lavf57.83.100", "ENCODER_OPTIONS=--bitrate 64 --serial 1",
    "TITLE=", "title=x", "DURATION=00:01:02.5"
  };
  static const char * const other[] = {
    "ENCODER=opusenc from opus-tools 0.2.1", "encoder=Lavc61.19.100 libopus ",
    "TITLE", "Title=x", "=", ""
  };
  bytes b = { 0 }, f = { 0 }, value = { 0 }, pic = { 0 };
  int i, j, k;
  xt_section_begin("encoder string table");
  Fk(2,
    int base = k ? CMT_OPUS | CMT_ODICT : 0;
    Fi((int) ARRAY_LEN(vendors),
      sz miss = 0, hit = 0;
      int m;
      for (m = 0; m < 2; m++) {
        f.n = b.n = 0;
        Fj((int) ARRAY_LEN(known),
          if (m) mangled(&f, known[j]);
          else string(&f, known[j], strlen(known[j])));
        Fj((int) ARRAY_LEN(other),
          string(&f, other[j], strlen(other[j])));
        append(&b, k ? "OpusTags" : "\003vorbis", k ? 8 : 7);
        string(&b, vendors[i], strlen(vendors[i]));
        word(&b, (u32) (ARRAY_LEN(known) + ARRAY_LEN(other)), 0);
        append(&b, f.b, f.n);
        if (!k) append(&b, "\001", 1);
        if (m) miss = roundtrip(&b, base);  else hit = roundtrip(&b, base);
      }
      CHECK(hit + 24 < miss,
            "codec %d codes known fields by index (%lu < %lu bytes)", k,
            (unsigned long) hit, (unsigned long) miss);
      if (i < 5) {
        for (m = 0; m < 2; m++) {
          b.n = 0;  append(&b, k ? "OpusTags" : "\003vorbis", k ? 8 : 7);
          if (m) mangled(&b, vendors[i]);
          else string(&b, vendors[i], strlen(vendors[i]));
          word(&b, 0, 0);
          if (m) miss = roundtrip(&b, base);  else hit = roundtrip(&b, base);
        }
        CHECK(hit + 4 < miss,
              "codec %d codes vendor %d by index (%lu < %lu bytes)", k, i,
              (unsigned long) hit, (unsigned long) miss);
      }));
  art(&pic, 3000, 0);
  append(&value, "METADATA_BLOCK_PICTURE=", 23);  append(&value, pic.b, pic.n);
  f.n = 0;  string(&f, value.b, value.n);  string(&f, "TITLE=abc", 9);
  b.n = 0;  tags(&b, 1, &f, 2);
  CHECK(roundtrip(&b, CMT_OPUS) < pic.n * 4 / 5,
        "a picture after the key from the table keeps its transform");
  Fi(40, b.n = (sz) (i * 2);  roundtrip(&b, CMT_OPUS));
  xfree(b.b);  xfree(f.b);  xfree(value.b);  xfree(pic.b);
}

static void packet(bytes * out, const u8 * b, sz n, u32 serial, u32 * seq,
                   u32 glo, u32 ghi, int eos, int laces) {
  sz off = 0, take;
  u8 * image = xmalloc(65307);
  do {
    ogg_page p;
    sz start = off, size;
    memset(&p, 0, sizeof p);
    do {
      take = MIN(255, n - off);  p.lace[p.nseg++] = (u8) take;  off += take;
    } while (take == 255 && p.nseg < laces);
    p.type = (u8) ((!*seq ? 2 : 0) | (start ? 1 : 0) |
                   (take < 255 && eos ? 4 : 0));
    p.serial = serial;  p.seq = (*seq)++;
    p.glo = take < 255 ? glo : 0xFFFFFFFFUL;
    p.ghi = take < 255 ? ghi : 0xFFFFFFFFUL;
    p.blen = off - start;
    size = xt_ogg_emit(&p, image, b + start);  append(out, image, size);
  } while (take == 255);
  xfree(image);
}

static void retag(const char * in, const char * out, const bytes * tags,
                  int laces) {
  sz n, at = 0;
  u8 * b = slurp(in, &n);
  bytes result = { 0 }, acc = { 0 };
  u32 seq = 0;
  int np = 0;
  while (at < n) {
    ogg_page p;
    sz got = ogg_parse(&p, b + at, n - at), off;
    int i;
    FATAL_UNLESS(got, "test: invalid fixture page");
    off = at + OGG_HDRMIN + (sz) p.nseg;
    Fi(p.nseg,
      append(&acc, b + off, p.lace[i]);  off += p.lace[i];
      if (p.lace[i] < 255) {
        const bytes * data = np == 1 ? tags : &acc;
        packet(&result, data->b, data->n, p.serial, &seq, p.glo, p.ghi,
               (p.type & 4) && i == p.nseg - 1, np == 1 ? laces : 255);
        np++;  acc.n = 0;
      });
    at += got;
  }
  FATAL_UNLESS(np >= 3 && !acc.n, "test: incomplete fixture packets");
  spew(out, result.b, result.n);  xfree(b);  xfree(result.b);  xfree(acc.b);
}

static void t_packets(void) {
  const char * in = xt_tmp("edges.ogg"), * arc = xt_tmp("edges.blr");
  const char * out = xt_tmp("edges.out"), * again = xt_tmp("cli2.blr");
  const sz lengths[] = { 61439, 61440, 61441, 65025, 65536, 131070, 1048577 };
  bytes b = { 0 }, fields = { 0 }, value = { 0 }, pic = { 0 };
  int i, j;
  xt_section_begin("large comment packets and page boundaries");
  art(&pic, 800000, 1);
  append(&value, "METADATA_BLOCK_PICTURE=", 23);  append(&value, pic.b, pic.n);
  string(&fields, value.b, value.n);
  Fj(2,
    const char * fixture = xt_fixture(xt_data, j ? "silk_speech_12k.opus"
                                                 : "tiny.ogg");
    for (i = 0; i <= (int) ARRAY_LEN(lengths); i++) {
      bytes empty = { 0 };
      b.n = 0;
      if (i == (int) ARRAY_LEN(lengths))
        tags(&b, j, &fields, 1);
      else {
        sz trailer;
        tags(&b, j, &empty, 0);  trailer = b.n;
        while (b.n < lengths[i]) append(&b, "\0", 1);
        if (!j) b.b[trailer] = 1;
      }
      retag(fixture, in, &b, i & 1 ? 17 : 255);
      if (j) {
        CHECK(!opus_pack(in, arc, 245) && !opus_unpack(arc, out),
              "Opus metadata %lu encodes/decodes", (unsigned long) b.n);
        CHECK(!opus_pack(in, again, 245), "Opus metadata re-encodes");
      } else {
        vb_pack(in, arc, 126);  vb_unpack(arc, out);  vb_pack(in, again, 126);
      }
      CHECK(xt_same_file(in, out),
            "codec %d restores %lu metadata bytes and Ogg framing", j,
            (unsigned long) b.n);
      CHECK(xt_same_file(arc, again),
            "codec %d metadata encoding is deterministic", j);
    });
  xfree(b.b);  xfree(fields.b);  xfree(value.b);  xfree(pic.b);
  xt_unlink(in);  xt_unlink(arc);  xt_unlink(out);  xt_unlink(again);
}

void xt_run_comments(void) {
  t_text();  t_bytes();  t_pictures();  t_sdict();  t_packets();
}
