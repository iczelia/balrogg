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

#ifndef BLR_SQUASH_H
#define BLR_SQUASH_H

#include "sys/common.h"

extern const int cm_sqt[33];
extern short acm_str[4096];

static INLINE int acm_squash(int d) {
  int w;
  if (d > 2047) return 4095;
  if (d < -2047) return 1;
  w = d & 127;  d = (d >> 7) + 16;
  return (cm_sqt[d] * (128 - w) + cm_sqt[d + 1] * w + 64) >> 7;
}

static INLINE int acm_stretch(int p12) { return acm_str[CLAMP(p12, 0, 4095)]; }

#endif
