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

#include "mod/mod.h"

static const char FIXED[][5] = {
  "M.K.", "M!K!", "M&K!", "FLT4", "N.T.", "FEST", "LARD", "NSMS",
  "FLT8", "CD81", "OKTA", "OCTA"
};
#define NFIXED ((int) ARRAY_LEN(FIXED))
#define T_CHN  NFIXED
#define T_CH   (T_CHN + 9)
#define T_CN   (T_CH + 90)
#define T_TDZ  (T_CN + 90)
#define NTAG   (T_TDZ + 9)

static const u16 SEMI[MOD_NSEMI - 1] = {
  3979, 3756, 3545, 3346, 3158, 2981, 2813, 2655, 2506, 2366, 2233, 2108,
  1989, 1878, 1772, 1673, 1579, 1490, 1406, 1327, 1253, 1183, 1116, 1054,
  994, 939, 886, 836, 789, 745, 703, 663, 626, 591, 558, 527, 497, 469, 443,
  418, 394, 372, 351, 331, 313, 295, 279, 263, 248, 234, 221, 209, 197, 186,
  175, 165, 156, 147, 139, 131, 124, 117, 110, 104, 98, 93, 87, 82, 78, 73,
  69, 65, 62, 58, 55, 52, 49, 46, 43, 41, 39, 36, 34, 32, 31, 29, 27, 26, 24,
  23, 21, 20, 19, 18, 17, 16
};

static int dig(u8 c) { return c >= '0' && c <= '9' ? c - '0' : -1; }

int mod_tag_index(const u8 * t) {
  int i, a = dig(t[0]), b = dig(t[1]);
  Fi(NFIXED, if (!memcmp(t, FIXED[i], 4)) return i);
  if (a > 0 && !memcmp(t + 1, "CHN", 3)) return T_CHN + a - 1;
  if (a > 0 && b >= 0 && t[2] == 'C' && (t[3] == 'H' || t[3] == 'N'))
    return (t[3] == 'H' ? T_CH : T_CN) + (a - 1) * 10 + b;
  if (!memcmp(t, "TDZ", 3) && dig(t[3]) > 0) return T_TDZ + dig(t[3]) - 1;
  return -1;
}

int mod_tag_count(void) { return NTAG; }

void mod_tag_bytes(int k, u8 * t) {
  if (k < NFIXED) memcpy(t, FIXED[k], 4);
  else if (k < T_CH) {
    t[0] = (u8) ('1' + k - T_CHN);  memcpy(t + 1, "CHN", 3);
  }
  else if (k < T_TDZ) {
    int v = (k - (k < T_CN ? T_CH : T_CN)) + 10;
    t[0] = (u8) ('0' + v / 10);  t[1] = (u8) ('0' + v % 10);
    t[2] = 'C';  t[3] = k < T_CN ? 'H' : 'N';
  } else { memcpy(t, "TDZ", 3);  t[3] = (u8) ('1' + k - T_TDZ); }
}

static int tag_ch_k(int k) {
  if (k < 8) return 4;
  if (k < NFIXED) return 8;
  if (k < T_CH) return k - T_CHN + 1;
  if (k < T_CN) return k - T_CH + 10;
  if (k < T_TDZ) return k - T_CN + 10;
  return k - T_TDZ + 1;
}

int mod_tag_ch(const u8 * t) {
  int k = mod_tag_index(t);
  return k < 0 ? 0 : tag_ch_k(k);
}

static uint64_t stot(const u8 * h, int ns) {
  uint64_t s = 0;
  int i;
  Fi(ns, s += 2 * (uint64_t) rd16be(h + 42 + 30 * i));
  return s;
}

u32 mod_p0(const u8 * h, int ns) {
  const u8 * o = h + 22 + 30 * ns;
  u32 m = 0;
  int i;
  Fi(128, m = MAX(m, o[i]));
  return m + 1;
}

static int stk_ok(const u8 * b, uint64_t size) {
  const u8 * o = b + 472;
  int i;
  if (b[470] < 1 || b[470] > 128) return 0;
  Fi(128, if (o[i] > 63) return 0);
  Fi(15, if (b[44 + 30 * i] > 15 || b[45 + 30 * i] > 64) return 0);
  return stot(b, 15) > 0 && size >= MOD_HDR15 + (uint64_t) mod_p0(b, 15) * 1024;
}

