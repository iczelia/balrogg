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

typedef struct { sz off, end;  int len; } opkt;

typedef struct {
  opkt * pk;  int npk;  sz pkcap;
  opage * pg;  int npg;  sz pgcap;
  u32 serial;
  int channels;
  int nstreams, ncoupled;
} ostream;

static int head_streams(const u8 * h, int n, ostream * s) {
  s->channels = h[9];
  if (!h[18]) {
    s->nstreams = 1;  s->ncoupled = s->channels - 1;
    return s->channels >= 1 && s->channels <= 2 ? 0 : -1;
  }
  if ((h[18] != 1 && h[18] != 255) || n < 21 + s->channels ||
      s->channels < 1 || (h[18] == 1 && s->channels > 8))
    return -1;
  s->nstreams = h[19];  s->ncoupled = h[20];
  return s->nstreams >= 1 && s->ncoupled <= s->nstreams &&
         s->nstreams + s->ncoupled <= 255 ? 0 : -1;
}

static int sd_size(const u8 * d, int len, int * v) {
  if (len < 1) return -1;
  if (d[0] < 252) { *v = d[0];  return 1; }
  if (len < 2) return -1;
  *v = 4 * d[1] + d[0];  return 2;
}

static int ms_join(const u8 * p, int n, u8 * out) {
  unsigned char toc;
  opus_int16 sizes[48];
  int poff = 0, nf = opus_packet_parse(p, n, &toc, NULL, sizes, &poff), v, lb;
  if (nf < 0) return -1;
  v = (toc & 3) == 2 || ((toc & 3) == 3 && p[1] & 0x80) ? sizes[nf - 1]
                                                         : sizes[0];
  memcpy(out, p, (sz) poff);
  if (v < 252) { out[poff] = (u8) v;  lb = 1; }
  else {
    out[poff] = (u8) (252 + (v & 3));
    out[poff + 1] = (u8) ((v - out[poff]) >> 2);  lb = 2;
  }
  memcpy(out + poff + lb, p + poff, (sz) (n - poff));
  return n + lb;
}

static int ms_split(const u8 * d, int len, u8 * out, int * olen, u8 * check) {
  int h = 1, count = 1, cbr = 0, pad = 0, sum = 0, v, n, i, total;
  if (len < 1) return -1;
  switch (d[0] & 3) {
  case 1: count = 2;  cbr = 1;  break;
  case 2:
    count = 2;  n = sd_size(d + h, len - h, &v);
    if (n < 0) return -1;
    h += n;  sum += v;  break;
  case 3:
    if (len < 2 || !(d[1] & 63)) return -1;
    count = d[1] & 63;  cbr = !(d[1] & 0x80);  h = 2;
    if (d[1] & 0x40) {
      int p;
      do {
        if (h >= len) return -1;
        p = d[h++];  pad += p == 255 ? 254 : p;
      } while (p == 255);
    }
    if (!cbr)
      Fi(count - 1,
        n = sd_size(d + h, len - h, &v);
        if (n < 0) return -1;
        h += n;  sum += v);
    break;
  default: break;
  }
  n = sd_size(d + h, len - h, &v);
  if (n < 0 || v > 1275) return -1;
  sum += cbr ? v * count : v;
  total = h + n + sum + pad;
  if (total > len) return -1;
  memcpy(out, d, (sz) h);  memcpy(out + h, d + h + n, (sz) (total - h - n));
  *olen = total - n;
  if (ms_join(out, *olen, check) != total || !blr_same(check, d, (sz) total))
    return -1;
  return total;
}

static OpusDecoder ** decs_open(const ostream * s) {
  OpusDecoder ** d = xcalloc((sz) s->nstreams, sizeof *d);
  int k;
  Fk(s->nstreams,
    d[k] = opus_decoder_create(s->nstreams == 1 ? s->channels
                               : k < s->ncoupled ? 2 : 1);
    if (!d[k]) {
      while (k--) opus_decoder_destroy(d[k]);
      xfree(d);  return NULL;
    });
  return d;
}

