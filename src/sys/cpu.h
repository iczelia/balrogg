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

#ifndef BLR_CPU_H
#define BLR_CPU_H

#include "sys/common.h"

int blr_cpu_sse2(void);
int blr_cpu_avx2(void);
int blr_cpu_crc(void);
int blr_cpu_crc_present(void);
int blr_cpu_clmul(void);

int blr_cpu_avx2_forced(void);
const char * blr_simd_built(void);
const char * blr_simd_dispatched(void);

#endif
