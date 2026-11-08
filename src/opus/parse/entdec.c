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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arch.h"
#include "entdec.h"
#include "entenc.h"
#include "mfrngcod.h"

#include "opus/ent.h"
#include "celt.h"

static unsigned isqrt32(opus_uint32 _val){
  unsigned b;
  unsigned g;
  int      bshift;
  g=0;
  bshift=(EC_ILOG(_val)-1)>>1;
  b=1U<<bshift;
  do{
    opus_uint32 t, m;
    t=(((opus_uint32)g<<1)+b)<<bshift;
    m=0U-(opus_uint32)(t<=_val);
    g+=b&m;
    _val-=t&m;
    b>>=1;
    bshift--;
  }
  while(bshift>=0);
  return g;
}

int orec_mode = OREC_OFF;

void blr_fatal(const char * fmt, ...);
static void oe_die(const char * msg) {
  blr_fatal("opus: %s", msg);
}

static int ec_read_byte(ec_dec * d) {
  return d->offs < d->storage ? d->buf[d->offs++] : 0;
}

static int ec_read_byte_from_end(ec_dec * d) {
  return d->end_offs < d->storage ? d->buf[d->storage - ++(d->end_offs)] : 0;
}

static void ec_dec_normalize(ec_dec * d) {
  while (d->rng <= EC_CODE_BOT) {
    int sym;
    d->nbits_total += EC_SYM_BITS;  d->rng <<= EC_SYM_BITS;  d->nnorm++;
    sym = d->rem;  d->rem = ec_read_byte(d);
    sym = (sym << EC_SYM_BITS | d->rem) >> (EC_SYM_BITS - EC_CODE_EXTRA);
    d->val = ((d->val << EC_SYM_BITS) + (EC_SYM_MAX & ~sym)) & (EC_CODE_TOP - 1);
  }
}

static void raw_dec_init(ec_dec * d, unsigned char * buf, opus_uint32 storage) {
  d->buf = buf;  d->storage = storage;
  d->end_offs = 0;  d->end_window = 0;  d->nend_bits = 0;
  d->nbits_total = EC_CODE_BITS + 1
    - ((EC_CODE_BITS - EC_CODE_EXTRA) / EC_SYM_BITS) * EC_SYM_BITS;
  d->offs = 0;  d->rng = 1U << EC_CODE_EXTRA;  d->rem = ec_read_byte(d);
  d->val = d->rng - 1 - (d->rem >> (EC_SYM_BITS - EC_CODE_EXTRA));
  d->error = 0;
  ec_dec_normalize(d);
  d->nnorm = 0;  d->nraw = 0;  d->ostorage = storage;  d->oslot = -1;
}

static void oe_patch(ec_dec * d, opus_uint32 at, opus_uint32 v, int bits);

static unsigned raw_decode(ec_dec * d, unsigned ft) {
  unsigned s;
  d->ext = celt_udiv(d->rng, ft);
  s = (unsigned) (d->val / d->ext);
  return ft - EC_MINI(s + 1, ft);
}

static unsigned raw_decode_bin(ec_dec * d, unsigned bits) {
  unsigned s;
  d->ext = d->rng >> bits;
  s = (unsigned) (d->val / d->ext);
  return (1U << bits) - EC_MINI(s + 1U, 1U << bits);
}

static void raw_dec_update(ec_dec * d, unsigned fl, unsigned fh, unsigned ft) {
  opus_uint32 s = IMUL32(d->ext, ft - fh);
  d->val -= s;
  d->rng = fl > 0 ? IMUL32(d->ext, fh - fl) : d->rng - s;
  ec_dec_normalize(d);
}

static int raw_dec_bit_logp(ec_dec * d, unsigned logp) {
  opus_uint32 r = d->rng, v = d->val, s = r >> logp;
  int ret = v < s;
  if (!ret) d->val = v - s;
  d->rng = ret ? s : r - s;
  ec_dec_normalize(d);
  return ret;
}

static int raw_dec_icdf(ec_dec * d, const unsigned char * icdf, unsigned ftb) {
  opus_uint32 r, v, s, t;
  int ret;
  s = d->rng;  v = d->val;  r = s >> ftb;  ret = -1;
  do { t = s;  s = IMUL32(r, icdf[++ret]); } while (v < s);
  d->val = v - s;  d->rng = t - s;
  ec_dec_normalize(d);
  return ret;
}

