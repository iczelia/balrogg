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

#ifndef BLR_ITI_H
#define BLR_ITI_H

#include "mod/mi.h"
#include "it/it.h"
#include "it/itz.h"

int s16_config(int c);
void * s16_new(int c);
void s16_smp(mx * m, void * st, const i16 * src, i16 * dst, sz nv, sz fr);
void s16_free(void * st);

#endif
