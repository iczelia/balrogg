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

#ifndef BLR_IT_H
#define BLR_IT_H

#include "sys/common.h"

#define IT_HDR    0xC0
#define IT_INS    554
#define IT_SHD    80
#define IT_PATH   8
#define IT_MAXN   ((sz) 1 << 30)
#define IT_MAXROW 1024
#define IT_STC0   700
#define IT_S16C0  800

enum { IK_INS, IK_SHD, IK_PAT, IK_SMP, IK_MSG, IK_N };
enum { IS_NONE, IS_PRI, IS_ZERO, IS_ALIAS, IS_RAW, IS_N };

typedef struct {
  u8 k, st;
  u32 i, v;
  sz at, len;
  u8 * d;
} it_ent;

typedef struct {
  sz n, tab;
  u32 on, inn, sn, pn, ne;
  it_ent * e;
  u32 * ord, np;
  sz clip;
} it_lay;

static INLINE u32 it_eshd(const it_lay * L, u32 i) { return L->inn + i; }
static INLINE u32 it_epat(const it_lay * L, u32 i) {
  return L->inn + L->sn + i;
}
static INLINE u32 it_esmp(const it_lay * L, u32 i) {
  return L->inn + L->sn + L->pn + i;
}
static INLINE u32 it_emsg(const it_lay * L) {
  return L->inn + 2 * L->sn + L->pn;
}

int it_kind(const u8 * b, sz n);
sz it_tab(const u8 * h);
void it_lay_init(it_lay * L, const u8 * h, sz n);
void it_lay_free(it_lay * L);
int it_smp_has(const u8 * s);
uint64_t it_smp_raw(const u8 * s);
uint64_t it_smp_comp(const u8 * b, sz n, sz at, const u8 * s);
void it_parse(it_lay * L, const u8 * b);
void it_offsets(const it_lay * L, u8 * h);

typedef struct { int s8, s16, st; } it_cfg;

int it_cfg_ok(const it_cfg * c);
void it_level(int level, it_cfg * c);
sz it_code(const u8 * b, sz n, const it_cfg * c, u8 ** out);
int it_decode(const u8 * a, sz alen, sz n, int s8, u8 * out);

int it_sniff(const char * path);
int it_is_archive(const char * path);
int it_pack(const char * in, const char * out, const it_cfg * c);
int it_unpack(const char * in, const char * out);

#endif