static int raw_dec_icdf16(ec_dec * d, const opus_uint16 * icdf, unsigned ftb) {
  opus_uint32 r, v, s, t;
  int ret;
  s = d->rng;  v = d->val;  r = s >> ftb;  ret = -1;
  do { t = s;  s = IMUL32(r, icdf[++ret]); } while (v < s);
  d->val = v - s;  d->rng = t - s;
  ec_dec_normalize(d);
  return ret;
}

static opus_uint32 raw_dec_bits(ec_dec * d, unsigned bits) {
  ec_window w = d->end_window;
  int avail = d->nend_bits;
  opus_uint32 ret;
  if ((unsigned) avail < bits) {
    do {
      w |= (ec_window) ec_read_byte_from_end(d) << avail;
      avail += EC_SYM_BITS;
    } while (avail <= EC_WINDOW_SIZE - EC_SYM_BITS);
  }
  ret = (opus_uint32) w & (((opus_uint32) 1 << bits) - 1U);
  w >>= bits;  avail -= bits;
  d->end_window = w;  d->nend_bits = avail;  d->nbits_total += bits;  d->nraw += bits;
  return ret;
}

static opus_uint32 raw_dec_uint(ec_dec * d, opus_uint32 ft) {
  unsigned f, s;
  int ftb;
  celt_assert(ft > 1);
  ft--;
  ftb = EC_ILOG(ft);
  if (ftb > EC_UINT_BITS) {
    opus_uint32 t;
    ftb -= EC_UINT_BITS;
    f = (unsigned) (ft >> ftb) + 1;
    s = raw_decode(d, f);
    raw_dec_update(d, s, s + 1, f);
    t = (opus_uint32) s << ftb | raw_dec_bits(d, ftb);
    if (t <= ft) return t;
    oe_patch(d, d->nraw - (opus_uint32) ftb, ft & ((1U << ftb) - 1), ftb);
    return ft;
  }
  ft++;
  s = raw_decode(d, (unsigned) ft);
  raw_dec_update(d, s, s + 1, (unsigned) ft);
  return s;
}

#define OE_MAXCTX 8
#define OE_NPATCH 8

typedef struct { oprec op;  opus_uint32 storage; } oe_op;

typedef struct {
  ec_ctx * p;
  int live;
  ec_ctx mir;
  unsigned char * mbuf;
  opus_uint32 mcap, storage0;
  const unsigned char * obuf;
  int has;
  oe_op * log;
  int nlog, clog;
  opus_uint32 npatch, ppos[OE_NPATCH], pval[OE_NPATCH];
} oe_ctx;

static oe_ctx oc[OE_MAXCTX];
static int oc_n;

static oe_ctx * oc_find(ec_ctx * p) {
  int i;
  for (i = 0; i < oc_n; i++) if (oc[i].p == p) return &oc[i];
  return NULL;
}

static void oe_patch(ec_dec * d, opus_uint32 at, opus_uint32 v, int bits) {
  oe_ctx * c = d->oslot >= 0 ? &oc[d->oslot] : NULL;
  if (!c || c->npatch == OE_NPATCH) { d->error |= 1;  return; }
  c->ppos[c->npatch] = at;  c->pval[c->npatch++] = v | (opus_uint32) bits << 24;
  d->error |= 8;
}

int orec_log, orec_lost;

static void oe_log(ec_dec * d, const oprec * op) {
  oe_ctx * c;
  oe_op * o;
  if (d->oslot < 0) return;
  c = &oc[d->oslot];
  if (!c->has) return;
  if (c->nlog == c->clog) {
    int n = c->clog ? 2 * c->clog : 1024;
    oe_op * l = realloc(c->log, (size_t) n * sizeof *l);
    if (!l) oe_die("out of memory for the frame's operations");
    c->log = l;  c->clog = n;
  }
  o = c->log + c->nlog++;
  o->op = *op;  o->storage = d->storage;
  if (op->kind == OP_ICDF) {
    const unsigned char * t = op->pdf;
    o->op.nsym = t[op->v];  o->op.aux = op->v > 0 ? t[op->v - 1] : 0;
  } else if (op->kind == OP_ICDF16) {
    const opus_uint16 * t = op->pdf;
    o->op.nsym = t[op->v];  o->op.aux = op->v > 0 ? t[op->v - 1] : 0;
  }
}

