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

typedef struct {
  u32 feat;
  u8 lev, mix, blend, side;
  u8 smax, ext, lam;
  u8 hi, act;
  u8 acm;
  u8 nodyn;
  u8 oz;
} ocfg;

enum { RA_OWN, RA_RULE, RA_COST };
#define RA(lo, mid, high) ((lo) | (mid) << 2 | (high) << 4)

#define F1 0
#define FA 0x03FF
#define FB (FA & ~OF_TH1)
#define FR (FB | OF_RATE | OF_LM0)
#define FS (FR | OF_SPRI)
#define FN (FS | OF_FTRANS)
#define FAS (FA | OF_RATE | OF_SPRI | OF_LM0)
#define FASM (FAS | OF_MIXR)
#define FASC (FASM | OF_SCOARSE)
#define FNC (FN | OF_TH1 | OF_MIXR | OF_SCOARSE)
#define FASL (FASC | OF_SLOGP)
#define FNL (FNC | OF_SLOGP)
#define FASK (FASL | OF_THKP1)
#define FNK (FNL | OF_THKP1)
#define R7 RA(RA_OWN, RA_RULE, RA_RULE)
#define R8 RA(RA_OWN, RA_COST, RA_RULE)
#define R9 RA(RA_COST, RA_COST, RA_RULE)

#define FLAT(f, d) { f, d, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
#define FLATN(f, d) { f, d, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 }
#define TREE(f, m, bl, sd) { f, 7, m, bl, sd, 0, 0, 0, 0, 0, 0, 0, 0 }
#define SIDE(f, d) { f, d, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 }
#define RULE(f, m, sd, smax, ext, lam, hi, act) \
  { f, 7, m, 1, sd, smax, ext, lam, hi, act, 0, 0, 0 }
#define FZ(d) \
  { OF_HEAD | OF_COUNT | OF_TAGS, d, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }
#define FZT(f, m, bl) { f, 7, m, bl, 0, 0, 0, 0, 0, 0, 0, 0, 1 }
#define TREEZ(f, m, bl) { f, 7, m, bl, 1, 0, 0, 0, 0, 0, 0, 0, 2 }
#define FZTL(f) { f, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3 }
#define ACM(f) { FA | (f), 6, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0 }
#define ACMT(f) { FA | OF_ACMG | OF_RATE | OF_SPRI | OF_MIXR | (f), \
                  7, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0 }
#define AP(n) ((u32) (n) * OF_ACMP1)

static const struct { short id;  ocfg c; } OCFGS[] = {
  { 200, FLAT(F1, 0) }, { 245, FLAT(FR, 0) }, { 248, FLAT(FR, 3) },
  { 255, FLAT(FS, 4) }, { 257, FLAT(FS, 6) }, { 258, FLAT(FN, 1) },
  { 260, FLAT(FN, 3) }, { 263, FLAT(FN, 6) },
  { 272, TREE(FS, 0, 0, 0) }, { 287, TREE(FAS, 0, 0, 1) },
  { 288, TREE(FAS, 0, 1, 1) }, { 295, TREE(FASM, 0, 0, 1) },
  { 296, TREE(FASM, 0, 1, 1) }, { 297, TREE(FASM, 1, 1, 1) },
  { 298, TREE(FASM, 2, 1, 1) }, { 299, TREE(FASM, 4, 1, 1) },
  { 301, RULE(FASM, 2, 1, 4, 0, 1, 4, R8) }, { 307, TREE(FASC, 4, 1, 1) },
  { 312, SIDE(FNC, 2) }, { 313, SIDE(FNC, 4) }, { 314, SIDE(FNC, 6) },
  { 315, TREE(FASL, 0, 0, 1) }, { 322, RULE(FASL, 4, 1, 4, 1, 0, 4, R9) },
  { 328, TREE(FASK, 0, 0, 1) }, { 330, TREE(FASK, 1, 1, 1) },
  { 332, TREE(FASK, 4, 1, 1) }, { 333, RULE(FASK, 1, 1, 0, 0, 0, 2, R7) },
  { 335, RULE(FASK, 4, 1, 4, 1, 0, 4, R9) },
  { 336, SIDE(FNK, 0) }, { 339, SIDE(FNK, 6) },
  { 342, ACM(OF_ACML | OF_ACMN) }, { 344, ACMT(OF_ACML | OF_ACMN | OF_ACMQ) },
  { 348, ACMT(OF_ACML | OF_ACMQ) }, { 357, ACMT(OF_ACMX | OF_ACMW) },
  { 358, ACMT(OF_ACMX | OF_ACMW | OF_ACMF) },
  { 361, ACMT(OF_ACMX | OF_ACMW | AP(3)) },
  { 362, ACMT(OF_ACMX | OF_ACMW | AP(4)) },
  { 364, ACM(OF_ACMG | OF_ACMW | AP(3)) }, { 365, FLATN(F1, 0) },
  { 372, FZ(0) }, { 373, FZ(1) }, { 374, FZ(2) }, { 375, FZ(3) },
  { 377, FZ(5) }, { 378, FZ(6) },
  { 380, FZT(FASM, 4, 1) }, { 381, TREEZ(FASM, 0, 0) },
  { 382, TREEZ(FAS, 0, 1) }, { 383, TREEZ(FASM, 1, 1) },
  { 384, TREEZ(FASM, 2, 1) }, { 385, TREEZ(FASM, 4, 1) }, { 386, FZTL(FS) }
};
#define OM_NCFG ((int) ARRAY_LEN(OCFGS))

int opus_nconfig(void) { return OM_NCFG; }

int opus_config_id(int i) { return OCFGS[i].id; }

static const ocfg * om_cfg(int n) {
  int i;
  Fi(OM_NCFG, if (OCFGS[i].id == n) return &OCFGS[i].c);
  return NULL;
}

int opus_config_acm(int n) { const ocfg * c = om_cfg(n);  return c && c->acm; }

static const short OLEV[BLR_NLEV][OC_N] = {
  { 245, 372, 365, 372, 372, 372, 372 },
  { 245, 372, 200, 372, 372, 372, 372 },
  { 258, 372, 200, 373, 372, 372, 372 },
  { 248, 372, 200, 374, 372, 372, 372 },
  { 255, 372, 200, 263, 373, 373, 372 },
  { 257, 372, 258, 272, 374, 374, 295 },
  { 263, 372, 260, 312, 378, 375, 288 },
  { 287, 380, 263, 295, 377, 378, 380 },
  { 295, 380, 336, 315, 386, 378, 330 },
  { 296, 380, 312, 315, 381, 313, 330 },
  { 299, 380, 314, 315, 382, 295, 330 },
  { 307, 380, 339, 297, 383, 328, 330 },
  { 307, 380, 333, 301, 384, 296, 330 },
  { 307, 332, 332, 332, 385, 297, 330 },
  { 307, 332, 335, 332, 332, 298, 332 },
  { 307, 335, 335, 322, 335, 333, 335 },
  { 342, 342, 344, 342, 342, 332, 342 },
  { 342, 348, 344, 344, 342, 335, 348 },
  { 362, 362, 344, 364, 364, 364, 362 },
  { 358, 358, 361, 358, 358, 357, 358 },
};

int opus_level_cfg(int level, int cls) { return OLEV[level - 1][cls]; }
