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

#ifndef BLR_VORBIS_H
#define BLR_VORBIS_H

#include "model.h"
#include "cm.h"

#define VB_MAXCLASS 16            /*  floor 1 class numbers are 4 bits  */
#define VB_MAXPART  32            /*  floor 1 partition count is 5 bits  */
#define VB_MAXPOST  256           /*  at most 2 + 31*8 = 250 posts, rounded up  */
#define VB_MAXRCL   64            /*  residue classifications are 6 bits + 1  */
#define VB_MAXSUB   16
#define VB_MAXCH    256
#define VB_MAXMODE  64
#define VB_MAXFLOOR 4             /*  floors given their own model tables  */

/*  Model blocks in first-use order.  */
enum {
  M_CH, M_RATE, M_BRMAX, M_BRNOM, M_BRMIN, M_BLK, M_FRAME,
  M_NBOOK, M_DIM, M_ENT, M_ORDF, M_ORDR, M_LEN, M_VBITS, M_MULT,
  M_NTIME, M_NFLOOR, M_PART, M_PCLS, M_MBOOK, M_SBOOK, M_FX,
  M_NRES, M_RBEG, M_REND, M_RPSZ, M_RNCL, M_CASC, M_RBOOK,
  M_NMAP, M_SUBM, M_CSTEP, M_MAGANG, M_MUX, M_SMT, M_SMF, M_SMR,
  M_NMODE, M_MDMAP, M_FMANT, M_FEXP0, M_FEXP1,
  /*  Alternate model for multiplicands wider than their lookup table.  */
  M_MULTW, M_FCLASS, M_N
};

/*  Bit-tree slots.  Each name is the tree's root; a tree of depth d owns
    1 << d slots, of which 1 .. (1 << d) - 1 are nodes.  */
#define F_ORD     0               /*  codebook: lengths are ordered  */
#define F_SPARSE  1               /*  codebook: length list is sparse  */
#define F_USE0    2               /*  sparse: entry 0 is used  */
#define F_USEU    3               /*  sparse: entry n used, n-1 was not  */
#define F_USED    4               /*  sparse: entry n used, n-1 was too  */
#define F_LOOK    8               /*  codebook lookup type, depth 2  */
#define F_SEQ     12              /*  lookup: sequence_p  */
#define F_FLTYPE  13              /*  floor type  */
#define F_CDIM    16              /*  floor 1 class dimension, depth 3  */
#define F_CSUB    24              /*  floor 1 class subclasses, depth 2  */
#define F_FMUL    28              /*  floor 1 multiplier, depth 2  */
#define F_RANGE   32              /*  floor 1 rangebits, depth 4  */
#define F_RSTYPE  48              /*  residue type, depth 2  */
#define F_BLKF    52              /*  mode block flag  */
#define F_MINSG   53              /*  lookup: sign of the minimum  */
#define F_DLTSG   54              /*  lookup: sign of the delta  */
#define F_NSLOT   56

#define VB_CBANK  8               /*  comment byte tree: banks ...  */
#define VB_CSIZE  256             /*  ... of 256 nodes each  */

/*  Audio packets use separate type, mode, and payload streams.  */

#define VB_MBANK  8               /*  mode tree: 3 bits of context ...  */
#define VB_MSTEP  0x40            /*  ... times one tree of up to 64 modes  */

/*  Identification fields needed by later layers.  */
typedef struct {
  u32 ch, rate;
  u32 bs0, bs1;
} vb_info;


typedef struct {
  u32 dim, ent;
  u8 look;                    /*  lookup type: 0 none, 1 the VQ grid  */
  u8 * len;                   /*  codeword length per entry, 0 = unused  */
  u32 * code;                 /*  canonical codeword, most significant first  */
  i32 * fast;                /* lazily built 8-bit prefix lookup */
  u8 * fastbits;
  i32 * nd;                   /*  decode tree: two children per node, a
                                  negative child is -(entry + 1)  */
  u32 nv;                     /*  multiplicands, i.e. digits per dimension  */
  u32 divmul, divshift;        /*  exact division of 24-bit entry indices  */
  u32 * mult;                 /*  the multiplicand VALUES  */
  u32 base, off;              /*  max multiplicand + 1, and half of it  */
  /*  Multiplicand value to mult[] index, `base` entries wide.  */
  u32 * inv;
  /*  Exact normalized lookup delta used in model-slot keys.  */
  u32 dm;  int de, ds;
  u32 slot;                   /*  the model slot this book borrows  */
} vb_book;