static OPUS_INLINE void oe_note(ec_dec * d, const oprec * op, opus_int32 v) {
  d->ostorage = d->storage;
  if (op->v != v) d->error |= 2;
  if (orec_log) oe_log(d, op);
}

static void theta_interval(int mode, int qn, int x, unsigned * fl, unsigned * fh, unsigned * ft) {
  if (mode == 0) {
    int p0 = 3, x0 = qn / 2;
    *ft = (unsigned) (p0 * (x0 + 1) + x0);
    *fl = (unsigned) (x <= x0 ? p0 * x : (x - 1 - x0) + (x0 + 1) * p0);
    *fh = (unsigned) (x <= x0 ? p0 * (x + 1) : (x - x0) + (x0 + 1) * p0);
  } else {
    int t = ((qn >> 1) + 1) * ((qn >> 1) + 1), fs, l;
    fs = x <= (qn >> 1) ? x + 1 : qn + 1 - x;
    l = x <= (qn >> 1) ? x * (x + 1) >> 1 : t - ((qn + 1 - x) * (qn + 2 - x) >> 1);
    *ft = (unsigned) t;  *fl = (unsigned) l;  *fh = (unsigned) (l + fs);
  }
}

static void oe_encode(ec_enc * e, const oprec * op) {
  switch (op->kind) {
  case OP_LOGP: ec_enc_bit_logp(e, op->v, op->ftb);  break;
  case OP_ICDF: {
    unsigned char t[2];
    t[0] = (unsigned char) op->aux;  t[1] = (unsigned char) op->nsym;
    ec_enc_icdf(e, op->v > 0, op->v > 0 ? t : t + 1, op->ftb);
    break; }
  case OP_ICDF16: {
    opus_uint16 t[2];
    t[0] = (opus_uint16) op->aux;  t[1] = (opus_uint16) op->nsym;
    ec_enc_icdf16(e, op->v > 0, op->v > 0 ? t : t + 1, op->ftb);
    break; }
  case OP_UINT: ec_enc_uint(e, (opus_uint32) op->v, op->nsym);  break;
  case OP_BITS: ec_enc_bits(e, (opus_uint32) op->v, op->ftb);  break;
  case OP_LAPLACE: { int v = op->v;  ec_laplace_encode(e, &v, op->aux, (int) op->ftb);  break; }
  case OP_THETA:
    if (op->ftb == 1) ec_enc_uint(e, (opus_uint32) op->v, op->nsym);
    else {
      unsigned fl, fh, ft;
      theta_interval((int) op->ftb, (int) op->nsym - 1, op->v, &fl, &fh, &ft);
      ec_encode(e, fl, fh, ft);
    }
    break;
  default: break;
  }
}

