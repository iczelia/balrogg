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

#include "coder/acm.h"
#include <immintrin.h>

static int64_t lo64(__m128i h) {
  int64_t s;
  _mm_storel_epi64((__m128i *) (void *) &s, h);
  return s;
}

int64_t acm_dot_avx2(const i32 * w, const int * x, int n) {
  __m256i a = _mm256_setzero_si256();
  __m128i h;
  int64_t s;
  int i;
  for (i = 0; i + 8 <= n; i += 8) {
    __m256i wv = _mm256_loadu_si256((const __m256i *) (const void *) (w + i));
    __m256i xv = _mm256_loadu_si256((const __m256i *) (const void *) (x + i));
    a = _mm256_add_epi64(a, _mm256_mul_epi32(wv, xv));
    a = _mm256_add_epi64(a, _mm256_mul_epi32(_mm256_srli_epi64(wv, 32),
                                             _mm256_srli_epi64(xv, 32)));
  }
  h = _mm_add_epi64(_mm256_castsi256_si128(a),
                    _mm256_extracti128_si256(a, 1));
  h = _mm_add_epi64(h, _mm_unpackhi_epi64(h, h));
  s = lo64(h);
  for (; i < n; i++) s += (int64_t) w[i] * x[i];
  return s;
}

void acm_train_avx2(i32 * w, const int * x, int n, int e) {
  __m256i ev = _mm256_set1_epi32(e);
  int i;
  for (i = 0; i + 8 <= n; i += 8) {
    __m256i xv = _mm256_loadu_si256((const __m256i *) (const void *) (x + i));
    __m256i lo = _mm256_srli_epi64(_mm256_mul_epi32(xv, ev), 20);
    __m256i hi = _mm256_srli_epi64(
      _mm256_mul_epi32(_mm256_srli_epi64(xv, 32), ev), 20);
    __m256i d = _mm256_blend_epi32(lo, _mm256_slli_epi64(hi, 32), 0xAA);
    __m256i * p = (__m256i *) (void *) (w + i);
    _mm256_storeu_si256(p, _mm256_add_epi32(_mm256_loadu_si256(p), d));
  }
  for (; i < n; i++) w[i] += (i32) (((int64_t) x[i] * e) >> 20);
}

int64_t acm_dot16_avx2(const i16 * w, const i16 * x, int n) {
  __m256i a = _mm256_setzero_si256();
  __m128i h;
  int i;
  for (i = 0; i < n; i += 16)
    a = _mm256_add_epi32(a, _mm256_madd_epi16(
          _mm256_loadu_si256((const __m256i *) (const void *) (w + i)),
          _mm256_loadu_si256((const __m256i *) (const void *) (x + i))));
  a = _mm256_add_epi64(_mm256_cvtepi32_epi64(_mm256_castsi256_si128(a)),
                       _mm256_cvtepi32_epi64(_mm256_extracti128_si256(a, 1)));
  h = _mm_add_epi64(_mm256_castsi256_si128(a),
                    _mm256_extracti128_si256(a, 1));
  h = _mm_add_epi64(h, _mm_unpackhi_epi64(h, h));
  return lo64(h);
}

void acm_train16_avx2(i16 * w, const i16 * x, int n, int e) {
  __m256i ev = _mm256_set1_epi16((short) e);
  int i;
  for (i = 0; i < n; i += 16) {
    __m256i * p = (__m256i *) (void *) (w + i);
    __m256i d = _mm256_mulhrs_epi16(
      _mm256_loadu_si256((const __m256i *) (const void *) (x + i)), ev);
    _mm256_storeu_si256(p, _mm256_adds_epi16(_mm256_loadu_si256(p), d));
  }
}

static __m256i sra5(__m256i x) {
  __m256i m = _mm256_cmpgt_epi64(_mm256_setzero_si256(), x);
  return _mm256_xor_si256(_mm256_srli_epi64(_mm256_xor_si256(x, m), 5), m);
}

static __m256i ld4(const i32 * p) {
  return _mm256_cvtepi32_epi64(
    _mm_loadu_si128((const __m128i *) (const void *) p));
}

void vba_lsacc_avx2(int64_t * a, int64_t * b, int64_t * te, int64_t * tx,
                    const i32 * r, const i32 * x0, const i32 * e) {
  __m256i R[VBA_NR], X = ld4(x0), E = ld4(e), v, * p;
  int i, j;
  Fi(VBA_NR, R[i] = ld4(r + i * VBA_LSL));
  Fi(VBA_NR,
    for (j = 0; j <= i; j++, a += VBA_LSL) {
      p = (__m256i *) (void *) a;  v = _mm256_loadu_si256(p);
      _mm256_storeu_si256(p, _mm256_add_epi64(v,
        _mm256_sub_epi64(_mm256_mul_epi32(R[i], R[j]), sra5(v))));
    });
  for (i = 0; i < VBA_NR; i++, b += VBA_LSL) {
    p = (__m256i *) (void *) b;  v = _mm256_loadu_si256(p);
    _mm256_storeu_si256(p, _mm256_add_epi64(v,
      _mm256_sub_epi64(_mm256_mul_epi32(R[i], X), sra5(v))));
  }
  p = (__m256i *) (void *) te;  v = _mm256_loadu_si256(p);
  _mm256_storeu_si256(p, _mm256_add_epi64(v,
    _mm256_sub_epi64(_mm256_mul_epi32(E, E),
                     sra5(_mm256_add_epi64(v, _mm256_add_epi64(v, v))))));
  p = (__m256i *) (void *) tx;  v = _mm256_loadu_si256(p);
  _mm256_storeu_si256(p, _mm256_add_epi64(v,
    _mm256_sub_epi64(_mm256_mul_epi32(X, X),
                     sra5(_mm256_add_epi64(v, _mm256_add_epi64(v, v))))));
}

