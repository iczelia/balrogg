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

#ifndef BLR_ACM_H
#define BLR_ACM_H

#include "coder/squash.h"

#define ACM_NCX   40
#define ACM_NSEL  8
#define ACM_NLSEL 2
#define ACM_NTY   16

typedef uint64_t acm_h;

typedef int (*acm_coder)(void * ctx, int bit, u32 p0);

typedef struct acm acm;

typedef struct {
  acm_h cx[ACM_NCX];
  int ncx;
  acm_h sel[ACM_NSEL];
  acm_h apm[3];
  int ty;
  int xon;
  int xin;
} acm_dec;

void acm_prepare(void);
enum { ACM_VORBIS, ACM_OPUS, ACM_VLITE, ACM_OPUSG, ACM_VORBISG, ACM_MODX };
#define ACM_W16   0x100
#define ACM_APMS  0x200
acm * acm_new(int prof);
enum { ACM_EFULL, ACM_ELITE, ACM_EAPM, ACM_EPRI };
void acm_class(acm * q, int ty, int e, int ncap);
void acm_free(acm * q);
sz acm_bytes(void);
void acm_bind(acm * q, acm_coder c, void * ctx);
acm_dec * acm_d(acm * q);

int acm_bit(acm * q, int bit, u32 p1, acm_h node);
u32 acm_qtree(acm * q, u32 v, const uint64_t * pmf, u32 n, acm_h base,
              int dsel, int mid);
u32 acm_utree(acm * q, u32 v, u32 n, acm_h base);

#if defined(HAVE_AVX2)
int64_t acm_dot_avx2(const i32 * w, const int * x, int n);
void acm_train_avx2(i32 * w, const int * x, int n, int e);
int64_t acm_dot16_avx2(const i16 * w, const i16 * x, int n);
void acm_train16_avx2(i16 * w, const i16 * x, int n, int e);
#endif

#define VBA_LSL 4
#define VBA_NR  12
#if defined(HAVE_AVX2)
void vba_lsacc_avx2(int64_t * a, int64_t * b, int64_t * te, int64_t * tx,
                    const i32 * r, const i32 * x0, const i32 * e);
void vba_lspred_avx2(const int64_t * a, const int64_t * b, i32 * w,
                     const i32 * r, i32 * out);
#endif

static INLINE acm_h acm_hash(acm_h a, acm_h b) {
  acm_h x = (a ^ 0x9E3779B97F4A7C15ULL) + b * 0xD6E8FEB86659FD93ULL;
  x ^= x >> 32;  x *= 0xD6E8FEB86659FD93ULL;  x ^= x >> 29;
  return x;
}
#define PH2(a, b)          acm_hash((acm_h) (a), (acm_h) (b))
#define PH3(a, b, c)       PH2(PH2(a, b), c)
#define PH4(a, b, c, d)    PH2(PH3(a, b, c), d)
#define PH5(a, b, c, d, e) PH2(PH4(a, b, c, d), e)
#define PH6(a, b, c, d, e, f) PH2(PH5(a, b, c, d, e), f)

#endif