static int oe_derive(oe_ctx * c) {
  const ec_dec * d = c->p;
  const unsigned char * in = c->obuf;
  unsigned char * out;
  ec_enc * m = &c->mir;
  opus_uint32 st = d->ostorage, j = d->nnorm, full = d->nraw >> 3, part = d->nraw & 7, cw = 0, dd, i, t, w, cap;
  int borrow;
  if (d->error & ~8 || d->val >= d->rng || j > 0x7FFFFFF0U) return 0;
  if (d->nraw && (j > st || full > st || (part && full >= st))) return 0;
  cap = d->nraw || st > j + 8 ? st : j + 8;
  if (cap > c->mcap) {
    free(c->mbuf);
    c->mbuf = malloc(cap);
    if (!c->mbuf) oe_die("out of memory for the mirror frame");
    c->mcap = cap;
  }
  out = c->mbuf;
  for (i = 0; i < 4; i++) cw = cw << 8 | (j + i < d->offs ? in[j + i] : 0);
  cw = cw >> (EC_SYM_BITS - EC_CODE_EXTRA) & (EC_CODE_TOP - 1);
  dd = d->rng - 1 - d->val;
  borrow = cw < dd;
  for (i = 0; i < j; i++) out[i] = i < d->offs ? in[i] : 0;
  if (borrow) {
    for (i = j; i > 0 && !out[i - 1]; i--) out[i - 1] = EC_SYM_MAX;
    if (!i) return 0;
    out[i - 1]--;
  }
  for (t = 0; t < j && out[j - 1 - t] == EC_SYM_MAX; t++);
  w = t < j ? j - 1 - t : 0;
  if (w + full > cap) return 0;
  ec_enc_init(m, out, cap);
  m->val = (cw - dd + (borrow ? EC_CODE_TOP : 0)) & (EC_CODE_TOP - 1);
  m->rng = d->rng;
  m->rem = t < j ? out[w] : -1;  m->ext = t;  m->offs = w;
  for (i = 0; i < full; i++) out[st - 1 - i] = in[st - 1 - i];
  m->end_offs = full;
  m->end_window = part ? in[st - 1 - full] & ((1U << part) - 1) : 0;
  m->nend_bits = (int) part;
  for (i = 0; i < c->npatch; i++) {
    opus_uint32 b = c->ppos[i], v = c->pval[i] & 0xFFFFFF, k, n = c->pval[i] >> 24;
    for (k = 0; k < n; k++, b++) {
      opus_uint32 x = v >> k & 1;
      if (b >> 3 < full) {
        unsigned char * q = out + st - 1 - (b >> 3);
        *q = (unsigned char) ((*q & ~(1U << (b & 7))) | x << (b & 7));
      } else m->end_window = (m->end_window & ~(1U << (b - 8 * full))) | x << (b - 8 * full);
    }
  }
  ec_enc_done(m);
  m->storage = st;
  return !m->error;
}

static void oe_replay(oe_ctx * c) {
  int i;
  ec_enc_init(&c->mir, c->mbuf, c->storage0);
  for (i = 0; i < c->nlog; i++) {
    c->mir.storage = c->log[i].storage;
    oe_encode(&c->mir, &c->log[i].op);
  }
  ec_enc_done(&c->mir);
}

static void oe_close(oe_ctx * c) {
  if (!c->live) return;
  c->live = 0;
  if (orec_mode == OREC_ANALYZE) {
    if (!c->has) return;
    if (!oe_derive(c)) {
      if (orec_log) oe_replay(c);
      else { orec_lost = 1;  c->mir.storage = c->p->ostorage;  memcpy(c->mbuf, c->obuf, c->mir.storage); }
    }
    om_frame((unsigned char *) c->obuf, c->mbuf, c->mir.storage);
  } else {
    ec_ctx e = *c->p;
    e.storage = e.ostorage;
    ec_enc_done(&e);
    if (c->has) om_frame(e.buf, NULL, e.storage);
  }
}

void oe_begin(void) {
  int i;
  for (i = 0; i < OE_MAXCTX; i++) {
    oc[i].p = NULL;  oc[i].live = 0;  oc[i].obuf = NULL;  oc[i].has = 0;
  }
  oc_n = 0;
}

void oe_reset(void) {
  int i;
  orec_band = orec_ch = orec_LM = orec_intra = orec_band1 = orec_bLM = 0;
  orec_pvqN = orec_pvqK = orec_ftb = 0;
  orec_cframe = 0;
  oe_begin();
  for (i = 0; i < OE_MAXCTX; i++) {
    free(oc[i].mbuf);  oc[i].mbuf = NULL;  oc[i].mcap = 0;
    free(oc[i].log);  oc[i].log = NULL;  oc[i].clog = oc[i].nlog = 0;
  }
}

void oe_end(void) {
  int i;
  for (i = 0; i < oc_n; i++) oe_close(&oc[i]);
}

void ec_dec_init(ec_dec * d, unsigned char * buf, opus_uint32 storage) {
  oe_ctx * c;
  if (orec_mode == OREC_OFF) { raw_dec_init(d, buf, storage);  return; }
  c = oc_find(d);
  if (c) oe_close(c);
  else {
    if (oc_n >= OE_MAXCTX) oe_die("too many live entropy contexts in one packet");
    c = &oc[oc_n++];
  }
  c->p = d;  c->live = 1;
  c->has = buf != NULL && storage != 0;
  c->obuf = buf;  c->nlog = 0;  c->npatch = 0;  c->storage0 = storage;
  if (orec_mode == OREC_ANALYZE) {
    raw_dec_init(d, buf, storage);
    if (c->has && storage > c->mcap) {
      free(c->mbuf);
      c->mbuf = malloc(storage);
      if (!c->mbuf) oe_die("out of memory for the mirror frame");
      c->mcap = storage;
    }
  } else {
    ec_enc_init(d, buf, storage);
    d->ostorage = storage;
  }
  d->oslot = (int) (c - oc);
}

