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

#ifndef BLR_VORBIS_H
#define BLR_VORBIS_H

#include "coder/model.h"
#include "coder/cm.h"
#include "sys/map.h"

#define VB_MAXCLASS 16
#define VB_MAXPART  32
#define VB_MAXPOST  256
#define VB_MAXRCL   64
#define VB_MAXSUB   16
#define VB_MAXCH    256
#define VB_MAXMODE  64
#define VB_MAXFLOOR 4

enum {
  M_CH, M_RATE, M_BRMAX, M_BRNOM, M_BRMIN, M_BLK, M_FRAME,
  M_NBOOK, M_DIM, M_ENT, M_ORDF, M_ORDR, M_LEN, M_VBITS, M_MULT,
  M_NTIME, M_NFLOOR, M_PART, M_PCLS, M_MBOOK, M_SBOOK, M_FX,
  M_NRES, M_RBEG, M_REND, M_RPSZ, M_RNCL, M_CASC, M_RBOOK,
  M_NMAP, M_SUBM, M_CSTEP, M_MAGANG, M_MUX, M_SMT, M_SMF, M_SMR,
  M_NMODE, M_MDMAP, M_FMANT, M_FEXP0, M_FEXP1,
  M_MULTW, M_FCLASS, M_N
};

#define F_ORD     0
#define F_SPARSE  1
#define F_USE0    2
#define F_USEU    3
#define F_USED    4
#define F_LOOK    8
#define F_SEQ     12
#define F_FLTYPE  13
#define F_CDIM    16
#define F_CSUB    24
#define F_FMUL    28
#define F_RANGE   32
#define F_RSTYPE  48
#define F_BLKF    52
#define F_MINSG   53
#define F_DLTSG   54
#define F_NSLOT   56

#define VB_TBANK  8
#define VB_TSIZE  256

#define VB_MBANK  8
#define VB_MSTEP  0x40

enum { VQ_WHOLE, VQ_BOOK, VQ_NEXT, VQ_TAIL, VQ_N };

typedef struct {
  u32 ch, rate;
  u32 bs0, bs1;
} vb_info;

typedef struct {
  u32 dim, ent;
  u8 look;
  u8 * len;
  u32 * code;
  u32 * rcode;
  u32 * fast;
  u32 fbits;
  i32 * nd;
  u32 nv;
  u32 divmul, divshift;
  u32 * mult;
  i16 * dig;
  u32 base, off;
  u32 * inv;
  u32 dm;  int de, ds;
  u32 slot;
  u8 * fz;
  u8 fzm;
  void * acm;
  void * zc;
} vb_book;

typedef struct {
  u32 parts, mult, quant, posts, rng;
  u8 pcls[VB_MAXPART];
  u8 cdim[VB_MAXCLASS], csub[VB_MAXCLASS];
  i32 cbook[VB_MAXCLASS], csb[VB_MAXCLASS][8];
  u32 x[VB_MAXPOST];
  u8 srt[VB_MAXPOST];
  u8 lo[VB_MAXPOST], hi[VB_MAXPOST];
  uint64_t rdx[VB_MAXPOST];
} vb_floor;

static INLINE uint64_t vb_rcp(u32 d) {
  return d ? (((uint64_t) 1 << 40) + d - 1) / d : 0;
}

#define VB_RQ 12

extern const i32 VB_RAT[129];
int64_t vb_bdel(const vb_book * b);
uint64_t vb_gsum(u32 n, u32 dim);
void vb_gfill(uint64_t ** m, u32 * pw, const vb_book * b, u32 n, u32 dim,
              int res);

static INLINE int vb_sq(int v, int lim) { return CLAMP(v, -lim, lim); }

static INLINE int64_t vb_rat(i32 v, int d) {
  return (int64_t) v * VB_RAT[vb_sq(d, 64) + 64] >> 12;
}

static INLINE int64_t vb_clip24(int64_t x) {
  return CLAMP(x, -(1 << 24), 1 << 24);
}

static INLINE uint64_t vb_mass(u32 len) {
  return len ? (uint64_t) 1 << (40 - len) : 0;
}

static INLINE u32 vb_pfrac(uint64_t a, uint64_t b) {
  uint64_t t = a + b;
  int sh = 0;
  while ((t >> sh) >= ((uint64_t) 1 << 40)) sh++;
  return t >> sh ? (u32) ((a >> sh) * 65536 / (t >> sh)) : 32768;
}

