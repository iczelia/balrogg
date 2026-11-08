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

#include "fmt/cmt.h"
#include "coder/rc.h"

#define C_BLOCK 16384
#define C_WINDOW 65536
#define C_HASH 65536
#define C_MATCH 259
#define C_CONTEXTS (C_IDX + 4)

enum { C_VENDOR, C_KEY, C_VALUE, C_TAIL, C_OPAQUE, C_PICTURE, C_ROLES };
#define C_FLAG  (C_ROLES * 16)
#define C_MLEN  (C_FLAG + C_ROLES)
#define C_DIST  (C_MLEN + C_ROLES)
#define C_SIZE  (C_DIST + 2)
#define C_COUNT (C_SIZE + 4)
#define C_SPLIT (C_COUNT + 4)
#define C_FORM  (C_SPLIT + 1)
#define C_PAD   (C_FORM + 1)
#define C_IDX   (C_PAD + 1)

static const char dict[] =
  "TITLE=\0ARTIST=\0ALBUM=\0ALBUMARTIST=\0DATE=\0GENRE=\0TRACKNUMBER=\0"
  "TRACKTOTAL=\0DISCNUMBER=\0DISCTOTAL=\0COMMENT=\0DESCRIPTION=\0"
  "COMPOSER=\0PERFORMER=\0COPYRIGHT=\0LICENSE=\0ENCODER=\0"
  "METADATA_BLOCK_PICTURE=\0REPLAYGAIN_TRACK_GAIN=\0REPLAYGAIN_TRACK_PEAK=\0"
  "REPLAYGAIN_ALBUM_GAIN=\0REPLAYGAIN_ALBUM_PEAK=\0R128_TRACK_GAIN=\0"
  "R128_ALBUM_GAIN=\0MUSICBRAINZ_TRACKID=\0MUSICBRAINZ_ALBUMID=\0"
  "MUSICBRAINZ_ARTISTID=\0MUSICBRAINZ_ALBUMARTISTID=\0"
  "Xiph.Org libVorbis I \0libopus\0Lavf\0Lavc\0https://\0http://\0"
  "image/jpeg\0image/png\0Front cover\0";
static const char dict_opus[] =
  "encoder=Lavc6\0encoder=Lavf6\0 libopus\0language=\0DURATION=00:0\0"
  "ENCODER=opusenc from opus-tools 0.2\0ENCODER_OPTIONS=--bitrate \0"
  "handler_name=SoundHandler\0major_brand=\0vendor_id=[0][0][0][0]\0"
  "comment=\0title=\0artist=\0album=\0date=\0genre=\0track=\0";
static const char sd_vorbis[] =
  "Xiphophorus libVorbis I 20000508\0Xiphophorus libVorbis I 20001031\0"
  "Xiphophorus libVorbis I 20010225\0Xiphophorus libVorbis I 20010615\0"
  "Xiphophorus libVorbis I 20010813\0Xiphophorus libVorbis I 20011231\0"
  "Xiph.Org libVorbis I 20020717\0Xiph.Org libVorbis I 20030909\0"
  "Xiph.Org libVorbis I 20040629\0Xiph.Org libVorbis I 20050304\0"
  "Xiph.Org libVorbis I 20070622\0Xiph.Org libVorbis I 20080501\0"
  "Xiph.Org libVorbis I 20090624\0Xiph.Org libVorbis I 20090709\0"
  "Xiph.Org libVorbis I 20100325 (Everywhere)\0"
  "Xiph.Org libVorbis I 20101101 (Schaufenugget)\0"
  "Xiph.Org libVorbis I 20120203 (Omnipresent)\0"
  "Xiph.Org libVorbis I 20140122 (Turpak\303\244r\303\244jiin)\0"
  "Xiph.Org libVorbis I 20150105 (\342\233\204\342\233\204\342\233\204\342\233\204)\0"
  "Xiph.Org libVorbis I 20180316 (Now 100% fewer shells)\0"
  "Xiph.Org libVorbis I 20200704 (Reducing Environment)\0";
