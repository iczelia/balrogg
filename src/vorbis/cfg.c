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
  short id;  u8 vidx;  signed char pre;  u8 acm, rule;  u16 zc;
} vcfg;

static const vcfg VCFGS[] = {
  { 109, 1, 1, 0, 0, 0 }, { 110, 1, 2, 0, 0, 0 }, { 111, 3, 2, 0, 0, 0 },
  { 112, 3, 3, 0, 0, 0 }, { 113, 3, 4, 0, 0, 0 }, { 126, 0, 10, 0, 0, 0 },
  { 130, 1, 12, 0, 0, 0 }, { 131, 1, 13, 0, 0, 0 }, { 132, 3, 13, 0, 0, 0 },
  { 133, 3, 14, 0, 0, 0 }, { 134, 3, 15, 0, 0, 0 }, { 139, 1, 18, 0, 0, 0 },
  { 141, 3, 19, 0, 0, 0 }, { 142, 3, 20, 0, 0, 0 }, { 154, 1, 21, 0, 0, 0 },
  { 155, 3, 2, 2, 0, 0 }, { 157, 3, 2, 4, 1, 0 }, { 158, 3, 2, 5, 1, 0 },
  { 159, 3, 2, 6, 1, 0 }, { 160, 3, 2, 7, 1, 0 }, { 165, 3, 2, 12, 1, 0 },
  { 171, 0, -1, 0, 0, 1 }, { 172, 0, -1, 0, 0, 2 }, { 173, 0, -1, 0, 0, 10 },
  { 174, 0, -1, 0, 0, 26 }, { 176, 0, -1, 0, 0, 103 }, { 187, 0, -1, 0, 0, 2074 },
  { 191, 1, 1, 0, 0, 4487 }, { 192, 1, 2, 0, 0, 4487 }
};
#define VB_NCFG ((int) ARRAY_LEN(VCFGS))

static const vcfg * cfg_find(int n) {
  int i;
  Fi(VB_NCFG, if (VCFGS[i].id == n) return VCFGS + i);
  return NULL;
}

int vb_nconfig(void) { return VB_NCFG; }

int vb_config(int i) { return i >= 0 && i < VB_NCFG ? VCFGS[i].id : 0; }

int vb_config_acm(int n) {
  const vcfg * c = cfg_find(n);
  return c && c->acm;
}

int vb_config_tune(int n, vb_tune * t) {
  const vcfg * c = cfg_find(n);
  FATAL_UNLESS(c != NULL, "vorbis: no configuration %d", n);
  if (c->pre >= 0) vb_tune_preset(t, c->pre);
  else vb_tune_default(t);
  if (c->acm) {
    t->x |= (u8) (VB_TX_ACM | (c->acm == 2 ? VB_TX_ACML : 0));
    if (c->acm >= 4) t->flags |= VB_TF_ACMG | VB_TF_ACM16;
    if (c->acm == 5) t->x2 |= VB_T2_ACMZ;
    if (c->acm >= 6) t->x2 = (u8) ((c->acm - 5) * VB_T2_PT1);
  }
  if (c->zc) {
    t->x3 = (u8) (VB_T3_ZC | ((c->zc - 1) & 0x7F) << 1);
    t->x2 = (u8) (t->x2 | ((c->zc - 1) >> 6 & VB_T2_ZC) |
                  ((c->zc - 1) >> 5 & VB_T2_ZC2));
  }
  if (c->rule) t->x2 |= VB_T2_RULE;
  return c->vidx;
}

static const short VLEV[BLR_NLEV][VC_N] = {
  { 126, 126, 126, 176, 126, 176 },
  { 154, 154, 154, 154, 154, 154 },
  { 109, 191, 130, 154, 154, 154 },
  { 109, 192, 131, 154, 154, 154 },
  { 109, 192, 131, 130, 130, 131 },
  { 110, 112, 131, 131, 130, 139 },
  { 111, 112, 132, 131, 130, 139 },
  { 112, 112, 132, 131, 131, 139 },
  { 112, 112, 132, 131, 131, 132 },
  { 112, 112, 133, 139, 139, 132 },
  { 112, 141, 141, 139, 139, 132 },
  { 112, 141, 141, 132, 132, 133 },
  { 113, 134, 141, 133, 133, 133 },
  { 171, 172, 134, 133, 133, 141 },
  { 172, 173, 142, 141, 141, 142 },
  { 174, 174, 174, 142, 174, 174 },
  { 187, 187, 187, 155, 187, 187 },
  { 155, 155, 155, 155, 155, 187 },
  { 165, 165, 165, 165, 165, 187 },
  { 158, 160, 157, 159, 159, 157 },
};

int vb_level_cfg(int level, int cls) { return VLEV[level - 1][cls]; }