static INLINE int vb_unfold(int pr, int v, int range) {
  int hr = range - pr, lr = pr, room = MIN(hr, lr) * 2;
  if (!v) return pr;
  if (v >= room) return hr > lr ? v - lr + pr : pr - v + hr - 1;
  return v & 1 ? pr - (v + 1) / 2 : pr + v / 2;
}

static INLINE int vb_fold(int pr, int t, int range) {
  int hr = range - pr, lr = pr, room = MIN(hr, lr) * 2, dd = t - pr;
  if (!dd) return 0;
  if (dd > 0 && 2 * dd < room) return 2 * dd;
  if (dd < 0 && -2 * dd - 1 < room) return -2 * dd - 1;
  return hr > lr ? t - pr + lr : pr + hr - 1 - t;
}

static INLINE void vb_uncouple(i32 m, i32 a, i32 * l, i32 * r) {
  if (m > 0) {
    if (a > 0) { *l = m;  *r = m - a; }
    else { *r = m;  *l = m + a; }
  } else {
    if (a > 0) { *l = m;  *r = m + a; }
    else { *r = m;  *l = m - a; }
  }
}

typedef struct {
  u32 type, beg, end, psz, ncl, cbook;
  u8 casc[VB_MAXRCL];
  i32 book[VB_MAXRCL][8];
  u32 npass;
  u32 cdmul, cdshift;
  void * acm;
  void * zc;
  int64_t th[VB_MAXRCL][2];
} vb_res;

typedef struct {
  vb_res * r;
  int on, sum;
  u32 cls, n, done, g, il;
  uint64_t jm;
  int64_t a, b;
} vb_rule;

void vb_rule_begin(vb_rule * p, vb_res * r, u32 cls, int last, u32 g, u32 il);
int vb_rule_state(vb_rule * p, int64_t acc, int64_t del, int * fa);
void vb_rule_put(vb_rule * p, int64_t ax, int fa);
void vb_rule_end(vb_rule * p, vb_res * r, u32 cls);

typedef struct {
  u32 sub, nstep;
  u8 mag[VB_MAXCH], ang[VB_MAXCH];
  u8 mux[VB_MAXCH], fl[VB_MAXSUB], rs[VB_MAXSUB];
} vb_map;

typedef struct {
  vb_book * bk;  u32 nbk;
  vb_floor * fl;  u32 nfl;
  vb_res * rs;  u32 nrs;
  vb_map * mp;  u32 nmp;
  u32 nmd;
  u8 blockflag[VB_MAXMODE], mdmap[VB_MAXMODE];
} vb_setup;

#define VB_FZ0     0x01
#define VB_FZNZ    0x02
#define VB_FZP     0x04
#define VB_FZN     0x08
#define VB_FZ1     0x10
#define VB_FZ2     0x20
#define VB_FZALL   0x3F
#define VB_FZMAX   ((sz) 1 << 22)

typedef struct {
  u32 use, ent, nv, cap;
  u32 dm;  int de, ds;
  u8 * len;
} vb_slot;

#define VB_NSLOT   32

enum {
  A_USED,
  A_FZERO,
  A_FLEN,
  A_FMAG,
  A_CLASS,
  A_NGLOB,
  A_RZERO = A_NGLOB,
  A_RSIGN,
  A_RONE,
  A_RLEN,
  A_RMANT,
  A_NTAB
};

#define AR_HIST2   4
#define AR_PLEN    8
#define AR_MAGB    8
#define AR_LOW2    4
#define AR_TRI     28
#define AR_NRES    4
#define AR_NPASS   8
#define AR_NCH     4
#define AR_NPART   1024
#define VB_DIGTAB  8192
#define AR_MAXIDX  0x2000
#define AR_NBIN    (AR_MAXIDX / 4)
#define AR_ILOG    16
#define AR_MCLS    16

#define VB_MEMSZ   ((sz) AR_NRES * AR_NPASS * AR_MAXIDX * AR_NCH)

#define CM_NST     5
#define CM_BITS    18
#define CM_SEL     200
#define VB_TF_HIST 0x04
#define VB_TF_GATE 0x08
#define VB_TF_ZMAG 0x10
#define VB_TF_ZONLY 0x20
#define VB_TF_ACMG 0x40
#define VB_TF_ACM16 0x80
#define CM_NAVG    5

#define VB_TX_PRIOR 0x01
#define VB_TX_DP   0x02
#define VB_TX_APM  0x04
#define VB_TX_SLOTW 0x08
#define VB_TX_ACM  0x10
#define VB_TX_H2   0x20
#define VB_TX_APMS 0x40
#define VB_TX_ACML 0x80

