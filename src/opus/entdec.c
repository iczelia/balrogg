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

/*  libopus entropy decoder with public operations routed through the model.
    OREC_OFF retains the upstream behavior.  */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os_support.h"
#include "arch.h"
#include "entdec.h"
#include "entenc.h"
#include "mfrngcod.h"

#include "opusent.h"
#include "celt.h"

/*  celt/mathops.c's integer square root, the one piece of it the band
    angle's triangular PDF needs.  */
static unsigned isqrt32(opus_uint32 _val){
  unsigned b;
  unsigned g;
  int      bshift;
  g=0;
  bshift=(EC_ILOG(_val)-1)>>1;
  b=1U<<bshift;
  do{
    opus_uint32 t;
    t=(((opus_uint32)g<<1)+b)<<bshift;
    if(t<=_val){
      g+=b;
      _val-=t;
    }
    b>>=1;
    bshift--;
  }
  while(bshift>=0);
  return g;
}

int orec_mode = OREC_OFF;

/*  common.h is deliberately not in this translation unit (see opusent.h), so
    FATAL is not either; nothing below is recoverable anyway.  */
static void oe_die(const char * msg) {
  fprintf(stderr, "opus: %s\n", msg);  exit(1);
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
    d->nbits_total += EC_SYM_BITS;  d->rng <<= EC_SYM_BITS;
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
}

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
  d->end_window = w;  d->nend_bits = avail;  d->nbits_total += bits;
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
    d->error = 1;
    return ft;
  }
  ft++;
  s = raw_decode(d, (unsigned) ft);
  raw_dec_update(d, s, s + 1, (unsigned) ft);
  return s;
}

/*  Mirror live contexts so SYNTH can finalize them after libopus returns.
    ANALYZE uses the same encoder to detect changed slack bytes. Redundancy
    frames may create several contexts.  */

#define OE_MAXCTX 8

typedef struct {
  ec_ctx * p;    /*  the context libopus is using  */
  ec_ctx save;   /*  SYNTH: our copy of it, one operation behind at worst  */
  int live;
  /*  ANALYZE: the frame SYNTH will lay down, and the real one.  */
  ec_ctx mir;
  unsigned char * mbuf;
  opus_uint32 mcap;
  const unsigned char * obuf;
  int has;       /*  this context has a frame to compare at all  */
} oe_ctx;

static oe_ctx oc[OE_MAXCTX];
static int oc_n;

static oe_ctx * oc_find(ec_ctx * p) {
  int i;
  for (i = 0; i < oc_n; i++) if (oc[i].p == p) return &oc[i];
  return NULL;
}

static void oc_save(ec_ctx * p) {
  oe_ctx * c = oc_find(p);
  if (c) c->save = *p;
}

/*  Resynchronize mirror storage after redundancy-frame changes.  */
static oe_ctx * oc_mir(ec_ctx * p) {
  oe_ctx * c = oc_find(p);
  if (!c || !c->live || !c->has) return NULL;
  c->mir.storage = p->storage;
  return c;
}

/*  Finalize one context before comparing or storing its bytes.  */
static void oe_close(oe_ctx * c) {
  if (!c->live) return;
  c->live = 0;
  if (orec_mode == OREC_ANALYZE) {
    if (!c->has) return;
    ec_enc_done(&c->mir);
    om_frame((unsigned char *) c->obuf, c->mbuf, c->mir.storage);
  } else {
    ec_enc_done(&c->save);
    if (c->has) om_frame(c->save.buf, NULL, c->save.storage);
  }
}

/*  Reset packet state while retaining scratch buffers.  */
void oe_begin(void) {
  int i;
  for (i = 0; i < OE_MAXCTX; i++) {
    oc[i].p = NULL;  oc[i].live = 0;  oc[i].obuf = NULL;  oc[i].has = 0;
  }
  oc_n = 0;
}

