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

#ifndef BLR_OPACM_H
#define BLR_OPACM_H

#include "coder/acm.h"
#include "opus/ent.h"

typedef struct opa opa;

#define OPA_XIN  2
#define OPA_PRI  4
#define OPA_LITE 8
#define OPA_CAP  32

enum { OPA_NONE, OPA_BIT, OPA_CUM };
typedef struct {
  int kind;
  u32 p1;
  const u32 * cum;
  int n;
  const u32 * (*split)(int dep, int off, int N, int K, int par);
  void (*splitk)(int k);
} opa_pri;

opa * opa_new(int flags);
#define OPA_FS   256
#define OPA_PT(f) ((f) >> 9 & 31)
void opa_free(opa * m);
void opa_bind(opa * m, acm_coder c, void * ctx);
void opa_op(opa * m, oprec * op, int dec, const opa_pri * ap);
void opa_dynest(const i32 * L0, const i32 * L1, int C, int start, int end,
                int LM, u8 * est);
#define OPA_DYNMIN(LM) (25 + 5 * (LM))

#endif
