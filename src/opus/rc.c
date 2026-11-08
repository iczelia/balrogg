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

#define OM_ENC 1
#define OM_DEC 2

static int om_mode;
static rc_enc * E;
static rc_dec * D;
static opa * om_acm;

static INLINE int om_fbit(u32 p0, int bit) {
  if (om_mode == OM_DEC) return rc_dec_bit_raw(D, p0);
  rc_enc_bit_raw(E, p0, bit);
  return bit;
}

static INLINE u32 orc_div(u32 range, u32 ft) {
  return ft & (ft - 1) ? range / ft : range >> (blr_ilog(ft) - 1);
}

static INLINE void orc_enc_cum(rc_enc * e, u32 fl, u32 fh, u32 ft) {
  u32 r;
  FATAL_IF_HOT(!(fl < fh && fh <= ft && ft <= 1UL << 17))
    ("opus: bad cumulative step %lu..%lu of %lu",
     (unsigned long) fl, (unsigned long) fh, (unsigned long) ft);
  r = orc_div(e->range, ft);
  rc_addlow(e, r * fl);
  e->range = fh < ft ? r * (fh - fl) : e->range - r * fl;
  rc_norm(e);
}

static INLINE void orc_dec_cum_upd(rc_dec * d, u32 e, u32 fl, u32 fh, u32 ft) {
  d->code -= e * fl;
  d->range = fh < ft ? e * (fh - fl) : d->range - e * fl;
}

static INLINE u32 orc_dec_uni(rc_dec * d, u32 ft) {
  u32 s, e;
  rc_dec_norm(d);
  e = orc_div(d->range, ft);
  if (ft == 2) s = d->code >= e;
  else s = MIN(d->code / e, ft - 1);
  orc_dec_cum_upd(d, e, s, s + 1, ft);
  return s;
}

#define OF_SPLIT  0x0001
#define OF_THPRI  0x0002
#define OF_THCTX  0x0004
#define OF_TH1    0x0008
#define OF_COARSE 0x0010
#define OF_FINE   0x0020
#define OF_SIDE   0x0040
#define OF_HEAD   0x0080
#define OF_COUNT  0x0100
#define OF_TAGS   0x0200
#define OF_SPRI   0x0400
#define OF_RATE   0x0800
#define OF_FTRANS 0x1000
#define OF_MIXR   0x2000
#define OF_LM0    0x4000
#define OF_SCOARSE 0x8000
#define OF_SLOGP  0x10000
#define OF_THKP1  0x20000
#define OF_ACMG   0x40000
#define OF_ACMX   0x80000
#define OF_ACMQ   0x100000
#define OF_ACML   0x200000
#define OF_ACMN   0x800000
#define OF_ACMW   0x2000000
#define OF_ACMF   0x4000000
#define OF_ACMP   0xF8000000u
#define OF_ACMP1  0x8000000
static unsigned om_feat;
static int om_nodyn;
#define OM_DYN (!om_nodyn)

static int om_opaf(u32 f) {
  return (f & OF_ACMX ? OPA_XIN : 0) | (f & OF_ACMQ ? OPA_PRI : 0) |
         (f & OF_ACML ? OPA_LITE : 0) | (f & OF_ACMN ? OPA_CAP : 0) |
         (f & OF_ACMF ? OPA_FS : 0) | (int) ((f & OF_ACMP) / OF_ACMP1) << 9;
}

static int om_acm_bit(void * ctx, int bit, u32 p0) {
  (void) ctx;
  return om_fbit(p0, bit);
}
#ifdef BLR_PROFILE
static int om_comp = P_OOTHER;
#endif

static INLINE void p_cum(u32 fl, u32 fh, u32 ft) { orc_enc_cum(E, fl, fh, ft); }

static INLINE int p_sign(int neg) {
  PROF(prof_sym(om_comp, 0, 1, 2));
  if (om_mode == OM_DEC) return (int) orc_dec_uni(D, 2);
  p_cum((u32) neg, (u32) neg + 1, 2);
  return neg;
}

static void p_raw(u32 * v, int nbits) {
  while (nbits > 0) {
    int k = MIN(16, nbits);
    nbits -= k;
    if (om_mode == OM_DEC) *v |= orc_dec_uni(D, 1UL << k) << nbits;
    else { u32 x = *v >> nbits & ((1UL << k) - 1UL);  p_cum(x, x + 1, 1UL << k); }
  }
}

static u32 uni_dec(u32 V) {
  u32 t, v;
  int ftb;
  if (V <= 1) return 0;
  PROF(prof_sym(om_comp, 0, 1, V));
  t = V - 1;
  ftb = (int) blr_ilog(t);
  if (ftb > 8) {
    u32 lo = 0;
    ftb -= 8;
    v = orc_dec_uni(D, (t >> ftb) + 1) << ftb;
    p_raw(&lo, ftb);
    v |= lo;
    return MIN(v, t);
  }
  return orc_dec_uni(D, V);
}

static void uni_enc(u32 v, u32 V) {
  u32 t;
  int ftb;
  if (V <= 1) return;
  PROF(prof_sym(om_comp, 0, 1, V));
  FATAL_IF_HOT(v >= V)
    ("opus: uniform %lu out of range %lu", (unsigned long) v,
     (unsigned long) V);
  t = V - 1;
  ftb = (int) blr_ilog(t);
  if (ftb > 8) {
    u32 lo = v & ((1UL << (ftb - 8)) - 1UL);
    ftb -= 8;
    p_cum(v >> ftb, (v >> ftb) + 1, (t >> ftb) + 1);  p_raw(&lo, ftb);
  } else p_cum(v, v + 1, V);
}

static void p_uniform(u32 * v, u32 V) {
  if (om_mode == OM_DEC) *v = uni_dec(V);
  else { uni_enc(*v, V);  if (V <= 1) *v = 0; }
}
