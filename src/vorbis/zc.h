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

#ifndef BLR_VBZC_H
#define BLR_VBZC_H

#include "coder/rc.h"
#include "vorbis/vb.h"

typedef struct vbz vbz;

#define VBZ_O_MIX 0x01
#define VBZ_O_NOTP 0x02
#define VBZ_O_NOSSE 0x04
#define VBZ_O_AVG 0x08
#define VBZ_O_BIN 0x10
#define VBZ_O_FAST 0x20
#define VBZ_O_REG 0x40
#define VBZ_O_FL 0x80
#define VBZ_O_LITE 0x100
#define VBZ_O_LS 0x800
#define VBZ_O_QF 0x1000
vbz * vbz_new(int opt, uint64_t len);
void vbz_free(vbz * m);
void vbz_bind(vbz * m, rc_enc * e, rc_dec * d);
void vbz_link(vbz * m);
void vbz_book_free(void * p);
void vbz_res_free(void * p);

void vbz_frame(vbz * m, int bf, u32 n, u32 nch);
int vbz_floor(vbz * m, const vb_setup * s, u32 k, u32 fno, const vb_floor * f,
              u32 * y, u32 * classes, int u, int choices);
void vbz_couple(vbz * m, u32 mag, u32 ang);
void vbz_ready(vbz * m);
u32 vbz_class(vbz * m, vb_res * r, vb_book * cb, u32 rno, u32 j, u32 pp,
              u32 k, u32 pre, u32 val, u32 il, int rch, const u8 * cmap);
u32 vbz_digit(vbz * m, vb_book * b, u32 k, u32 pre, u32 idx, int enc,
              u32 pass, int ch, u32 c);
void vbz_frame_end(vbz * m);
int vbz_fast(vbz * m, vb_book * b);
int vbz_reg(const vbz * m);
int vbz_fl(const vbz * m);
u32 vbz_entry(vbz * m, vb_book * b, u32 e, int enc, u32 pass, u32 c, u32 ch,
              u32 st, u32 il, int jch, const u8 * cmap, int first);

#endif