typedef struct {
  u32 parts, mult, quant, posts, rng;
  u8 pcls[VB_MAXPART];
  u8 cdim[VB_MAXCLASS], csub[VB_MAXCLASS];
  i32 cbook[VB_MAXCLASS], csb[VB_MAXCLASS][8];
  u32 x[VB_MAXPOST];
  u8 srt[VB_MAXPOST];         /*  post numbers in ascending X order  */
} vb_floor;

typedef struct {
  u32 type, beg, end, psz, ncl, cbook;
  u8 casc[VB_MAXRCL];
  i32 book[VB_MAXRCL][8];
} vb_res;

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

/*  Codebooks share model slots chosen by structural similarity.  */

typedef struct {
  u32 use, ent, nv, cap;
  u32 dm;  int de, ds;
  u8 * len;
} vb_slot;

#define VB_NSLOT   32         /*  the default pool, for the flags byte 0x09  */
#define VB_MAXSLOT 128        /*  the largest pool vb_slots accepts  */

/*  Audio model tables share one mixed-radix arena.  */

enum {
  A_USED,                 /*  floor: this channel carries a curve  */
  A_FZERO,                /*  floor: this post's value is zero  */
  A_FLEN,                 /*  floor: how many bits the value needs  */
  A_FMAG,                 /*  floor: the value's bits  */
  A_CLASS,                /*  residue: the partition classification tree  */
  A_NGLOB,
  A_RZERO = A_NGLOB,      /*  residue digit: the digit is zero  */
  A_RSIGN,                /*  ... its sign  */
  A_RONE,                 /*  ... its magnitude is one  */
  A_RLEN,                 /*  ... otherwise, how many bits it needs  */
  A_RMANT,                /*  ... and those bits  */
  A_NTAB
};

/*  Context-axis bounds enforced by the format or caller.  */
#define AR_HIST2   4          /*  a two-bit history  */
#define AR_PLEN    8          /*  previous coded length at a post, 0..7  */
#define AR_MAGB    8          /*  magnitude bit count, 0..7  */
#define AR_LOW2    4          /*  the two magnitude bits already coded  */
#define AR_TRI     28         /*  (bits, position) pairs, 1 <= bits <= 7  */
#define AR_NRES    4          /*  residue number, low two bits  */
#define AR_NPASS   8          /*  residue passes  */
#define AR_NCH     4          /*  channel within the vector, clamped  */
#define AR_NPART   1024       /*  partitions of one residue  */
#define AR_MAXIDX  0x2000     /*  residue index, i.e. half the largest block  */
#define AR_NBIN    (AR_MAXIDX / 4)   /*  ... banked four indices to a bin  */
#define AR_ILOG    16         /*  ilog of the residue index  */
#define AR_MCLS    16         /*  last magnitude: 0, 1, or 2 + its bit count,
                                  so ten values, rounded up to a shift  */
#define AR_PCLS    8          /*  the previous packet's class at a partition,
                                  bucketed: none, 0, 1, 2-3, 4-7, 8+  */

/*  Cross-pass residue memory indexed by residue, pass, value, and channel.  */
#define VB_MEMSZ   ((sz) AR_NRES * AR_NPASS * AR_MAXIDX * AR_NCH)

/*  Previous-packet classes indexed by residue, channel, and partition.  */
#define VB_CLSMSZ  ((sz) AR_NRES * AR_NCH * AR_NPART)

/*  Five stages: zero flag, sign, magnitude == 1, bit length, mantissa.  */
#define CM_NST     5
#define CM_BITS    18
#define CM_SEL     200           /*  weight rows: 5 previous-digit classes x 5
                                     cross-channel classes x 2 channel parities x
                                     4 memory bits  */
#define VB_TF_CLS  0x01
#define VB_TF_MATCH 0x02          /*  the residue digit match model  */

/*  Per-file adaptation cap, CM rate, and feature flags.  */
typedef struct { u8 alim, lr, flags; } vb_tune;

#define VB_TUNE_LEN 3
/*  CM stages enabled at each effort level.  */
#define CM_NLEV    8
extern const u8 CM_LEVMASK[CM_NLEV];

/*  Previous Floor 1 length per post, shared across channels.  */
#define VB_FPLSZ   ((sz) VB_MAXFLOOR * VB_MAXPOST)

#define VB_APBITS 12
#define VB_APSIZE (1U << VB_APBITS)
typedef struct { u16 p[VB_APSIZE];  u8 c[VB_APSIZE]; } vb_apage;

