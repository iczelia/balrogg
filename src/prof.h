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

#ifndef BLR_PROF_H
#define BLR_PROF_H

#include "common.h"

/*  Optional bit attribution and symbol dumps for offline model experiments.
    This compiles away unless BLR_PROFILE is defined.  */
#ifdef BLR_PROFILE

/*  Components, in the order the summary prints them.  */
enum {
  P_RZERO, P_RSIGN, P_RONE, P_RLEN, P_RMANT,   /*  residue digits  */
  P_CLASS,                                     /*  partition classes  */
  P_FZERO, P_FLEN, P_FMAG, P_USED,             /*  floor curve  */
  P_VOTHER,                                    /*  setup, modes, framing  */
  P_PVQ, P_THETA, P_FINE, P_COARSE,            /*  Opus components that dominate the bitrate  */
  P_OICDF, P_OLOGP, P_OUINT, P_OOTHER,
  P_N
};

/*  Record streams, one file each.  */
enum { S_RES, S_CLS, S_FLOOR, S_PVQ, S_THETA, S_FINE, S_COARSE, S_N };

extern int prof_on;
/*  Component tag for the next binary decision.  */
extern int prof_site;

/*  Open the dumps under `dir` (created if absent) and start counting.  */
void prof_open(const char * dir);
/*  Write the summary to stdout and close every dump.  */
void prof_close(void);

/*  Charge one binary decision to the active component.  */
void prof_hook(void * ctx, u32 prob, int bit);
/*  Charge an n-ary step directly: the Opus side codes symbols, not bits.  */
void prof_sym(int comp, u32 fl, u32 fh, u32 ft);

/*  Write one record per extracted symbol.  */
void prof_res(int slot, int q, int pass, int ch, u32 idx, int mem,
              int lastnz, int signhist, int magclass, i32 val);
/*  Residue context shared by extracted records.  */
extern u32 prof_pkt, prof_rcls, prof_rpart, prof_ch;
void prof_cls(int q, int ch, int part, i32 val);
void prof_floor(int fl, int ch, int hist, int post, int prevlen, i32 val);
void prof_pvq(int band, int nb, int kb, u32 N, u32 K, u32 V, u32 index);
void prof_theta(int band, int shape, int prev, u32 qn, u32 N, i32 val);
void prof_fine(int band, int ch, int nbits, int prev, i32 val);
void prof_coarse(int band, int ch, int intra, int lm, i32 pf, i32 pb, i32 val);

#define PROF(x) do { if (prof_on) { x; } } while (0)

#else
#define PROF(x) ((void) 0)
#endif

#endif