#define VB_T2_ACMZ 0x01
#define VB_T2_PT   0x3E
#define VB_T2_PT1  0x02
#define VB_T2_RULE 0x40

#define VB_T3_ZC   0x01
#define VB_T3_OPT  0xFE
#define VB_T2_ZC   0x3E
#define VB_T2_ZC2  0x80

#define VB_DPN     14
#define VB_DPSZ    ((sz) AR_NRES * VB_DPN * AR_NCH * AR_MAXIDX)
#define VB_DPLIM   15

#define VB_APMC    (2048 + (AR_MAXIDX - 2048) / 4)
#define VB_APM0    ((u32) VB_APMC * AR_NCH)
#define VB_APM1    ((u32) AR_ILOG * AR_MCLS * AR_NCH * 5)

#define VB_GATE_T   655
#define VB_GATE_NB  64
#define VB_GATE_SZ  ((sz) CM_NST * VB_NSLOT * VB_GATE_NB)
#define VB_GATE_MAX 1023
typedef struct { u32 p, n; } vb_gate;

typedef struct { u8 lr, flags, x, x2, x3; } vb_tune;

#ifndef VB_FLIM
#define VB_FLIM     8
#endif
#define VB_NPRESET 22
void vb_tune_preset(vb_tune * t, int k);
#define CM_NLEV    4

#define VB_FPLSZ   ((sz) VB_MAXFLOOR * VB_MAXPOST)

typedef struct {
  model m[M_N];
  u16 f[F_NSLOT];
  u8 fc[F_NSLOT];
  u16 * tailp;
  u8 * tailc;
  u32 pb;
  vb_info i;
  u16 am[VB_MBANK * VB_MSTEP];
  u8 amc[VB_MBANK * VB_MSTEP];
  u16 aw[2][2];
  u8 awc[2][2];
  model apt;
  model apl;
  u8 pm, pw[2];

  vb_setup ** su;  u32 nsu, csu;
  vb_setup * cur;
  vb_slot sl[VB_NSLOT];  u32 nsl;
  u8 st[VB_NSLOT];

  u32 * ar;  sz arn;
  blr_map arena;
  u32 ab[A_NTAB];
  u32 aglob, astep;

  cm cm;
  u8 cm_mask;
  vb_tune t;
  vb_gate * gate;
  i32 nv0[4];
  u32 nidx[4], nrun[4];
  i32 nxv;  int npch, nstarted;
  u8 ai[VB_NSLOT];
  u8 ad[VB_NSLOT];
  u8 * mem;
  u16 * mavg;
  u32 * dp;
  blr_map dpmap;
  u32 * symbols;  sz nsymbols, csymbols;  int symfull;
  u32 * ys;  sz ysn;
  u32 * cs;  sz csn;
  u8 fpl[VB_FPLSZ];
  u8 hu;
  u8 fh[4], sh[4], mh[4];
  u32 psl;
  struct vba * acm;
  struct vbz * zc;
  uint64_t olen;
  u16 vq[VQ_N];  u8 vqc[VQ_N];
  u8 rule;
  vb_rule rp;
  u8 zsh, ish, csh, hsh;
  blr_arena tbl;
} vb_ctx;

static INLINE u32 vb_half(const vb_ctx * v, u32 md) {
  return (v->cur->blockflag[md] ? v->i.bs1 : v->i.bs0) / 2;
}

void vb_init(vb_ctx * v);
void vb_level(vb_ctx * v, int lev);
void vb_tune_set(vb_ctx * v, const vb_tune * t);
void vb_tune_default(vb_tune * t);
int vb_zcopt(const vb_tune * t);
void vb_free(vb_ctx * v);
void vb_link(vb_ctx * v);
void vb_endlink(vb_ctx * v);
void vb_use(vb_ctx * v, u32 n);

void vb_hdr_enc(vb_ctx * v, rc_enc * e, int which, const u8 * pkt, sz len);
void vb_hdr_dec(vb_ctx * v, rc_dec * d, int which, u8 * pkt, sz len);

sz vb_aud_enc(vb_ctx * v, rc_enc * eb, rc_enc * em, rc_enc * ep,
              const u8 * pkt, sz len, int cont, int open);
sz vb_aud_dec(vb_ctx * v, rc_dec * db, rc_dec * dm, rc_dec * dp,
              u8 * pkt, sz len, int cont, sz * open);

#endif