typedef struct {
  model m[M_N];
  u16 f[F_NSLOT];
  u8 fc[F_NSLOT];             /*  ... their counts, for the adaptive coder  */
  u16 * cmt;                  /*  VB_CBANK * VB_CSIZE comment-byte nodes  */
  u8 * cmtc;
  u32 pb;                     /*  last codebook index  */
  vb_info i;
  u16 am[VB_MBANK * VB_MSTEP];  /*  mode tree, one bank per context  */
  u8 amc[VB_MBANK * VB_MSTEP];
  u16 aw[2][2];               /*  the two window flags, two contexts each  */
  u8 awc[2][2];
  model apt;                  /*  the packet type, on the small stream  */
  u8 pm, pw[2];               /*  previous mode number and window flags  */

  vb_setup ** su;  u32 nsu, csu;   /*  every setup, and the current one  */
  vb_setup * cur;
  vb_slot sl[VB_MAXSLOT];  u32 nsl, ns;
  u8 st[VB_MAXSLOT];          /*  which slots this link has touched  */

  vb_apage ** ar;  sz arn;    /*  model pages allocated only when touched  */
  u32 ab[A_NTAB];             /*  each table's base within the arena  */
  u32 aglob, astep;           /*  the shared tables' span, and one slot's  */

  /*  A bit-history model on exact residue context, mixed with the arena
      probability using neighborhood-selected weights.  */
  cm cm;
  u8 cm_mask;                 /*  enabled stages  */
  vb_tune t;                  /*  the searched, transmitted parameters  */
  i32 nv0[4], nv1[4];         /*  the last two digits of each channel  */
  u32 nidx[4], nrun[4];       /*  ... their bin, and the contiguous run  */
  i32 nxv;  int npch, nstarted;   /*  the immediately preceding digit  */
  u8 ai[VB_MAXSLOT];          /*  which slot regions of it are seeded yet  */
  u8 ad[VB_MAXSLOT];          /*  regions ever written  */
  u8 * mem;                   /*  VB_MEMSZ: [residue][pass][index][channel]  */
  u8 * clsm;                  /*  VB_CLSMSZ: the previous packet's class  */
  u32 * symbols;  sz nsymbols, csymbols;  int symfull;
  u32 * ys;  sz ysn;          /*  scratch: floor posts, per channel  */
  u32 * cs;  sz csn;          /*  scratch: residue classifications  */
  u8 fpl[VB_FPLSZ];           /*  floor 1: the previous length per post  */
  u8 hu;                      /*  2-bit history of the floor "used" flag  */
  u8 fh[4], sh[4], mh[4];     /*  per channel: last flag, sign history,
                                  last magnitude's class  */
  u32 psl;                    /*  the slot the last residue value used  */
} vb_ctx;

void vb_init(vb_ctx * v);
/*  Size the codebook pool and audio arena immediately after vb_init.  */
void vb_slots(vb_ctx * v, u32 n);
/*  Effort, 0 to CM_NLEV-1, selects enabled CM stages.  */
void vb_level(vb_ctx * v, int lev);
/*  Set the tune between vb_init and vb_level.  */
void vb_tune_set(vb_ctx * v, const vb_tune * t);
void vb_tune_default(vb_tune * t);
/*  Serialise / parse the blob the archive header carries.  */
void vb_tune_put(const vb_tune * t, u8 * p);
void vb_tune_get(vb_tune * t, const u8 * p, sz n);
void vb_free(vb_ctx * v);
/*  Clear link histories while retaining probabilities and predictors.  */
void vb_link(vb_ctx * v);
/*  Age unused model slots at the end of a link.  */
void vb_endlink(vb_ctx * v);
/*  Reset probabilities and the codebook pool for non-solid mode. Keep parsed
    setups for header references.  */
void vb_reset(vb_ctx * v);
/*  Select the zero-based setup used by repeated header pages.  */
void vb_use(vb_ctx * v, u32 n);

/*  `which` selects identification, comment, or setup as 0, 1, or 2.  */
void vb_hdr_enc(vb_ctx * v, rc_enc * e, int which, const u8 * pkt, sz len);
void vb_hdr_dec(vb_ctx * v, rc_dec * d, int which, u8 * pkt, sz len);

/*  Process one audio packet across bulk, mode, and type streams. Return the
    consumed bits before byte padding.  */
sz vb_aud_enc(vb_ctx * v, rc_enc * eb, rc_enc * em, rc_enc * ep,
              const u8 * pkt, sz len, int cont);
sz vb_aud_dec(vb_ctx * v, rc_dec * db, rc_dec * dm, rc_dec * dp,
              u8 * pkt, sz len, int cont);

#endif
