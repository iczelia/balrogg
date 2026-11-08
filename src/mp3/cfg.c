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

enum { MP_NONE, MP_DIRECT, MP_SYM, MP_CHOOSE, MP_Z };

typedef struct { u8 eng, nb, ns, nm;  u32 cls; } mp_cfg;

#define MP_FALLBACK MP_CFG0

static const mp_cfg CFGS[] = {
  [0] = { MP_DIRECT, 0, 0, 0, 0 },
  [5] = { MP_SYM, 0, 0, 0, 0 },
  [40] = { MP_Z, 0, 0, 0, MP3Z_LEVEL0 },
  [41] = { MP_Z, 0, 0, 0, MP3Z_LEVEL4 },
  [42] = { MP_Z, 0, 0, 0, MP3Z_LEVEL5 },
  [43] = { MP_Z, 0, 0, 0, MP3Z_LEVEL6 },
  [44] = { MP_Z, 0, 0, 0, MP3Z_LEVEL7 },
  [45] = { MP_SYM, 0, 0, 0, 1 },
  [46] = { MP_CHOOSE, 45, 5, 2, 0 },
};
#define NCFG ((int) ARRAY_LEN(CFGS))

int mp3_config(int n) {
  return n >= MP_CFG0 && n < MP_CFG0 + NCFG &&
         CFGS[n - MP_CFG0].eng != MP_NONE;
}

static const u8 LEVELS[BLR_NLEV] = {
  5, 46, 44, 40, 40, 40, 40, 40, 40, 41, 41, 41, 41, 42, 42, 42, 43, 43, 43, 43
};
int mp3_level_cfg(int level) {
  return MP_CFG0 + LEVELS[CLAMP(level, 1, BLR_NLEV) - 1];
}
