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

#define K_TOC     ((okey) 0x10000000UL << 32)
#define K_HDR     ((okey) 0x11000000UL << 32)
#define K_NHDR    ((okey) 0x12000000UL << 32)
#define K_LEN     ((okey) 0x13000000UL << 32)
#define K_TRL     ((okey) 0x15000000UL << 32)
#define K_HTYPE   ((okey) 0x17000000UL << 32)
#define K_NSEGS   ((okey) 0x18000000UL << 32)
#define K_SEQOK   ((okey) 0x19000000UL << 32)
#define K_GDOK    ((okey) 0x1A000000UL << 32)
#define K_GDELTA  ((okey) 0x1B000000UL << 32)
#define K_RAW     ((okey) 0x1D000000UL << 32)
#define K_FSLACK  ((okey) 0x1E000000UL << 32)
#define K_FBYTE   ((okey) 0x1F000000UL << 32)
#define K_LINK    ((okey) 0x20000000UL << 32)
#define K_GAPCLS  ((okey) 0x21000000UL << 32)
#define K_FHIT    ((okey) 0x22000000UL << 32)
#define K_COUNT   ((okey) 0x23000000UL << 32)
#define K_CHOICE  ((okey) 0x25000000UL << 32)

#define OPUS_MAXPKT 61440

#define OC_BLOBESC 4095
#define OC_HDRESC  15
#define OC_HEADMAX 64

typedef struct {
  u8 prev_hdr[8];
  int prev_nhdr, prev_len, prev_htype, prev_nsegs;
  u32 prev_seq;
  opus_uint64 prev_gran;
  opus_int64 prev_gdelta;
} oc_state;

typedef struct { int htype, nsegs;  u32 seqno;  opus_uint64 granule; } opage;

static void byte_ctx(okey base, int slot, int prev, int * v) {
  om_int(base | (okey) slot << 16 | (okey) (prev & 0xFF), 256, 192, 28, 16384, v);
}

enum { GAP_LIBOPUS, GAP_LAVC, GAP_OTHER, GAP_NCLS };
#define FH_N   4
#define FH_CAP 1276

static int gap_cls;
static u32 fh_lo, fh_span;
static void resid(okey base, opus_int64 * v);
static u8 fh[FH_N][FH_CAP];
static u32 fhn[FH_N];
static int fh_at;
#define FH(j)  fh[(fh_at + (j)) % FH_N]
#define FHN(j) fhn[(fh_at + (j)) % FH_N]

static void fh_reset(void) {
  memset(fhn, 0, sizeof fhn);  fh_at = 0;  fh_lo = fh_span = 0;
}

static int gap_class(const u8 * vendor, sz n) {
  if (n >= 7 && !memcmp(vendor, "libopus", 7)) return GAP_LIBOPUS;
  if ((n >= 6 && !memcmp(vendor, "ffmpeg", 6)) ||
      (n >= 4 && (!memcmp(vendor, "Lavf", 4) || !memcmp(vendor, "Lavc", 4))))
    return GAP_LAVC;
  return GAP_OTHER;
}

static void fh_push(const u8 * buf, u32 n) {
  fh_at = (fh_at + FH_N - 1) % FH_N;
  FHN(0) = MIN(n, FH_CAP);  memcpy(FH(0), buf, FHN(0));
}

