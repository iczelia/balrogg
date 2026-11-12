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

static INLINE crc_w lanes(crc_w c, const u8 ** pp, sz * pn, sz len,
                          u32 z[4][256]) {
  const u8 * p = *pp;
  sz n = *pn;
  for (; n >= 3 * len; n -= 3 * len, p += 2 * len) {
    crc_w c1 = 0, c2 = 0;
    const u8 * e = p + len;
    do {
      c = step(c, p);
      c1 = step(c1, p + len);
      c2 = step(c2, p + 2 * len);
      p += CRC_W;
    } while (p < e);
    c = crc32c_shift(z, crc32c_shift(z, (u32) c) ^ (u32) c1) ^ (u32) c2;
  }
  *pp = p;  *pn = n;
  return c;
}

u32 CRC_FN(u32 crc, const void * buf, sz n) {
  const u8 * p = buf;
  crc_w c = (u32) ~crc;
  crc32c_ready();
  for (; n && ((uintptr_t) p & (CRC_W - 1)); n--)
    c = CRC_BYTE((u32) c, *p++);
  c = lanes(c, &p, &n, CRC_LONG, crc32c_zlong);
  c = lanes(c, &p, &n, CRC_SHORT, crc32c_zshort);
  for (; n >= CRC_W; n -= CRC_W, p += CRC_W) c = step(c, p);
  for (; n; n--) c = CRC_BYTE((u32) c, *p++);
  return ~(u32) c;
}