int mod_kind(const u8 * b, sz n, uint64_t size) {
  if (n >= MOD_HDR31 && mod_tag_ch(b + 1080)) return 31;
  if (n >= MOD_HDR15 && stk_ok(b, size)) return 15;
  return 0;
}

u32 mod_pick_np(const u8 * h, int ns, int ch, uint64_t n) {
  uint64_t base = (ns == 31 ? MOD_HDR31 : MOD_HDR15) + stot(h, ns),
      cell = (uint64_t) ch * 4 * MOD_ROWS;
  u32 p0 = mod_p0(h, ns);
  if (n < base || (n - base) % cell || (n - base) / cell > MOD_MAXPAT)
    return p0;
  return (u32) ((n - base) / cell);
}

void mod_layout(mod_lay * L, const u8 * h, int ns, u32 np, sz n) {
  uint64_t o;
  int i;
  memset(L, 0, sizeof *L);
  L->ns = ns;  L->n = n;  L->np = np;  L->p0 = mod_p0(h, ns);
  L->hdr = ns == 31 ? MOD_HDR31 : MOD_HDR15;
  L->tag = ns == 31 ? mod_tag_index(h + 1080) : -1;
  L->ch = L->tag >= 0 ? tag_ch_k(L->tag) : 4;
  L->cell = (sz) L->ch * 4 * MOD_ROWS;
  L->pat = L->hdr;
  o = (uint64_t) L->hdr + (uint64_t) np * L->cell;
  L->plen = (sz) MIN((uint64_t) n - L->hdr, o - L->hdr);
  L->ncell = L->plen / 4;
  Fi(ns,
    uint64_t len = 2 * (uint64_t) rd16be(h + 42 + 30 * i);
    L->sat[i] = (sz) MIN(o, (uint64_t) n);
    L->slen[i] = o < n ? (sz) MIN(len, (uint64_t) n - o) : 0;
    o += len);
  L->smp = L->sat[0];
  L->need = o;
  L->tail = (sz) MIN(o, (uint64_t) n);
  L->tlen = n - L->tail;
}

int mod_semi(u32 p) {
  int lo = 0, hi = MOD_NSEMI - 1;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (p <= SEMI[mid]) lo = mid + 1; else hi = mid;
  }
  return lo;
}

u32 mod_semi_lo(int s) { return s == MOD_NSEMI - 1 ? 1 : (u32) SEMI[s] + 1; }
u32 mod_semi_hi(int s) { return s == 0 ? 4095 : SEMI[s - 1]; }

int mod_field(int ns, const u8 * t, sz at, int * fpos, int * si) {
  sz so = 20 + 30 * (sz) ns, k;
  *si = -1;  *fpos = 0;
  if (at < 20) { *fpos = (int) at;  return MF_TITLE; }
  if (at < so) {
    k = (at - 20) % 30;
    *si = (int) ((at - 20) / 30);
    if (k < 22) { *fpos = (int) k;  return MF_NAME; }
    return MF_LENH + (int) (k - 22);
  }
  if (at == so) return MF_SONG;
  if (at == so + 1) return MF_RST;
  if (at < so + 130) {
    *fpos = (int) (at - so - 2);
    return *fpos < t[so] ? MF_ORD : MF_ORDX;
  }
  *fpos = (int) (at - so - 130);
  return MF_TAG;
}

static u32 llmap(u32 v, u32 len, u32 ls) {
  u32 a = ls <= len ? len - ls : 0;
  if (a > 2 && (v == a || v == 2)) return v == a ? 2 : a;
  return v < 2 ? v ^ 1 : v;
}

void mod_xform(u8 * h, int ns, int inv) {
  u8 * o = h + 22 + 30 * ns;
  int i, mx = -1;
  Fi(ns,
    u8 * s = h + 20 + 30 * i;
    wr16be(s + 28, llmap(rd16be(s + 28), rd16be(s + 22), rd16be(s + 26))));
  Fi(128,
    int v = inv ? (o[i] + mx + 1) & 255 : o[i];
    o[i] = (u8) (inv ? v : (v - mx - 1) & 255);
    mx = MAX(mx, v));
}
