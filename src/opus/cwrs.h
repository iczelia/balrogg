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

#ifndef BLR_OPCWRS_H
#define BLR_OPCWRS_H

#include "sys/common.h"

#define CW_N   208
#define CW_W   (CW_N + 3)
#define CW_BAD 0xFFFFFFFFUL
#define CW_U(U, n, k) (U)[(sz) (n) * CW_W + (k)]

static u32 * cw_new(void) {
  u32 * U = xmalloc((sz) CW_W * CW_W * sizeof *U);
  int n, k;
  Fk(CW_W, CW_U(U, 0, k) = k == 0);
  for (n = 1; n < CW_W; n++) {
    CW_U(U, n, 0) = 0;
    for (k = 1; k < CW_W; k++) {
      uint64_t v = (uint64_t) CW_U(U, n - 1, k) + CW_U(U, n, k - 1) +
                   CW_U(U, n - 1, k - 1);
      CW_U(U, n, k) = v >= CW_BAD ? CW_BAD : (u32) v;
    }
  }
  return U;
}

static void cw_x2(u32 * x2) {
  int j;
  x2[0] = 1UL << 30;
  for (j = 1; j < 256; j++)
    x2[j] = (u32) (((uint64_t) x2[j - 1] * 1070838486U + (1U << 29)) >> 30);
}

static u32 cw_V(const u32 * U, int n, int k) {
  uint64_t v;
  if (n < 0 || n > CW_N || k < 0 || k + 1 > CW_N + 2) return CW_BAD;
  if (CW_U(U, n, k) == CW_BAD || CW_U(U, n, k + 1) == CW_BAD) return CW_BAD;
  v = (uint64_t) CW_U(U, n, k) + CW_U(U, n, k + 1);
  return v >= CW_BAD ? CW_BAD : (u32) v;
}

static INLINE int cw_step(const u32 * U, int n, int * kp, u32 * ip) {
  u32 p, q, i = *ip;
  int s, k0, k = *kp, val;
  if (k >= n) {
    p = CW_U(U, n, k + 1);
    s = i >= p ? -1 : 0;
    if (s) i -= p;
    k0 = k;
    q = CW_U(U, n, n);
    if (q > i) { k = n;  do p = CW_U(U, n, --k);  while (p > i); }
    else { for (p = CW_U(U, n, k); p > i; p = CW_U(U, n, k)) k--; }
    i -= p;
    val = (k0 - k + s) ^ s;
  } else {
    p = CW_U(U, n, k);
    q = CW_U(U, n, k + 1);
    if (p <= i && i < q) { i -= p;  val = 0; }
    else {
      s = i >= q ? -1 : 0;
      if (s) i -= q;
      k0 = k;
      do p = CW_U(U, n, --k);  while (p > i);
      i -= p;
      val = (k0 - k + s) ^ s;
    }
  }
  *kp = k;  *ip = i;
  return val;
}

static INLINE void cw_two(int k, u32 i, int * y) {
  u32 p = 2 * (u32) k + 1;
  int s = i >= p ? -1 : 0, k0 = k;
  if (s) i -= p;
  k = (int) ((i + 1) >> 1);
  if (k) i -= 2 * (u32) k - 1;
  y[0] = (k0 - k + s) ^ s;
  s = -(int) i;
  y[1] = (k + s) ^ s;
}

static void cw_dec(const u32 * U, int n, int k, u32 i, int * y) {
  int j;
  if (k == 0) { Fj(n, y[j] = 0);  return; }
  if (n == 1) { y[0] = i ? -k : k;  return; }
  while (n > 2) { *y++ = cw_step(U, n, &k, &i);  n--; }
  cw_two(k, i, y);
}

static u32 cw_idx(const u32 * U, int n, const int * y, int a, u32 i, int k) {
  int j;
  for (j = a - 1; j >= 0; j--) {
    i += CW_U(U, k, n - j);
    k += ABS(y[j]);
    if (y[j] < 0) i += CW_U(U, k + 1, n - j);
  }
  return i;
}

static u32 cw_enc(const u32 * U, int n, int k, const int * y) {
  if (k == 0) return 0;
  if (n == 1) return y[0] < 0 ? 1UL : 0UL;
  return cw_idx(U, n, y, n - 1, (u32) (y[n - 1] < 0), ABS(y[n - 1]));
}

#endif
