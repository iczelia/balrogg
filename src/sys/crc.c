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

#include "sys/crc.h"
#include "sys/cpu.h"

#if defined(HAVE_SSE42)
#define CRC_HW      crc32c_sse42
#define CRC_HW_NAME "sse4.2"
#elif defined(HAVE_ARM_CRC32)
#define CRC_HW      crc32c_arm
#define CRC_HW_NAME "armv8-crc"
#endif

static u32 T[8][256];
static int ready;
u32 crc32c_zlong[4][256], crc32c_zshort[4][256];

#define POLY 0x82F63B78u

static u32 mulmodp(u32 a, u32 b) {
  u32 p = 0, m;
  for (m = (u32) 1 << 31; m; m >>= 1) {
    if (a & m) p ^= b;
    b = b >> 1 ^ (POLY & (0u - (b & 1)));
  }
  return p;
}

static u32 xpow(uint64_t n) {
  u32 p = (u32) 1 << 31, s = (u32) 1 << 30;
  for (; n; n >>= 1, s = mulmodp(s, s)) if (n & 1) p = mulmodp(p, s);
  return p;
}

static void zeros(u32 z[4][256], sz len) {
  u32 one[32];
  int i, j, k;
  one[31] = xpow((uint64_t) len * 8);
  for (i = 31; i; i--) one[i - 1] = one[i] >> 1 ^ (POLY & (0u - (one[i] & 1)));
  Fk(4,
    for (z[k][0] = 0, j = 1; j < 256; j++) {
      for (i = 0; !(j >> i & 1); i++);
      z[k][j] = z[k][j & (j - 1)] ^ one[8 * k + i];
    });
}

void crc32c_ready(void) {
  u32 i, j, c;
  if (ready) return;
  Fi(256,
    c = i;
    Fj(8, c = c >> 1 ^ (POLY & (0u - (c & 1))));
    T[0][i] = c);
  Fi(256,
    for (j = 1; j < 8; j++)
      T[j][i] = T[j - 1][i] >> 8 ^ T[0][T[j - 1][i] & 0xFF]);
  zeros(crc32c_zlong, CRC_LONG);
  zeros(crc32c_zshort, CRC_SHORT);
  ready = 1;
}

u32 crc32c_sw(u32 crc, const void * buf, sz n) {
  const u8 * p = buf;
  u32 c = ~crc;
  crc32c_ready();
  for (; n >= 8; n -= 8, p += 8) {
    u32 a = c ^ rd32le(p);
    c = T[7][a & 0xFF] ^ T[6][a >> 8 & 0xFF] ^ T[5][a >> 16 & 0xFF] ^
        T[4][a >> 24] ^ T[3][p[4]] ^ T[2][p[5]] ^ T[1][p[6]] ^ T[0][p[7]];
  }
  while (n--) c = c >> 8 ^ T[0][(c ^ *p++) & 0xFF];
  return ~c;
}

crc32c_fn crc32c_impl(int i, const char ** name) {
  if (!i) { *name = "portable";  return crc32c_sw; }
#ifdef CRC_HW
  if (i == 1 && blr_cpu_crc_present()) { *name = CRC_HW_NAME;  return CRC_HW; }
#endif
  return NULL;
}

static crc32c_fn use;

static void decide(void) {
#ifdef CRC_HW
  if (blr_cpu_crc()) { use = CRC_HW;  return; }
#endif
  use = crc32c_sw;
}

u32 crc32c(u32 crc, const void * p, sz n) {
  if (!use) decide();
  return use(crc, p, n);
}

const char * crc32c_dispatched(void) {
  if (!use) decide();
#ifdef CRC_HW
  if (use == CRC_HW) return CRC_HW_NAME;
#endif
  return "portable";
}