void om_frame(u8 * buf, const u8 * mir, u32 n) {
  int diff = 0, prev = 0, last = FH_N;
  u32 lo = 0, hi = 0, i, span = 0;
  okey cls = (okey) gap_cls << 16;
  if (om_mode != OM_DEC) {
    while (lo < n && buf[lo] == mir[lo]) lo++;
    if (lo < n) {
      diff = 1;
      hi = n - 1;
      while (buf[hi] == mir[hi]) hi--;
      span = hi - lo;
    }
  }
  om_bit(K_FSLACK | cls, gap_cls != GAP_LAVC
         ? (u16) (65535U - (65536U >> 12)) : 0x8000, &diff);
  if (!diff) { fh_push(buf, n);  return; }
  if (gap_cls == GAP_LAVC) {
    opus_int64 d = (opus_int64) lo - fh_lo;
    resid(K_FHIT | (okey) 0xF0 << 8, &d);
    lo = (u32) ((opus_int64) fh_lo + d);
    FATAL_IF_HOT(om_mode == OM_DEC && (d < -(opus_int64) fh_lo || lo >= n))
      ("opus: frame patch starts at %lu of %lu", (unsigned long) lo,
       (unsigned long) n);
    d = (opus_int64) span - fh_span;
    resid(K_FHIT | (okey) 0xF1 << 8, &d);
    span = (u32) ((opus_int64) fh_span + d);
    FATAL_IF_HOT(om_mode == OM_DEC &&
                 (d < -(opus_int64) fh_span || span >= n - lo))
      ("opus: frame patch spans %lu of %lu", (unsigned long) span,
       (unsigned long) (n - lo));
    fh_lo = lo;  fh_span = span;
  } else {
    p_uniform(&lo, n);
    FATAL_IF_HOT(om_mode == OM_DEC && lo >= n)
      ("opus: frame patch starts at %lu of %lu", (unsigned long) lo,
       (unsigned long) n);
    p_uniform(&span, n - lo);
  }
  hi = lo + span;
  for (i = lo; i <= hi; i++) {
    int v = om_mode == OM_DEC ? 0 : buf[i], j, k, hit = 0;
    for (j = 0; j < FH_N && !hit; j++) {
      int c;
      if (i >= FHN(j)) continue;
      c = FH(j)[i];
      Fk(j, if (i < FHN(k) && FH(k)[i] == c) break);
      if (k < j) continue;
      hit = om_mode != OM_DEC && v == c;
      om_bit(K_FHIT | cls | (okey) j << 8 | (okey) last, 0x8000, &hit);
      if (hit) { v = c;  last = j; }
    }
    if (!hit) { byte_ctx(K_FBYTE, 0, prev, &v);  last = FH_N; }
    buf[i] = (u8) v;
    prev = v;
  }
  fh_push(buf, n);
}

static void resid(okey base, opus_int64 * v) {
  int zero, sg, nbits = 0;
  opus_uint64 mag;
  zero = om_mode != OM_DEC && *v == 0;
  sg = om_mode != OM_DEC && *v < 0;
  mag = om_mode != OM_DEC ? sg ? 0 - (opus_uint64) *v : (opus_uint64) *v : 0;
  om_bit(base | 1, 0x8000, &zero);
  if (zero) { if (om_mode == OM_DEC) *v = 0;  return; }
  om_bit(base | 2, 0x8000, &sg);
  if (om_mode != OM_DEC) nbits = (int) blr_ilog64(mag) - 1;
  om_int(base | 3, 64, 192, 28, 16384, &nbits);
  if (nbits > 0) {
    opus_uint64 m = 0;
    int sh = nbits;
    while (sh > 0) {
      int k = MIN(16, sh);
      u32 part;
      sh -= k;
      part = om_mode == OM_DEC ? 0 : (u32) (mag >> sh & ((1UL << k) - 1));
      p_uniform(&part, (u32) 1 << k);
      m |= (opus_uint64) part << sh;
    }
    if (om_mode == OM_DEC) mag = (opus_uint64) 1 << nbits | m;
  } else if (om_mode == OM_DEC) mag = 1;
  if (om_mode == OM_DEC) *v = (opus_int64) (sg ? 0 - mag : mag);
}

static int oc_len(okey key, int esc, int n) {
  int v = MIN(n, esc);
  om_int(key, esc + 1, 192, 28, 16384, &v);
  if (v == esc) {
    u32 extra = om_mode == OM_DEC ? 0 : (u32) (n - esc);
    p_uniform(&extra, OPUS_MAXPKT - esc + 1);
    v += (int) extra;
  }
  return v;
}

static void oc_packet_hdr(oc_state * s, int * nhdr, u8 * hdr, int * len) {
  int i, v = oc_len(K_NHDR | (okey) (s->prev_nhdr & 15), OC_HDRESC, *nhdr);
  opus_int64 d;
  *nhdr = v;  s->prev_nhdr = v;
  Fi(*nhdr,
    int b = om_mode == OM_DEC ? 0 : hdr[i];
    byte_ctx(i ? K_HDR : K_TOC, MIN(i, 8), i < 8 ? s->prev_hdr[i] : 0, &b);
    hdr[i] = (u8) b;
    if (i < 8) s->prev_hdr[i] = (u8) b);
  d = om_mode == OM_DEC ? 0 : (opus_int64) *len - (opus_int64) s->prev_len;
  resid(K_LEN, &d);
  if (om_mode == OM_DEC) {
    opus_int64 next = (opus_int64) s->prev_len + d;
    FATAL_UNLESS(next >= INT_MIN && next <= INT_MAX,
                 "opus: packet length is out of range");
    *len = (int) next;
  }
  s->prev_len = *len;
}

