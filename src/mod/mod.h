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

#ifndef BLR_MOD_H
#define BLR_MOD_H

#include "sys/common.h"

#define MOD_HDR31  1084
#define MOD_HDR15  600
#define MOD_ROWS   64
#define MOD_MAXPAT 1024
#define MOD_MAXN   ((sz) 1 << 30)
#define MOD_NSEMI  97
#define MOD_CFG0   500

typedef struct {
  int ns, ch, tag;
  sz hdr, n, cell;
  u32 np, p0;
  sz pat, plen, ncell;
  sz sat[31], slen[31], smp;
  sz tail, tlen;
  uint64_t need;
} mod_lay;

int mod_tag_ch(const u8 * t);
int mod_tag_index(const u8 * t);
int mod_tag_count(void);
void mod_tag_bytes(int k, u8 * t);
int mod_kind(const u8 * b, sz n, uint64_t size);
u32 mod_p0(const u8 * h, int ns);
u32 mod_pick_np(const u8 * h, int ns, int ch, uint64_t n);
void mod_layout(mod_lay * L, const u8 * h, int ns, u32 np, sz n);
enum {
  MF_TITLE, MF_NAME, MF_LENH, MF_LENL, MF_FT, MF_VOL, MF_LSH, MF_LSL,
  MF_LLH, MF_LLL, MF_SONG, MF_RST, MF_ORD, MF_ORDX, MF_TAG, MF_N
};
int mod_field(int ns, const u8 * t, sz at, int * fpos, int * si);
void mod_xform(u8 * h, int ns, int inv);
int mod_semi(u32 period);
u32 mod_semi_lo(int s);
u32 mod_semi_hi(int s);

sz mod_code(const u8 * b, sz n, int cfg, u8 ** out);
int mod_decode(const u8 * a, sz alen, sz n, int cfg, u8 * out);

int mod_config(int cfg);
int mod_sniff(const char * path);
int mod_is_archive(const char * path);
int mod_level_cfg(int level);
int mod_pack(const char * in, const char * out, int cfg);
int mod_unpack(const char * in, const char * out);

#endif