static void decs_close(OpusDecoder ** d, int n) {
  int k;
  if (!d) return;
  Fk(n, opus_decoder_destroy(d[k]));
  xfree(d);
}

#define OM_MIDRATE 20000
#define OM_HIRATE  32000

static opus_uint64 st_bytes(const ostream * s) {
  opus_uint64 n = 0;
  int i;
  for (i = 2; i < s->npk; i++) n += (opus_uint64) s->pk[i].len;
  return n;
}

static opus_uint64 st_gran(const ostream * s) {
  int i;
  for (i = s->npg - 1; i >= 0; i--)
    if (s->pg[i].granule != ~(opus_uint64) 0 && s->pg[i].granule)
      return s->pg[i].granule;
  return 0;
}

static int st_rate(const ostream * s) {
  opus_uint64 n = st_bytes(s), g = st_gran(s);
  if (!g) return 0;
  g *= (opus_uint64) s->nstreams;
  return n * 48000 >= (opus_uint64) OM_HIRATE * g ? 2
       : n * 48000 >= (opus_uint64) OM_MIDRATE * g;
}

static int st_class(blr_file * input, const ostream * s) {
  opus_uint64 n = st_bytes(s), g = st_gran(s);
  u32 npk = 0, silk = 0, celt = 0, lm0 = 0, narrow = 0;
  int i, hi = g && n * 48000 >= (opus_uint64) OM_MIDRATE * g *
                                (opus_uint64) s->nstreams;
  for (i = 2; i < s->npk; i++) {
    int toc;
    if (!s->pk[i].len) continue;
    toc = bf_get(input, s->pk[i].off) >> 3;
    npk++;
    if (toc < 16) silk++;
    else { celt++;  lm0 += !(toc & 3);  narrow += toc < 24; }
  }
  if (2 * silk >= npk && npk) return OC_SILK;
  if (2 * lm0 >= celt && celt) return hi ? OC_LM0H : OC_LM0;
  if (2 * narrow >= celt && celt) return OC_NARROW;
  if (s->channels == 1) return OC_MONO;
  return hi ? OC_HIGH : OC_LOW;
}

static void st_free(ostream * s) {
  xfree(s->pk);  xfree(s->pg);
  memset(s, 0, sizeof *s);
}

static sz page_hop(blr_file * input, sz end, sz * at) {
  u8 h[OGG_HDRMIN + OGG_MAXSEG];
  sz n, i;
  bf_read(input, end, h, OGG_HDRMIN);
  FATAL_UNLESS(!memcmp(h, "OggS", 4), "opus: missing continuation page");
  n = h[26];  bf_read(input, end + OGG_HDRMIN, h + OGG_HDRMIN, n);
  *at = end = end + OGG_HDRMIN + n;
  Fi(n, end += h[OGG_HDRMIN + i]);
  return end;
}

static void packet_read(blr_file * input, const opkt * pk, u8 * packet) {
  sz at = pk->off, end = pk->end, left = (sz) pk->len;
  while (left) {
    sz take = MIN(left, end - at);
    bf_read(input, at, packet, take);
    packet += take;  at += take;  left -= take;
    if (left) end = page_hop(input, end, &at);
  }
}

typedef struct { sz off, start, len; } tag_extent;
typedef struct {
  blr_file * input;
  tag_extent * ext;
  sz count, len;
} tag_reader;

static void tag_open(tag_reader * r, blr_file * input, const opkt * pk) {
  sz at = pk->off, end = pk->end, left = (sz) pk->len, cap = 0;
  r->input = input;  r->ext = NULL;  r->count = r->len = 0;
  while (left) {
    sz take = MIN(left, end - at);
    if (take) {
      tag_extent * x;
      r->ext = xgrow(r->ext, &cap, r->count + 1, sizeof *r->ext, 16);
      x = r->ext + r->count++;
      x->off = at;  x->start = r->len;  x->len = take;  r->len += take;
    }
    at += take;  left -= take;
    if (left) end = page_hop(input, end, &at);
  }
}