static const char sd_libopus[] =
  "1.0.1\0" "1.0.2\0" "1.0.3\0" "1.1\0" "1.1.1\0" "1.1.2\0" "1.1.3\0" "1.1.4\0"
  "1.1.5\0" "1.2\0" "1.2.1\0" "1.3\0" "1.3.1\0" "1.4\0" "1.5\0" "1.5.1\0"
  "1.5.2\0";
static const char sd_opusenc[] = "0.1\0" "0.1.1\0" "0.2\0" "0.2.1\0";
static const char sd_tools[] =
  "0.1.2\0" "0.1.3\0" "0.1.4\0" "0.1.5\0" "0.1.6\0" "0.1.7\0" "0.1.8\0"
  "0.1.9\0" "0.1.10\0" "0.2\0";
static const char sd_ffmpeg[] =
  "57.83.100\0" "57.107.100\0" "58.12.100\0" "58.18.100\0" "58.20.100\0"
  "58.35.100\0" "58.29.100\0" "58.54.100\0" "58.45.100\0" "58.91.100\0"
  "58.76.100\0" "58.134.100\0" "59.16.100\0" "59.18.100\0" "59.27.100\0"
  "59.37.100\0" "60.3.100\0" "60.3.100\0" "60.16.100\0" "60.31.102\0"
  "61.1.100\0" "61.3.100\0" "61.7.100\0" "61.19.100\0" "62.3.100\0"
  "62.11.100\0";
static const char sd_keys[] =
  "TITLE=\0ARTIST=\0ALBUM=\0ALBUMARTIST=\0DATE=\0GENRE=\0TRACKNUMBER=\0"
  "TRACKTOTAL=\0DISCNUMBER=\0DISCTOTAL=\0COMMENT=\0DESCRIPTION=\0COMPOSER=\0"
  "PERFORMER=\0COPYRIGHT=\0LICENSE=\0ORGANIZATION=\0LOCATION=\0CONTACT=\0"
  "ISRC=\0VERSION=\0LYRICS=\0ENCODER=\0ENCODED_BY=\0ENCODER_OPTIONS=\0"
  "LANGUAGE=\0REPLAYGAIN_TRACK_GAIN=\0REPLAYGAIN_TRACK_PEAK=\0"
  "REPLAYGAIN_ALBUM_GAIN=\0REPLAYGAIN_ALBUM_PEAK=\0R128_TRACK_GAIN=\0"
  "R128_ALBUM_GAIN=\0MUSICBRAINZ_TRACKID=\0MUSICBRAINZ_ALBUMID=\0"
  "MUSICBRAINZ_ARTISTID=\0MUSICBRAINZ_ALBUMARTISTID=\0METADATA_BLOCK_PICTURE=\0"
  "title=\0artist=\0album=\0album_artist=\0date=\0genre=\0track=\0disc=\0"
  "comment=\0description=\0composer=\0performer=\0copyright=\0encoder=\0"
  "encoded_by=\0language=\0DURATION=\0handler_name=\0major_brand=\0"
  "minor_version=\0compatible_brands=\0creation_time=\0vendor_id=\0";

static const char sd_options[] =
  "--quiet --ignorelength --raw --padding --discard-comments "
  "--discard-pictures --comp --expect-loss --max-delay --save-range "
  "--set-ctl-int --no-phase-inv --framesize --downmix-stereo --downmix-mono "
  "--cvbr --vbr --hard-cbr --speech --music --bitrate --serial ";

#define SD_MAXV 128
#define SD_MAXF 128
#define SD_BITS 7

typedef struct {
  char buf[8192];
  const char * v[SD_MAXV], * f[SD_MAXF];
  int nv, nf, nkey;
  sz used;
} sdict;