static void oc_packet_trailer(int n, u8 * t) {
  int i;
  Fi(n,
    int b = om_mode == OM_DEC ? 0 : t[i];
    byte_ctx(K_TRL, MIN(i, 8), i ? t[i - 1] : 0, &b);
    t[i] = (u8) b);
}

static u8 oh_prev[OC_HEADMAX];
static int oh_n;

static void oh_reset(void) {
  static const u8 dflt[19] = { 'O', 'p', 'u', 's', 'H', 'e', 'a', 'd',
                               1, 2, 0x38, 1, 0x80, 0xBB, 0, 0, 0, 0, 0 };
  memcpy(oh_prev, dflt, sizeof dflt);  oh_n = (int) sizeof dflt;
}

static void oc_blob(u8 * b, int * n) {
  int i, head = (om_feat & OF_HEAD) != 0;
  int same = head && om_mode != OM_DEC && *n == oh_n;
  if (head) om_bit(K_RAW | 0x30000, 0xF000, &same);
  *n = same ? oh_n : oc_len(K_RAW, OC_BLOBESC, *n);
  Fi(*n,
    int x = om_mode == OM_DEC ? 0 : b[i], hit = 0;
    if (head && i < oh_n) {
      hit = om_mode != OM_DEC && x == oh_prev[i];
      om_bit(K_RAW | 0x20000 | (okey) MIN(i, 31), 0xF000, &hit);
      if (hit) x = oh_prev[i];
    }
    if (!hit) byte_ctx(K_RAW | 0x10000, 0, i ? b[i - 1] : 0, &x);
    b[i] = (u8) x);
  if (head && *n <= OC_HEADMAX) { memcpy(oh_prev, b, (sz) *n);  oh_n = *n; }
}

static void oc_u32(u32 * v) {
  u32 hi = om_mode == OM_DEC ? 0 : *v >> 16;
  u32 lo = om_mode == OM_DEC ? 0 : *v & 0xFFFF;
  p_uniform(&hi, 65536);  p_uniform(&lo, 65536);
  if (om_mode == OM_DEC) *v = hi << 16 | lo;
}

static void oc_count(okey base, u32 * v) {
  opus_int64 d = om_mode == OM_DEC ? 0 : (opus_int64) *v;
  if (!(om_feat & OF_COUNT)) { oc_u32(v);  return; }
  resid(base, &d);
  FATAL_UNLESS(d >= 0 && d <= (opus_int64) 0xFFFFFFFFUL,
               "opus: count is out of range");
  *v = (u32) d;
}

static void oc_page(oc_state * s, opage * p) {
  int v;
  opus_int64 dd;
  v = om_mode == OM_DEC ? 0 : p->htype;
  om_int(K_HTYPE | (okey) s->prev_htype, 256, 192, 28, 16384, &v);
  p->htype = v;  s->prev_htype = v;

  v = om_mode == OM_DEC ? 0 : p->nsegs;
  om_int(K_NSEGS | (okey) s->prev_nsegs, 256, 192, 28, 16384, &v);
  p->nsegs = v;  s->prev_nsegs = v;

  v = om_mode == OM_DEC ? 0 : p->seqno == s->prev_seq + 1;
  om_bit(K_SEQOK, 0xF000, &v);
  if (v) { if (om_mode == OM_DEC) p->seqno = s->prev_seq + 1; }
  else { u32 x = p->seqno;  oc_u32(&x);  p->seqno = x; }
  s->prev_seq = p->seqno;

  v = om_mode == OM_DEC ? 0 : p->granule == ~(opus_uint64) 0;
  om_bit(K_GDOK | 1, 0x0400, &v);
  if (v) { if (om_mode == OM_DEC) p->granule = ~(opus_uint64) 0;  return; }
  dd = om_mode == OM_DEC ? 0
     : (opus_int64) (p->granule - s->prev_gran - (opus_uint64) s->prev_gdelta);
  resid(K_GDELTA, &dd);
  if (om_mode == OM_DEC)
    p->granule = s->prev_gran + (opus_uint64) s->prev_gdelta + (opus_uint64) dd;
  s->prev_gdelta = (opus_int64) (p->granule - s->prev_gran);
  s->prev_gran = p->granule;
}
