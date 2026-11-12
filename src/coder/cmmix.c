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

#if defined(BLR_CM_SSE2) || defined(BLR_CM_AVX2)
#define CM_SIMD 1
#if defined(BLR_CM_AVX2)
#define CM_BIT  cm_bit_avx2
#define CM_PLAIN cm_plain_avx2
#else
#define CM_BIT  cm_bit_sse2
#define CM_PLAIN cm_plain_sse2
#endif
#else
#define CM_SIMD 0
#define CM_BIT  cm_bit_scalar
#define CM_PLAIN cm_plain_scalar
#endif

#include "coder/cmmix.h"

HOT int CM_BIT(cm * restrict c, int st, int sel, u32 h, u32 * restrict p,
               int bit) {
  return mix_bit(c, st, sel, h, p, bit, 0, -1, 0, -1, -1);
}

HOT int CM_PLAIN(cm * restrict c, int st, int sel, u32 h, u32 * restrict p,
                 int bit) {
  return mix_bit(c, st, sel, h, p, bit, 1, -1, 1, 0, -1);
}
