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

#include "fmt/ogg.h"
#include <smmintrin.h>
#include <wmmintrin.h>

#define FOLD(hi, lo) _mm_set_epi64x((long long) (hi), (long long) (lo))

static INLINE __m128i load(const u8 * p, __m128i rev) {
  return _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *) (const void *) p), rev);
}

static INLINE __m128i fold(__m128i r, __m128i k) {
  return _mm_xor_si128(_mm_clmulepi64_si128(r, k, 0x11),
                       _mm_clmulepi64_si128(r, k, 0x00));
}

sz ogg_fold_clmul(u32 c, const u8 * d, sz n, u8 r[16]) {
  const __m128i rev = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                   13, 14, 15);
  const __m128i k512 = FOLD(0x8833794C, 0xE6228B11),
                k384 = FOLD(0x64BF7A9B, 0x8C3828A8);
  const __m128i k256 = FOLD(0x569700E5, 0x75BE46B7),
                k128 = FOLD(0xC5B9CD4C, 0xE8A45605);
  __m128i a0, a1, a2, a3;
  sz at = 64;
  if (n < 64) return 0;
  a0 = _mm_xor_si128(load(d, rev), _mm_set_epi32((int) c, 0, 0, 0));
  a1 = load(d + 16, rev);  a2 = load(d + 32, rev);  a3 = load(d + 48, rev);
  for (; at + 64 <= n; at += 64) {
    a0 = _mm_xor_si128(fold(a0, k512), load(d + at, rev));
    a1 = _mm_xor_si128(fold(a1, k512), load(d + at + 16, rev));
    a2 = _mm_xor_si128(fold(a2, k512), load(d + at + 32, rev));
    a3 = _mm_xor_si128(fold(a3, k512), load(d + at + 48, rev));
  }
  a0 = _mm_xor_si128(_mm_xor_si128(fold(a0, k384), fold(a1, k256)),
                     _mm_xor_si128(fold(a2, k128), a3));
  for (; at + 16 <= n; at += 16)
    a0 = _mm_xor_si128(fold(a0, k128), load(d + at, rev));
  _mm_storeu_si128((__m128i *) (void *) r, _mm_shuffle_epi8(a0, rev));
  return at;
}