static void tag_read(void * ctx, sz at, u8 * b, sz n) {
  tag_reader * r = ctx;
  sz lo = 0, hi = r->count;
  FATAL_UNLESS(at <= r->len && n <= r->len - at, "comment: read beyond packet");
  while (lo < hi) {
    sz mid = lo + (hi - lo) / 2;
    if (r->ext[mid].start + r->ext[mid].len <= at) lo = mid + 1;
    else hi = mid;
  }
  while (n) {
    const tag_extent * x = r->ext + lo++;
    sz off = at - x->start, take = MIN(n, x->len - off);
    bf_read(r->input, x->off + off, b, take);
    b += take;  at += take;  n -= take;
  }
}

static int comment_bit(void * ctx, u32 prob, int b) {
  (void) ctx;
  b = om_fbit(prob, b);
  PROF(prof_sym(om_comp, b ? prob : 0, b ? 65536 : prob, 65536));
  return b;
}

static int parse_stream(blr_file * input, sz * off, ostream * s) {
  sz at = *off, len = input->len, pstart = 0, pend = 0;
  u8 * page = xmalloc(OGG_HDRMIN + OGG_MAXSEG + 65025);
  u8 head[21 + 255];
  int acclen = 0, next = 0;
  memset(s, 0, sizeof *s);

  while (at + OGG_HDRMIN <= len) {
    int i, boff = 0;
    sz got;
    ogg_page p;
    const u8 * h = page;
    bf_read(input, at, page, OGG_HDRMIN);
    if (memcmp(h, "OggS", 4)) {
      fprintf(stderr, "balrogg: not an Ogg file\n");  goto bad;
    }
    if (h[4]) {
      fprintf(stderr, "balrogg: unsupported Ogg version %u\n", h[4]);  goto bad;
    }
    got = ogg_read(input, at, &p, page);
    if (!got) { fprintf(stderr, "balrogg: truncated Ogg page\n");  goto bad; }
    if (s->npg && (p.serial != s->serial ||
                   (p.type & 2 && !(p.type & 1) && p.np && p.plen[0] >= 8 &&
                    !memcmp(page + OGG_HDRMIN + p.nseg, "OpusHead", 8)))) {
      next = 1;  break;
    }
    if (s->npg == INT_MAX) {
      fprintf(stderr, "balrogg: too many Ogg pages\n");  goto bad;
    }
    s->pg = xgrow(s->pg, &s->pgcap, (sz) s->npg + 1, sizeof *s->pg, 1024);
    s->pg[s->npg].htype = p.type;
    s->pg[s->npg].nsegs = p.nseg;
    s->pg[s->npg].seqno = p.seq;
    s->pg[s->npg].granule = (opus_uint64) p.glo | (opus_uint64) p.ghi << 32;
    s->serial = p.serial;
    s->npg++;
    Fi(p.nseg,
      int sl = p.lace[i];
      int limit = s->npk == 1 ? (int) CMT_MAXLEN : OPUS_MAXPKT;
      if (sl > limit - acclen) {
        fprintf(stderr, "balrogg: Opus %s exceeds %d bytes\n",
                s->npk == 1 ? "comments" : "packet", limit);  goto bad;
      }
      if (!acclen) {
        pstart = at + OGG_HDRMIN + p.nseg + (sz) boff;  pend = at + got;
      }
      acclen += sl;  boff += sl;
      if (sl < 255) {
        if (s->npk == INT_MAX) {
          fprintf(stderr, "balrogg: too many Opus packets\n");  goto bad;
        }
        s->pk = xgrow(s->pk, &s->pkcap, (sz) s->npk + 1, sizeof *s->pk, 4096);
        s->pk[s->npk].off = pstart;
        s->pk[s->npk].end = pend;
        s->pk[s->npk].len = acclen;
        s->npk++;  acclen = 0;
      });
    at += got;
  }
  xfree(page);
  memset(head, 0, sizeof head);
  if (s->npk) { opkt first = s->pk[0];
    first.len = (int) MIN((sz) first.len, sizeof head);
    packet_read(input, &first, head); }
  if (at != len && !next) {
    fprintf(stderr, "balrogg: data follows the last Ogg page\n");  return 1;
  }
  if (acclen) {
    fprintf(stderr, "balrogg: incomplete final Opus packet\n");  return 1;
  }
  if (s->npk < 2 || s->pk[0].len < 8 || memcmp(head, "OpusHead", 8)) {
    fprintf(stderr, "balrogg: not an Ogg Opus stream\n");  return 1;
  }
  if (s->pk[0].len < 19) {
    fprintf(stderr, "balrogg: truncated OpusHead\n");  return 1;
  }
  if (head_streams(head, (int) MIN((sz) s->pk[0].len, sizeof head), s)) {
    if (head[18] && head[18] != 1 && head[18] != 255)
      fprintf(stderr, "balrogg: unsupported channel mapping family %d\n",
              head[18]);
    else
      fprintf(stderr, "balrogg: unsupported channel layout (%d channels, "
                      "family %d)\n", head[9], head[18]);
    return 1;
  }
  s->pk = xrealloc(s->pk, (sz) s->npk * sizeof *s->pk);
  s->pg = xrealloc(s->pg, (sz) s->npg * sizeof *s->pg);
  s->pkcap = s->npk;  s->pgcap = s->npg;
  *off = at;  return 0;
bad:
  xfree(page);  return 1;
}

