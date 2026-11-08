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

#ifndef BLR_VBACM_H
#define BLR_VBACM_H

#include "coder/acm.h"
#include "vorbis/vb.h"

typedef struct vba vba;

vba * vba_new(int prof, int zset, int pt);
void vba_free(vba * m);
sz vba_bytes(void);
void vba_bind(vba * m, acm_coder c, void * ctx);
void vba_book_free(void * p);
void vba_res_free(void * p);

u32 vba_field(vba * m, int k, u32 r);
u32 vba_tbits(vba * m, int slot, int d, u32 v);

u32 vba_mode(vba * m, u32 md, u32 nmd);
int vba_window(vba * m, int w, int which, u32 md, int w0);
void vba_frame(vba * m, int bf, u32 n, u32 nch, u32 md);
int vba_floor(vba * m, const vb_setup * s, u32 k, u32 fno, const vb_floor * f,
              u32 * y, u32 * classes, int u);
void vba_couple(vba * m, u32 mag, u32 ang);
void vba_ready(vba * m);
u32 vba_class(vba * m, vb_res * r, vb_book * cb, u32 rno, u32 j, u32 pp,
              u32 k, u32 pre, u32 val, u32 il, int rch, const u8 * cmap);
u32 vba_digit(vba * m, vb_book * b, u32 k, u32 pre, u32 idx, int enc,
              u32 pass, int cls, int ch, u32 c, u32 jp, u32 * nnz, int nbc,
              vb_rule * rp);
void vba_frame_end(vba * m);

#endif
