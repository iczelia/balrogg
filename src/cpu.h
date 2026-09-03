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

#ifndef BLR_CPU_H
#define BLR_CPU_H

#include "common.h"

/*  Run-time kernel selection.  The SSE2 mixer is compiled on x86 hosts and
    used when CPUID reports SSE2.  BLR_SIMD=scalar forces the portable kernel
    and BLR_SIMD=sse2 refuses to run without the vector one.  */

/*  Whether the SSE2 kernel is built and this CPU can run it.  */
int blr_cpu_sse2(void);

/*  Names for --version: the kernels compiled in and the one dispatched.  */
const char * blr_simd_built(void);
const char * blr_simd_dispatched(void);

#endif
