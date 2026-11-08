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

#ifndef BLR_MAP_H
#define BLR_MAP_H
#include "sys/common.h"

typedef struct { u8 * p;  sz len;  int heap; } blr_map;
extern int blr_no_mmap;

void bm_alloc(blr_map * m, sz n);
void bm_alloc_huge(blr_map * m, sz n);
int bm_file(blr_map * m, void * handle, sz at, sz n, int write);
int bm_flush(blr_map * m);
void bm_free(blr_map * m);

#define RG_MAX ((sz) 4 << 20)
void * rg_take(sz n, int zero);
int rg_owns(const void * p);
sz rg_size(const void * p);
void rg_give(void * p);
int rg_grow(void * p, sz n);
const char * rg_walk(void);
sz rg_inuse(void);
extern void (*rg_watch)(sz live);

#endif