/*  Clear file-level context so one file cannot affect the next.  */
void oe_reset(void) {
  int i;
  orec_band = orec_ch = orec_LM = orec_C = orec_intra = 0;
  orec_pvqN = orec_pvqK = orec_ftb = 0;
  oe_begin();
  for (i = 0; i < OE_MAXCTX; i++) { free(oc[i].mbuf);  oc[i].mbuf = NULL;  oc[i].mcap = 0; }
}

/*  Finalize contexts before the caller resets orec_mode.  */
void oe_end(void) {
  int i;
  for (i = 0; i < oc_n; i++) oe_close(&oc[i]);
}

void ec_dec_init(ec_dec * d, unsigned char * buf, opus_uint32 storage) {
  oe_ctx * c;
  if (orec_mode == OREC_OFF) { raw_dec_init(d, buf, storage);  return; }
  c = oc_find(d);
  if (c) oe_close(c);   /*  the same stack slot, reused for the next frame  */
  else {
    /*  Keep the slot's scratch buffer.  */
    if (oc_n >= OE_MAXCTX) oe_die("too many live entropy contexts in one packet");
    c = &oc[oc_n++];
  }
  c->p = d;  c->live = 1;
  /*  An empty context has no frame to reconcile.  */
  c->has = buf != NULL && storage != 0;
  c->obuf = buf;
  if (orec_mode == OREC_ANALYZE) {
    raw_dec_init(d, buf, storage);
    if (c->has) {
      if (storage > c->mcap) {
        free(c->mbuf);
        c->mbuf = malloc(storage);
        if (!c->mbuf) oe_die("out of memory for the mirror frame");
        c->mcap = storage;
      }
      ec_enc_init(&c->mir, c->mbuf, storage);
    }
  } else {
    ec_enc_init(d, buf, storage);
    c->save = *d;
  }
}

/*  Keep partial range steps raw. Their complete operations are hooked.  */

unsigned ec_decode(ec_dec * d, unsigned ft) { return raw_decode(d, ft); }
unsigned ec_decode_bin(ec_dec * d, unsigned bits) { return raw_decode_bin(d, bits); }
void ec_dec_update(ec_dec * d, unsigned fl, unsigned fh, unsigned ft) {
  raw_dec_update(d, fl, fh, ft);
}

/*  Mirror the value SYNTH will receive.  */
#define OE_MIRROR(d, call) do { oe_ctx * mc_ = oc_mir(d);  if (mc_) call; } while (0)

int ec_dec_bit_logp(ec_dec * d, unsigned logp, int site) {
  oprec op;
  if (orec_mode == OREC_OFF) return raw_dec_bit_logp(d, logp);
  op.kind = OP_LOGP;  op.site = site;
  op.pdf = NULL;  op.ftb = logp;  op.nsym = 2;  op.aux = 0;
  if (orec_mode == OREC_ANALYZE) {
    op.v = raw_dec_bit_logp(d, logp);  om_op(&op);
    OE_MIRROR(d, ec_enc_bit_logp(&mc_->mir, op.v, logp));
  }
  else {
    op.v = 0;  op.v = om_op(&op);
    ec_enc_bit_logp(d, op.v, logp);  oc_save(d);
  }
  return op.v;
}

int ec_dec_icdf(ec_dec * d, const unsigned char * icdf, unsigned ftb, int site) {
  oprec op;
  if (orec_mode == OREC_OFF) return raw_dec_icdf(d, icdf, ftb);
  op.kind = OP_ICDF;  op.site = site;
  op.pdf = icdf;  op.ftb = ftb;  op.nsym = 0;  op.aux = 0;
  if (orec_mode == OREC_ANALYZE) {
    op.v = raw_dec_icdf(d, icdf, ftb);  om_op(&op);
    OE_MIRROR(d, ec_enc_icdf(&mc_->mir, op.v, icdf, ftb));
  }
  else {
    op.v = 0;  op.v = om_op(&op);
    ec_enc_icdf(d, op.v, icdf, ftb);  oc_save(d);
  }
  return op.v;
}