static const char * sd_put(sdict * t, const char * a, const char * b,
                           const char * c, const char * d) {
  char * s = t->buf + t->used;
  sz n = strlen(a) + strlen(b) + strlen(c) + strlen(d) + 1;
  FATAL_UNLESS(t->used + n <= sizeof t->buf, "comment: string table overflow");
  strcpy(s, a);  strcat(s, b);  strcat(s, c);  strcat(s, d);  t->used += n;
  return s;
}

#define SD_EACH(p, s) for (p = s; *p; p += strlen(p) + 1)

static void sd_build(sdict * t) {
  const char * p, * q;
  int k;
  t->nv = t->nf = 0;  t->used = 0;
  SD_EACH(p, sd_vorbis) t->v[t->nv++] = p;
  for (p = sd_ffmpeg, k = 0; *p; p += strlen(p) + 1, k++)
    if (!(k & 1)) t->v[t->nv++] = sd_put(t, "Lavf", p, "", "");
  SD_EACH(p, sd_libopus) t->v[t->nv++] = sd_put(t, "libopus ", p, "", "");
  SD_EACH(p, sd_libopus) SD_EACH(q, sd_opusenc)
    t->v[t->nv++] = sd_put(t, "libopus ", p, ", libopusenc ", q);
  SD_EACH(p, sd_tools)
    t->f[t->nf++] = sd_put(t, "ENCODER=opusenc from opus-tools ", p, "", "");
  for (p = sd_ffmpeg, k = 0; *p; p += strlen(p) + 1, k++)
    if (k & 1) {
      t->f[t->nf++] = sd_put(t, "encoder=Lavc", p, " libopus", "");
      t->f[t->nf++] = sd_put(t, "encoder=Lavc", p, " libvorbis", "");
    } else t->f[t->nf++] = sd_put(t, "encoder=Lavf", p, "", "");
  t->nkey = t->nf;
  SD_EACH(p, sd_keys) t->f[t->nf++] = p;
  FATAL_UNLESS(t->nv < SD_MAXV && t->nf < SD_MAXF,
               "comment: string table overflow");
}

