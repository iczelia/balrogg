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

#ifndef BLR_MAP_H
#define BLR_MAP_H
#include "common.h"

typedef struct { u8 * p;  sz len;  int heap; } blr_map;
extern int blr_no_mmap;

/*  Zeroed anonymous memory, with a heap fallback.  */
void bm_alloc(blr_map * m, sz n);
/*  Map a file window, or return zero to use buffered I/O. NULL is anonymous.  */
int bm_file(blr_map * m, void * handle, sz at, sz n, int write);
int bm_flush(blr_map * m);
void bm_free(blr_map * m);

#endif
