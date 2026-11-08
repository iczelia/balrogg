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

#ifndef BLR_MP3MODE_H
#define BLR_MP3MODE_H

#include "sys/common.h"

#define MP_CFG0 400
int mp3_config(int n);
int mp3_level_cfg(int level);
int mp3_sniff(const u8 * b, sz n, int eof);
int mp3_is_archive(const char * path);
int mp3_pack(const char * in, const char * out, int cfg);
extern sz mp3_window;
int mp3_unpack(const char * in, const char * out);

typedef struct { const u32 * code;  const u8 * len;  int dim; } mp3_huff;
#define MP3_NTREE 18
extern const mp3_huff mp3_pairs[MP3_NTREE];
extern const mp3_huff mp3_quad;

extern const i32 mp3_bitrate[32], mp3_rate_hz[2][8];
extern const i32 mp3_slen1[16], mp3_slen2[16];
extern const u8 mp3_side_len[4], mp3_lsf_nr[72], mp3_scfsi_band[5];
extern const u8 mp3_short_row[9], mp3_short_width[7][13];
extern const u8 mp3_huff_dims[35][3];

static INLINE u32 mp3_crc8(u32 c, u32 v) {
  int b;
  for (b = 7; b >= 0; b--) {
    u32 x = (c >> 15 ^ v >> b) & 1;
    c = (c << 1) & 0xFFFF;
    if (x) c ^= 0x8005;
  }
  return c;
}

#endif