void ec_dec_shrink(ec_dec * d, opus_uint32 n) {
  if (orec_mode == OREC_ANALYZE && d->nraw) d->error |= 4;
  d->storage -= n;
}

unsigned ec_decode_bin(ec_dec * d, unsigned bits) { return raw_decode_bin(d, bits); }
void ec_dec_update(ec_dec * d, unsigned fl, unsigned fh, unsigned ft) {
  raw_dec_update(d, fl, fh, ft);
}

int ec_dec_bit_logp(ec_dec * d, unsigned logp, int site) {
  oprec op;
  if (orec_mode == OREC_OFF) return raw_dec_bit_logp(d, logp);
  op.kind = OP_LOGP;  op.site = site;
  op.pdf = NULL;  op.ftb = logp;  op.nsym = 2;  op.aux = 0;
  if (orec_mode == OREC_ANALYZE) {
    { opus_int32 dv_ = raw_dec_bit_logp(d, logp);  op.v = dv_;  om_logp(&op);  oe_note(d, &op, dv_); }
  } else {
    op.v = 0;  op.v = om_logp(&op);
    ec_enc_bit_logp(d, op.v, logp);  d->ostorage = d->storage;
  }
  return op.v;
}

int ec_dec_icdf(ec_dec * d, const unsigned char * icdf, unsigned ftb, int site) {
  oprec op;
  if (orec_mode == OREC_OFF) return raw_dec_icdf(d, icdf, ftb);
  op.kind = OP_ICDF;  op.site = site;
  op.pdf = icdf;  op.ftb = ftb;  op.nsym = 0;  op.aux = 0;
  if (orec_mode == OREC_ANALYZE) {
    { opus_int32 dv_ = raw_dec_icdf(d, icdf, ftb);  op.v = dv_;  om_icdf(&op);
      oe_note(d, &op, dv_); }
  } else {
    op.v = 0;  op.v = om_icdf(&op);
    ec_enc_icdf(d, op.v, icdf, ftb);  d->ostorage = d->storage;
  }
  return op.v;
}

int ec_dec_icdf16(ec_dec * d, const opus_uint16 * icdf, unsigned ftb, int site) {
  oprec op;
  if (orec_mode == OREC_OFF) return raw_dec_icdf16(d, icdf, ftb);
  op.kind = OP_ICDF16;  op.site = site;
  op.pdf = icdf;  op.ftb = ftb;  op.nsym = 0;  op.aux = 0;
  if (orec_mode == OREC_ANALYZE) {
    { opus_int32 dv_ = raw_dec_icdf16(d, icdf, ftb);  op.v = dv_;  om_icdf(&op);
      oe_note(d, &op, dv_); }
  } else {
    op.v = 0;  op.v = om_icdf(&op);
    ec_enc_icdf16(d, op.v, icdf, ftb);  d->ostorage = d->storage;
  }
  return op.v;
}

opus_uint32 ec_dec_uint(ec_dec * d, opus_uint32 ft, int site) {
  oprec op;
  if (orec_mode == OREC_OFF) return raw_dec_uint(d, ft);
  op.kind = OP_UINT;  op.site = site;
  op.pdf = NULL;  op.ftb = 0;  op.nsym = ft;  op.aux = 0;
  if (orec_mode == OREC_ANALYZE) {
    { opus_int32 dv_ = (opus_int32) raw_dec_uint(d, ft);  op.v = dv_;  om_uint(&op);  oe_note(d, &op, dv_); }
  } else {
    op.v = 0;  op.v = om_uint(&op);
    ec_enc_uint(d, (opus_uint32) op.v, ft);  d->ostorage = d->storage;
  }
  return (opus_uint32) op.v;
}