int ec_dec_icdf16(ec_dec * d, const opus_uint16 * icdf, unsigned ftb, int site) {
  oprec op;
  if (orec_mode == OREC_OFF) return raw_dec_icdf16(d, icdf, ftb);
  op.kind = OP_ICDF16;  op.site = site;
  op.pdf = icdf;  op.ftb = ftb;  op.nsym = 0;  op.aux = 0;
  if (orec_mode == OREC_ANALYZE) {
    op.v = raw_dec_icdf16(d, icdf, ftb);  om_op(&op);
    OE_MIRROR(d, ec_enc_icdf16(&mc_->mir, op.v, icdf, ftb));
  }
  else {
    op.v = 0;  op.v = om_op(&op);
    ec_enc_icdf16(d, op.v, icdf, ftb);  oc_save(d);
  }
  return op.v;
}

opus_uint32 ec_dec_uint(ec_dec * d, opus_uint32 ft, int site) {
  oprec op;
  if (orec_mode == OREC_OFF) return raw_dec_uint(d, ft);
  op.kind = OP_UINT;  op.site = site;
  op.pdf = NULL;  op.ftb = 0;  op.nsym = ft;  op.aux = 0;
  if (orec_mode == OREC_ANALYZE) {
    op.v = (opus_int32) raw_dec_uint(d, ft);  om_op(&op);
    OE_MIRROR(d, ec_enc_uint(&mc_->mir, (opus_uint32) op.v, ft));
  } else {
    op.v = 0;  op.v = om_op(&op);
    ec_enc_uint(d, (opus_uint32) op.v, ft);  oc_save(d);
  }
  return (opus_uint32) op.v;
}

opus_uint32 ec_dec_bits(ec_dec * d, unsigned bits, int site) {
  oprec op;
  if (orec_mode == OREC_OFF) return raw_dec_bits(d, bits);
  op.kind = OP_BITS;  op.site = site;
  op.pdf = NULL;  op.ftb = bits;  op.nsym = 0;  op.aux = 0;
  if (orec_mode == OREC_ANALYZE) {
    op.v = (opus_int32) raw_dec_bits(d, bits);  om_op(&op);
    OE_MIRROR(d, ec_enc_bits(&mc_->mir, (opus_uint32) op.v, bits));
  } else {
    op.v = 0;  op.v = om_op(&op);
    ec_enc_bits(d, (opus_uint32) op.v, bits);  oc_save(d);
  }
  return (opus_uint32) op.v;
}


/*  Route a complete Laplace operation through the model.  */
int orec_laplace_decode(ec_dec * ec, unsigned fs, int decay) {
  oprec op;
  int v;
  op.kind = OP_LAPLACE;  op.site = OREC_S_CELT_COARSE;
  op.pdf = NULL;  op.ftb = (opus_uint32) decay;  op.nsym = 0;  op.aux = fs;
  if (orec_mode == OREC_ANALYZE) {
    op.v = ec_laplace_decode_raw(ec, fs, decay);  om_op(&op);
    /*  Mirror through libopus so changed bytes can be stored.  */
    { oe_ctx * mc_ = oc_mir(ec);
      if (mc_) { int mv = op.v;  ec_laplace_encode(&mc_->mir, &mv, fs, decay); } }
    return op.v;
  }
  op.v = 0;  op.v = om_op(&op);
  v = op.v;
  ec_laplace_encode(ec, &v, fs, decay);
  if (v != op.v) oe_die("coarse energy value is not representable");
  oc_save(ec);
  return op.v;
}