static const char b64[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

typedef struct {
  cmt_io * io;
  u16 p[C_CONTEXTS][256];
  u8 count[C_CONTEXTS][256];
  u8 win[C_WINDOW], buf[C_BLOCK], out[C_BLOCK];
  u32 * hash, pos;
  u32 * dict;
  sz ndict;
  sz used;
  sdict t;
} cmt;

static u32 symbol(cmt * c, int ctx, int bits, u32 v) {
  u32 idx = 1;
  int k;
  for (k = bits - 1; k >= 0; k--) {
    u16 pr = c->p[ctx][idx] ^ RC_PINIT;
    int b = c->io->bit(c->io->coder, pr, (int) (v >> k & 1));
    c->p[ctx][idx] = rc_adapt(pr, &c->count[ctx][idx], RC_ALIM, b) ^ RC_PINIT;
    idx = idx * 2 + (u32) b;
  }
  return idx - (1UL << bits);
}

static void emit(cmt * c, const u8 * b, sz n) {
  if (c->io->enc) return;
  while (n) {
    sz take = MIN(n, C_BLOCK - c->used);
    memcpy(c->out + c->used, b, take);  c->used += take;
    b += take;  n -= take;
    if (c->used == C_BLOCK) {
      c->io->write(c->io->output, c->out, c->used);  c->used = 0;
    }
  }
}

static u32 le32(cmt_io * f, sz at) {
  u8 b[4];
  f->read(f->input, at, b, 4);
  return rd32le(b);
}

static void emit32(cmt * c, u32 v) {
  u8 b[4];
  wr32le(b, v);  emit(c, b, 4);
}

static int structured(cmt_io * f, sz n, int opus) {
  u8 magic[8];
  sz at = opus ? 8 : 7;
  u32 len, count, i;
  if (n < at + 8) return 0;
  f->read(f->input, 0, magic, at);
  if (memcmp(magic, opus ? "OpusTags" : "\003vorbis", at)) return 0;
  len = le32(f, at);  at += 4;
  if (len > n - at - 4) return 0;
  at += len;  count = le32(f, at);  at += 4;
  if (count > (n - at) / 4) return 0;
  Fi(count,
    if (n - at < 4) return 0;
    len = le32(f, at);  at += 4;
    if (len > n - at) return 0;
    at += len);
  return 1;
}

static u32 number(cmt * c, int ctx, u32 v) {
  u32 x;
  int i;
  x = symbol(c, C_IDX + 2 + (ctx == C_COUNT), 8, v < 255 ? v : 255);
  if (x < 255) return x;
  v -= 255;  x = 0;
  Fi(4, x |= symbol(c, ctx + i, 8, v >> (8 * i) & 255) << (8 * i));
  return x + 255;
}

static u32 field(cmt * c, sz * at, sz n, int ctx) {
  u32 v;
  FATAL_UNLESS(n - *at >= 4, "comment: truncated length");
  v = number(c, ctx, c->io->enc ? le32(c->io, *at) : 0);
  emit32(c, v);  *at += 4;
  return v;
}

static u32 hash4(const u8 * b) {
  u32 h = (u32) b[0] * 0x1E35A7BDUL ^ (u32) b[1] * 0x9E3779B1UL;
  h ^= (u32) b[2] << 8 | b[3];
  return (h ^ (h >> 16)) & (C_HASH - 1);
}

static u32 hlook(const cmt * c, u32 h) {
  sz lo = 0, hi = c->ndict, m;
  if (c->hash[h]) return c->hash[h];
  while (lo < hi) {
    m = lo + (hi - lo) / 2;
    if (c->dict[m] >> 16 < h) lo = m + 1;
    else hi = m;
  }
  return lo < c->ndict && c->dict[lo] >> 16 == h ? c->dict[lo] & 0xFFFF : 0;
}

static void sort_u32(u32 * a, sz n) {
  u32 * t = xmalloc(n * sizeof *t), * s = a, * d = t, * x;
  sz c[256], i, k, at;
  int b;
  for (b = 0; b < 32; b += 8) {
    memset(c, 0, sizeof c);
    Fi(n, c[s[i] >> b & 255]++);
    at = 0;  Fk(256, sz m = c[k];  c[k] = at;  at += m);
    Fi(n, d[c[s[i] >> b & 255]++] = s[i]);
    x = s;  s = d;  d = x;
  }
  xfree(t);
}

static void text(cmt * c, sz at, sz n, int role) {
  int prev = 0;
  while (n) {
    sz take = MIN(n, C_BLOCK), i = 0;
    if (c->io->enc) c->io->read(c->io->input, at, c->buf, take);
    while (i < take) {
      u32 dist = 0, len = 0, j;
      int match;
      if (c->io->enc && take - i >= 4) {
        u32 p = hlook(c, hash4(c->buf + i));
        if (p && c->pos - (p - 1) <= C_WINDOW) {
          dist = c->pos - (p - 1);
          while (len < C_MATCH && len < take - i) {
            u8 b = len < dist ? c->win[(p - 1 + len) & (C_WINDOW - 1)]
                               : c->buf[i + len - dist];
            if (b != c->buf[i + len]) break;
            len++;
          }
        }
      }
      match = (int) symbol(c, C_FLAG + role, 1, len >= 4);
      if (match) {
        len = 4 + symbol(c, C_MLEN + role, 8, len >= 4 ? len - 4 : 0);
        {
          u32 d = dist ? dist - 1 : 0;
          u32 lo = symbol(c, C_DIST, 8, d & 255);
          u32 hi = symbol(c, C_DIST + 1, 8, d >> 8);
          dist = 1 + (lo | hi << 8);
        }
        FATAL_UNLESS(dist && dist <= MIN(c->pos, C_WINDOW) && len <= take - i,
                     "comment: invalid text match");
      } else {
        c->buf[i] = (u8) symbol(c, role * 16 + (prev >> 4), 8,
                                 c->io->enc ? c->buf[i] : 0);
        len = 1;
      }
      Fj(len,
        if (c->io->enc) {
          if (i + j + 4 <= take) c->hash[hash4(c->buf + i + j)] = c->pos + 1;
        } else if (match)
          c->buf[i + j] = c->win[(c->pos - dist) & (C_WINDOW - 1)];
        c->win[c->pos++ & (C_WINDOW - 1)] = c->buf[i + j]);
      prev = c->buf[i + len - 1];  i += len;
    }
    emit(c, c->buf, take);  at += take;  n -= take;
  }
}

static void feed(cmt * c, const u8 * b, sz n) {
  sz j;
  Fj(n,
    if (c->io->enc && j + 4 <= n) c->hash[hash4(b + j)] = c->pos + 1;
    c->win[c->pos++ & (C_WINDOW - 1)] = b[j]);
}

static void put_str(cmt * c, const char * s) {
  sz n = strlen(s);
  emit32(c, (u32) n);  emit(c, (const u8 *) s, n);  feed(c, (const u8 *) s, n);
}

static u32 sd_find(cmt * c, const char * const * tab, int lo, int hi, sz at,
                   sz n, int whole) {
  u8 b[128];
  sz m = MIN(n, sizeof b), best = 0;
  u32 r = 0;
  int k;
  c->io->read(c->io->input, at, b, m);
  for (k = lo; k < hi; k++) {
    sz l = strlen(tab[k]);
    if (whole ? l == n && l <= m : l <= m && l > best)
      if (!memcmp(b, tab[k], l)) {
        r = (u32) k + 1;  best = l;  if (whole) break;
      }
  }
  return r;
}

static int un64(u8 b) {
  if (b >= 'A' && b <= 'Z') return b - 'A';
  if (b >= 'a' && b <= 'z') return b - 'a' + 26;
  if (b >= '0' && b <= '9') return b - '0' + 52;
  if (b == '+') return 62;
  if (b == '/') return 63;
  return -1;
}

static int base64(cmt_io * f, sz at, sz n, u32 * bytes) {
  u8 buf[4096];
  sz left = n, i;
  int pad = 0;
  if (!n || n % 4) return 0;
  while (left) {
    sz take = MIN(left, sizeof buf);
    f->read(f->input, at, buf, take);
    for (i = 0; i < take; i += 4) {
      int a = un64(buf[i]), b = un64(buf[i + 1]);
      int d = un64(buf[i + 2]), e = un64(buf[i + 3]);
      if (a < 0 || b < 0) return 0;
      if (left == take && i + 4 == take && buf[i + 3] == '=') {
        pad = buf[i + 2] == '=' ? 2 : 1;
        if (pad == 2 ? (b & 15) != 0 : d < 0 || (d & 3) != 0) return 0;
      } else if (d < 0 || e < 0) return 0;
    }
    at += take;  left -= take;
  }
  *bytes = (u32) (n / 4 * 3 - (sz) pad);
  return 1;
}

static void unquad(const u8 * b, u8 * raw) {
  int a = un64(b[0]), v = un64(b[1]), d = un64(b[2]), e = un64(b[3]);
  raw[0] = (u8) (a << 2 | v >> 4);
  raw[1] = (u8) (v << 4 | (d < 0 ? 0 : d >> 2));
  raw[2] = (u8) ((d < 0 ? 0 : d << 6) | (e < 0 ? 0 : e));
}

static u32 pic32(cmt_io * f, sz at, u32 off) {
  u8 b[8], raw[6];
  f->read(f->input, at + off / 3 * 4, b, 8);
  unquad(b, raw);  unquad(b + 4, raw + 3);
  return rd32be(raw + off % 3);
}

static int picture(cmt_io * f, sz at, sz n, u32 * bytes, u32 * head) {
  u32 v, p;
  if (!base64(f, at, n, bytes) || *bytes < 32) return 0;
  v = pic32(f, at, 4);
  if (v > *bytes - 32) return 0;
  p = 8 + v;  v = pic32(f, at, p);
  if (v > *bytes - p - 24) return 0;
  p += 4 + v + 16;
  v = pic32(f, at, p);
  *head = p + 4;
  return v == *bytes - *head;
}

static int picture_key(const u8 * b, sz n) {
  static const char key[] = "METADATA_BLOCK_PICTURE=";
  sz i;
  if (n != sizeof key - 1) return 0;
  Fi(n,
    int v = b[i];
    if (v >= 'a' && v <= 'z') v -= 'a' - 'A';
    if (v != key[i]) return 0);
  return 1;
}

static void image(cmt * c, sz at, sz n, u32 bytes, u32 head) {
  u32 pad = c->io->enc ? (u32) (n / 4 * 3) - bytes : 0;
  u32 i, prev = 0;
  pad = symbol(c, C_PAD, 2, pad);
  FATAL_UNLESS(n && n % 4 == 0 && pad <= 2, "comment: invalid base64 length");
  bytes = (u32) (n / 4 * 3) - pad;
  head = number(c, C_SIZE, head);
  FATAL_UNLESS(head >= 32 && head <= bytes, "comment: invalid picture header");
  for (i = 0; i < bytes; i += 3) {
    u8 b[4] = { 0, 0, 0, 0 }, raw[3] = { 0, 0, 0 };
    u32 j, take = MIN(3, bytes - i);
    if (c->io->enc) {
      sz off = (sz) i / 3 * 4;
      if (!(off % C_BLOCK))
        c->io->read(c->io->input, at + off, c->buf, MIN(C_BLOCK, n - off));
      memcpy(b, c->buf + off % C_BLOCK, 4);
      unquad(b, raw);
    }
    Fj(take,
      if (i + j < head)
        raw[j] = (u8) symbol(c, C_PICTURE * 16 + (int) (prev >> 4), 8, raw[j]);
      else {
        u32 v = 0;
        int k;
        for (k = 7; k >= 0; k--)
          v = v * 2 + (u32) c->io->bit(c->io->coder, RC_PINIT, raw[j] >> k & 1);
        raw[j] = (u8) v;
      }
      prev = raw[j]);
    b[0] = (u8) b64[raw[0] >> 2];
    b[1] = (u8) b64[(raw[0] & 3) << 4 | raw[1] >> 4];
    b[2] = take > 1 ? (u8) b64[(raw[1] & 15) << 2 | raw[2] >> 6] : '=';
    b[3] = take > 2 ? (u8) b64[raw[2] & 63] : '=';
    emit(c, b, 4);
  }
}

void cmt_code(cmt_io * io, sz n, int opus) {
  cmt * c = xcalloc(1, sizeof *c);
  sz at = opus & CMT_OPUS ? 8 : 7, j;
  u32 len, count, i;
  int form;
  const sdict * t = &c->t;
  u32 x = 0;
  FATAL_UNLESS(n <= CMT_MAXLEN, "comment: packet exceeds 120 MiB");
  c->io = io;
  rc_adapt_init();
  memcpy(c->win, dict, sizeof dict - 1);  c->pos = sizeof dict - 1;
  if (opus & CMT_ODICT) {
    memcpy(c->win + c->pos, dict_opus, sizeof dict_opus - 1);
    c->pos += sizeof dict_opus - 1;
  }
  if (opus & CMT_OPUS) {
    memcpy(c->win + c->pos, sd_options, sizeof sd_options - 1);
    c->pos += sizeof sd_options - 1;
  }
  if (io->enc) {
    sz k = 0, nd = c->pos - 3;
    c->hash = xcalloc(C_HASH, sizeof *c->hash);
    c->dict = xmalloc(nd * sizeof *c->dict);
    Fj(nd, c->dict[j] = hash4(c->win + j) << 16 | (u32) (j + 1));
    sort_u32(c->dict, nd);
    Fj(nd,
      if (j + 1 == nd || c->dict[j + 1] >> 16 != c->dict[j] >> 16)
        c->dict[k++] = c->dict[j]);
    c->ndict = k;
  }
  sd_build(&c->t);
  form = (int) symbol(c, C_FORM, 1,
                      io->enc ? (u32) structured(io, n, opus & CMT_OPUS) : 0);
  if (!form) text(c, 0, n, C_OPAQUE);
  else {
    FATAL_UNLESS(n >= at + 8, "comment: truncated header");
    emit(c, (const u8 *) (opus & CMT_OPUS ? "OpusTags" : "\003vorbis"), at);
    if (io->enc) x = sd_find(c, t->v, 0, t->nv, at + 4, le32(io, at), 1);
    x = symbol(c, C_IDX, SD_BITS, x);
    FATAL_UNLESS((int) x <= t->nv, "comment: invalid vendor string");
    if (x) {
      FATAL_UNLESS(strlen(t->v[x - 1]) <= n - at - 8,
                   "comment: invalid vendor length");
      put_str(c, t->v[x - 1]);  at += 4 + strlen(t->v[x - 1]);
    } else {
      len = field(c, &at, n, C_SIZE);
      FATAL_UNLESS(len <= n - at - 4, "comment: invalid vendor length");
      text(c, at, len, C_VENDOR);  at += len;
    }
    count = field(c, &at, n, C_COUNT);
    FATAL_UNLESS(count <= (n - at) / 4, "comment: invalid field count");
    Fi(count,
      u8 key[255];
      u32 split = 0, bytes = 0, head = 0;
      int pic = 0;
      x = 0;
      if (io->enc) {
        FATAL_UNLESS(n - at >= 4, "comment: truncated length");
        len = le32(io, at);
        FATAL_UNLESS(len <= n - at - 4, "comment: invalid field length");
        x = sd_find(c, t->f, 0, t->nkey, at + 4, len, 1);
        if (!x) x = sd_find(c, t->f, t->nkey, t->nf, at + 4, len, 0);
      }
      x = symbol(c, C_IDX + 1, SD_BITS, x);
      FATAL_UNLESS((int) x <= t->nf, "comment: invalid field string");
      if (x && (int) x <= t->nkey) {
        FATAL_UNLESS(n - at >= 4 && strlen(t->f[x - 1]) <= n - at - 4,
                     "comment: invalid field length");
        put_str(c, t->f[x - 1]);  at += 4 + strlen(t->f[x - 1]);
        continue;
      }
      if (x) {
        FATAL_UNLESS(n - at >= 4, "comment: truncated length");
        split = (u32) strlen(t->f[x - 1]);
        len = split + number(c, C_SIZE, io->enc ? le32(io, at) - split : 0);
        FATAL_UNLESS(len >= split && len <= n - at - 4,
                     "comment: invalid field length");
        emit32(c, len);  at += 4;
        emit(c, (const u8 *) t->f[x - 1], split);
        feed(c, (const u8 *) t->f[x - 1], split);
        if (io->enc && picture_key((const u8 *) t->f[x - 1], split))
          pic = picture(io, at + split, len - split, &bytes, &head);
      } else {
        len = field(c, &at, n, C_SIZE);
        FATAL_UNLESS(len <= n - at, "comment: invalid field length");
        if (io->enc) {
          sz take = MIN(len, sizeof key);
          io->read(io->input, at, key, take);
          Fj(take, if (key[j] == '=') { split = (u32) j + 1;  break; });
          if (picture_key(key, split))
            pic = picture(io, at + split, len - split, &bytes, &head);
        }
        split = symbol(c, C_SPLIT, 8, split);
        FATAL_UNLESS(split <= len, "comment: invalid key length");
        text(c, at, split, C_KEY);
      }
      pic = (int) symbol(c, C_FORM, 1, (u32) pic);
      if (pic) image(c, at + split, len - split, bytes, head);
      else text(c, at + split, len - split, C_VALUE);
      at += len);
    text(c, at, n - at, C_TAIL);
  }
  if (c->used) io->write(io->output, c->out, c->used);
  xfree(c->hash);  xfree(c->dict);  xfree(c);
}
