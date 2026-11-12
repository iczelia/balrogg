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

#ifndef BLR_CRC_H
#define BLR_CRC_H

#include "sys/common.h"

u32 crc32c(u32 crc, const void * p, sz n);

typedef u32 (* crc32c_fn)(u32 crc, const void * p, sz n);

u32 crc32c_sw(u32 crc, const void * p, sz n);
u32 crc32c_sse42(u32 crc, const void * p, sz n);
u32 crc32c_arm(u32 crc, const void * p, sz n);

#define CRC_LONG  8192
#define CRC_SHORT 256
extern u32 crc32c_zlong[4][256], crc32c_zshort[4][256];
void crc32c_ready(void);

static INLINE u32 crc32c_shift(u32 z[4][256], u32 c) {
  return z[0][c & 0xFF] ^ z[1][c >> 8 & 0xFF] ^ z[2][c >> 16 & 0xFF] ^
         z[3][c >> 24];
}

crc32c_fn crc32c_impl(int i, const char ** name);
const char * crc32c_dispatched(void);

#endif