opus_uint32 ec_dec_bits(ec_dec * d, unsigned bits, int site) {
  oprec op;
  if (orec_mode == OREC_OFF) return raw_dec_bits(d, bits);
  op.kind = OP_BITS;  op.site = site;
  op.pdf = NULL;  op.ftb = bits;  op.nsym = 0;  op.aux = 0;
  if (orec_mode == OREC_ANALYZE) {
    { opus_int32 dv_ = (opus_int32) raw_dec_bits(d, bits);  op.v = dv_;  om_bits(&op);  oe_note(d, &op, dv_); }
  } else {
    op.v = 0;  op.v = om_bits(&op);
    ec_enc_bits(d, (opus_uint32) op.v, bits);  d->ostorage = d->storage;
  }
  return (opus_uint32) op.v;
}

int orec_laplace_decode(ec_dec * ec, unsigned fs, int decay) {
  oprec op;
  int v;
  op.kind = OP_LAPLACE;  op.site = OREC_S_CELT_COARSE;
  op.pdf = NULL;  op.ftb = (opus_uint32) decay;  op.nsym = 0;  op.aux = fs;
  if (orec_mode == OREC_ANALYZE) {
    op.v = ec_laplace_decode_raw(ec, fs, decay);
    v = op.v;
    if (!ec_laplace_fits(v, fs, decay)) oe_die("coarse energy value is not representable");
    om_laplace(&op);  oe_note(ec, &op, v);
    return op.v;
  }
  op.v = 0;  op.v = om_laplace(&op);
  v = op.v;
  ec_laplace_encode(ec, &v, fs, decay);
  if (v != op.v) oe_die("coarse energy value is not representable");
  ec->ostorage = ec->storage;
  return op.v;
}

int orec_thst, orec_th1 = 1;

int orec_dec_theta(ec_dec * ec, int qn, int stereo, int N, int B0) {
  oprec op;
  int itheta = 0, mode;
  unsigned fl, fh, ft;
  orec_thst = stereo;
  if (stereo && N > 2) mode = 0;
  else if (B0 > 1 || stereo) mode = 1;
  else mode = 2;
  op.kind = OP_THETA;  op.site = OREC_S_CELT_THETA;
  op.pdf = NULL;  op.ftb = (opus_uint32) mode;
  op.nsym = (opus_uint32) qn + 1;  op.aux = (opus_uint32) N;
  if (mode == 1) {
    if (!orec_th1) return (int) ec_dec_uint(ec, op.nsym, OREC_S_CELT_THETA);
    if (orec_mode == OREC_ANALYZE) {
      { opus_int32 dv_ = (opus_int32) raw_dec_uint(ec, op.nsym);  op.v = dv_;  om_angle(&op);  oe_note(ec, &op, dv_); }
    } else {
      op.v = 0;  op.v = om_angle(&op);
      ec_enc_uint(ec, (opus_uint32) op.v, op.nsym);  ec->ostorage = ec->storage;
    }
    return op.v;
  }
  if (orec_mode == OREC_ANALYZE) {
    if (mode == 0) {
      int p0 = 3, x0 = qn / 2, f = p0 * (x0 + 1) + x0, fs;
      fs = (int) raw_decode(ec, (unsigned) f);
      itheta = fs < (x0 + 1) * p0 ? fs / p0 : x0 + 1 + (fs - (x0 + 1) * p0);
    } else {
      int f = ((qn >> 1) + 1) * ((qn >> 1) + 1), fm;
      fm = (int) raw_decode(ec, (unsigned) f);
      if (fm < ((qn >> 1) * ((qn >> 1) + 1) >> 1)) itheta = (isqrt32(8 * (opus_uint32) fm + 1) - 1) >> 1;
      else itheta = (2 * (qn + 1) - isqrt32(8 * (opus_uint32) (f - fm - 1) + 1)) >> 1;
    }
    theta_interval(mode, qn, itheta, &fl, &fh, &ft);
    raw_dec_update(ec, fl, fh, ft);
    op.v = itheta;  om_angle(&op);
    op.v = itheta;  oe_note(ec, &op, itheta);
    return itheta;
  }
  op.v = 0;  itheta = om_angle(&op);
  theta_interval(mode, qn, itheta, &fl, &fh, &ft);
  ec_encode(ec, fl, fh, ft);
  ec->ostorage = ec->storage;
  return itheta;
}