typedef struct {
  const ostream * stream;
  blr_file * output;
  u8 * page;
  int pg, seg;
  sz body, partial;
} page_writer;

static void page_flush(page_writer * w) {
  const opage * p = w->stream->pg + w->pg;
  u8 * o = w->page;
  sz size = OGG_HDRMIN + (sz) p->nsegs + w->body;
  memcpy(o, "OggS", 4);  o[4] = 0;  o[5] = (u8) p->htype;
  wr64le(o + 6, p->granule);  wr32le(o + 14, w->stream->serial);
  wr32le(o + 18, p->seqno);  memset(o + 22, 0, 4);  o[26] = (u8) p->nsegs;
  ogg_crc_set(o, size);  bf_write(w->output, w->output->len, o, size);
  w->pg++;  w->seg = 0;  w->body = 0;
}

static void page_empty(page_writer * w) {
  while (w->pg < w->stream->npg && !w->stream->pg[w->pg].nsegs) page_flush(w);
}

static void page_lace(page_writer * w) {
  int ns = w->stream->pg[w->pg].nsegs;
  w->page[OGG_HDRMIN + w->seg++] = (u8) w->partial;
  w->partial = 0;
  if (w->seg == ns) page_flush(w);
}

static void page_bytes(void * ctx, const u8 * packet, sz len) {
  page_writer * w = ctx;
  while (len) {
    sz take = MIN(len, 255 - w->partial);
    int ns;
    page_empty(w);
    FATAL_UNLESS(w->pg < w->stream->npg, "opus: packets exceed page layout");
    ns = w->stream->pg[w->pg].nsegs;
    memcpy(w->page + OGG_HDRMIN + ns + w->body, packet, take);
    w->body += take;  w->partial += take;  packet += take;  len -= take;
    if (w->partial == 255) page_lace(w);
  }
}

static void page_endpacket(page_writer * w) {
  page_empty(w);
  FATAL_UNLESS(w->pg < w->stream->npg, "opus: packets exceed page layout");
  page_lace(w);
}

static void page_packet(page_writer * w, const u8 * packet, sz len) {
  page_bytes(w, packet, len);  page_endpacket(w);
}