/*  Route nonuniform CELT band angles through the model.  */
int orec_dec_theta(ec_dec * ec, int qn, int stereo, int N, int B0) {
  oprec op;
  int itheta = 0, mode;
  if (stereo && N > 2) mode = 0;            /*  step  */
  else if (B0 > 1 || stereo) mode = 1;      /*  uniform  */
  else mode = 2;                            /*  triangular  */
  if (mode == 1) return (int) ec_dec_uint(ec, qn + 1, OREC_S_CELT_THETA);
  op.kind = OP_THETA;  op.site = OREC_S_CELT_THETA;
  op.pdf = NULL;  op.ftb = (opus_uint32) mode;
  op.nsym = (opus_uint32) qn + 1;  op.aux = (opus_uint32) N;
  if (orec_mode == OREC_ANALYZE) {
    if (mode == 0) {
      int p0 = 3, x, x0 = qn / 2, ft = p0 * (x0 + 1) + x0, fs;
      fs = (int) raw_decode(ec, ft);
      if (fs < (x0 + 1) * p0) x = fs / p0; else x = x0 + 1 + (fs - (x0 + 1) * p0);
      raw_dec_update(ec, x <= x0 ? p0 * x : (x - 1 - x0) + (x0 + 1) * p0,
                         x <= x0 ? p0 * (x + 1) : (x - x0) + (x0 + 1) * p0, ft);
      itheta = x;
    } else {
      int fs = 1, ft = ((qn >> 1) + 1) * ((qn >> 1) + 1), fl = 0, fm;
      fm = (int) raw_decode(ec, ft);
      if (fm < ((qn >> 1) * ((qn >> 1) + 1) >> 1)) {
        itheta = (isqrt32(8 * (opus_uint32) fm + 1) - 1) >> 1;
        fs = itheta + 1;  fl = itheta * (itheta + 1) >> 1;
      } else {
        itheta = (2 * (qn + 1) - isqrt32(8 * (opus_uint32) (ft - fm - 1) + 1)) >> 1;
        fs = qn + 1 - itheta;  fl = ft - ((qn + 1 - itheta) * (qn + 2 - itheta) >> 1);
      }
      raw_dec_update(ec, fl, fl + fs, ft);
    }
    op.v = itheta;  om_op(&op);
    /*  Same split SYNTH runs below, against the mirror.  */
    { oe_ctx * mc_ = oc_mir(ec);
      if (mc_) {
        if (mode == 0) {
          int p0 = 3, x = itheta, x0 = qn / 2, ft = p0 * (x0 + 1) + x0;
          ec_encode(&mc_->mir, x <= x0 ? p0 * x : (x - 1 - x0) + (x0 + 1) * p0,
                               x <= x0 ? p0 * (x + 1) : (x - x0) + (x0 + 1) * p0, ft);
        } else {
          int ft = ((qn >> 1) + 1) * ((qn >> 1) + 1), fs, fl;
          fs = itheta <= (qn >> 1) ? itheta + 1 : qn + 1 - itheta;
          fl = itheta <= (qn >> 1) ? itheta * (itheta + 1) >> 1
                                   : ft - ((qn + 1 - itheta) * (qn + 2 - itheta) >> 1);
          ec_encode(&mc_->mir, fl, fl + fs, ft);
        }
      } }
    return itheta;
  }
  op.v = 0;  itheta = om_op(&op);
  if (mode == 0) {
    int p0 = 3, x = itheta, x0 = qn / 2, ft = p0 * (x0 + 1) + x0;
    ec_encode(ec, x <= x0 ? p0 * x : (x - 1 - x0) + (x0 + 1) * p0,
                  x <= x0 ? p0 * (x + 1) : (x - x0) + (x0 + 1) * p0, ft);
  } else {
    int ft = ((qn >> 1) + 1) * ((qn >> 1) + 1), fs, fl;
    fs = itheta <= (qn >> 1) ? itheta + 1 : qn + 1 - itheta;
    fl = itheta <= (qn >> 1) ? itheta * (itheta + 1) >> 1
                             : ft - ((qn + 1 - itheta) * (qn + 2 - itheta) >> 1);
    ec_encode(ec, fl, fl + fs, ft);
  }
  oc_save(ec);
  return itheta;
}