static __m256i srav(__m256i x, __m256i n) {
  __m256i m = _mm256_cmpgt_epi64(_mm256_setzero_si256(), x);
  return _mm256_xor_si256(_mm256_srlv_epi64(_mm256_xor_si256(x, m), n), m);
}

void vba_lspred_avx2(const int64_t * a, const int64_t * b, i32 * w,
                     const i32 * r, i32 * out) {
  __m256i H[VBA_NR][VBA_NR], L[VBA_NR][VBA_NR], D[VBA_NR], B[VBA_NR],
          W[VBA_NR], lam, shv, lo18 = _mm256_set1_epi64x(0x3FFFF);
  int64_t tr[VBA_LSL], md[VBA_LSL], sh[VBA_LSL], nv[VBA_LSL], dv[VBA_LSL],
          ov[VBA_LSL];
  int i, j, k, on = 0;
  Fj(VBA_LSL,
    tr[j] = md[j] = 0;
    Fi(VBA_NR,
      int64_t v = a[i * (i + 3) / 2 * VBA_LSL + j];
      tr[j] += v;  if (v > md[j]) md[j] = v);
    Fi(VBA_NR,
      int64_t v = b[i * VBA_LSL + j];
      v = v < 0 ? -v : v;  if (v > md[j]) md[j] = v);
    sh[j] = 0;
    while ((md[j] >> sh[j]) >= ((int64_t) 1 << 36)) sh[j]++;
    if (tr[j] > 0) on |= 1 << j; else tr[j] = sh[j] = 0;
    out[j] = 0);
  if (!on) return;
  shv = _mm256_loadu_si256((const __m256i *) (const void *) sh);
  lam = _mm256_srli_epi64(
    srav(_mm256_loadu_si256((const __m256i *) (const void *) tr), shv), 10);
  k = 0;
  Fi(VBA_NR,
    for (j = 0; j <= i; j++, k++) {
      __m256i v = srav(_mm256_loadu_si256(
        (const __m256i *) (const void *) (a + k * VBA_LSL)), shv);
      H[i][j] = H[j][i] = srav(v, _mm256_set1_epi64x(18));
      L[i][j] = L[j][i] = _mm256_and_si256(v, lo18);
      if (i == j) D[i] = v;
    });
  Fi(VBA_NR,
    B[i] = _mm256_slli_epi64(srav(_mm256_loadu_si256(
      (const __m256i *) (const void *) (b + i * VBA_LSL)), shv), 14);
    W[i] = ld4(w + i * VBA_LSL));
  Fk(2,
    Fi(VBA_NR,
      __m256i hs = _mm256_setzero_si256(), ls = _mm256_setzero_si256(), num;
      Fj(VBA_NR,
        if (j == i) continue;
        hs = _mm256_add_epi64(hs, _mm256_mul_epi32(H[i][j], W[j]));
        ls = _mm256_add_epi64(ls, _mm256_mul_epi32(L[i][j], W[j])));
      num = _mm256_sub_epi64(_mm256_sub_epi64(B[i], _mm256_slli_epi64(hs, 18)), ls);
      _mm256_storeu_si256((__m256i *) (void *) nv, num);
      _mm256_storeu_si256((__m256i *) (void *) dv,
                          _mm256_add_epi64(D[i], lam));
      Fj(VBA_LSL,
        int64_t q = dv[j] > 0 ? nv[j] / dv[j] : 0;
        nv[j] = q > 131072 ? 131072 : q < -131072 ? -131072 : q);
      W[i] = _mm256_loadu_si256((const __m256i *) (const void *) nv)));
  { __m256i o = _mm256_setzero_si256();
    Fi(VBA_NR,
      o = _mm256_add_epi64(o, _mm256_mul_epi32(ld4(r + i * VBA_LSL), W[i])));
    _mm256_storeu_si256((__m256i *) (void *) ov,
                        srav(o, _mm256_set1_epi64x(14))); }
  Fi(VBA_NR,
    _mm256_storeu_si256((__m256i *) (void *) nv, W[i]);
    Fj(VBA_LSL, if (on >> j & 1) w[i * VBA_LSL + j] = (i32) nv[j]));
  Fj(VBA_LSL,
    if (on >> j & 1) {
      int64_t mx = 0;
      Fi(VBA_NR,
        int64_t v = r[i * VBA_LSL + j];
        v = v < 0 ? -v : v;  if (v > mx) mx = v);
      out[j] = (i32) (ov[j] > 3 * mx ? 3 * mx
                      : ov[j] < -3 * mx ? -3 * mx : ov[j]);
    });
}
