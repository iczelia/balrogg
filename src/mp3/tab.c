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

#include "mp3/mp3.h"

const i32 mp3_bitrate[32] = {
  0, 8000, 16000, 24000, 32000, 40000, 48000, 56000, 64000, 80000, 96000,
  112000, 128000, 144000, 160000, 0, 32000, 40000, 48000, 56000, 64000,
  80000, 96000, 112000, 128000, 160000, 192000, 224000, 256000, 320000, 0, 0
};

const i32 mp3_rate_hz[2][8] = {
  { 11025, 12000, 8000, 0, 22050, 24000, 16000, 0 },
  { 0, 0, 0, 0, 44100, 48000, 32000, 0 }
};

const u8 mp3_side_len[4] = { 9, 17, 17, 32 };

const i32 mp3_slen1[16] = { 0, 0, 0, 0, 3, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4 };
const i32 mp3_slen2[16] = { 0, 1, 2, 3, 0, 1, 2, 3, 1, 2, 3, 1, 2, 3, 2, 3 };

const u8 mp3_lsf_nr[72] = {
  6, 5, 5, 5, 6, 5, 7, 3, 11, 10, 0, 0, 7, 7, 7, 0, 6, 6, 6, 3, 8, 8, 5, 0,
  9, 9, 9, 9, 9, 9, 12, 6, 18, 18, 0, 0, 12, 12, 12, 0, 12, 9, 9, 6,
  15, 12, 9, 0, 6, 9, 9, 9, 6, 9, 12, 6, 15, 18, 0, 0, 6, 15, 12, 0,
  6, 12, 9, 6, 6, 18, 9, 0
};

const u8 mp3_scfsi_band[5] = { 0, 6, 11, 16, 21 };

const u8 mp3_short_row[9] = { 0, 1, 2, 3, 4, 5, 5, 5, 6 };
const u8 mp3_short_width[7][13] = {
  { 4, 4, 4, 4, 6, 8, 10, 12, 14, 18, 22, 30, 56 },
  { 4, 4, 4, 4, 6, 6, 10, 12, 14, 16, 20, 26, 66 },
  { 4, 4, 4, 4, 6, 8, 12, 16, 20, 26, 34, 42, 12 },
  { 4, 4, 4, 6, 6, 8, 10, 14, 18, 26, 32, 42, 18 },
  { 4, 4, 4, 6, 8, 10, 12, 14, 18, 24, 32, 44, 12 },
  { 4, 4, 4, 6, 8, 10, 12, 14, 18, 24, 30, 40, 18 },
  { 8, 8, 8, 12, 16, 20, 24, 28, 36, 2, 2, 2, 26 }
};
