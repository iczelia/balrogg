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

#define ZI 10
#define ZP 6
#define ZN (ZI + ZP)
#define ZWB 12
#define ZAB 13
#define ZLIM 60
#define ZW0 19660

typedef struct {
  u32 * t, tm;
  i32 * w;
  u32 * wn;
  u16 * a;
  u8 * ai;
  u16 dt[ZLIM + 1], ar[33];
  int n, np, cap, pcap, two;
  u32 h[ZI], pv[ZP], ph[ZP];
} zc;

static INLINE u32 zh(u32 a, u32 b) {
  u32 v = (a ^ (b + 0x7F4A7C15u) * 0x9E3779B1u) * 0x85EBCA6Bu;
  return v ^ v >> 15;
}
#define ZH2(a, b) zh(a, b)
#define ZH3(a, b, c) zh(zh(a, b), c)
#define ZH4(a, b, c, d) zh(zh(zh(a, b), c), d)
#define ZH5(a, b, c, d, e) zh(ZH4(a, b, c, d), e)

static void zc_init(zc * z, int tb, int cap, int pcap, int two) {
  sz i;
  z->tm = ((u32) 1 << tb) - 1;
  z->t = xcalloc((sz) z->tm + 1, sizeof *z->t);
  z->w = xmalloc(((sz) 1 << ZWB) * (ZN + 1) * sizeof *z->w);
  z->wn = xcalloc((sz) 1 << ZWB, sizeof *z->wn);
  z->a = xmalloc(((sz) 33 << ZAB) * sizeof *z->a);
  z->ai = xcalloc((sz) 1 << ZAB, 1);
  Fi(33, z->ar[i] = (u16) (acm_squash(((int) i - 16) * 128) * 16));
  Fi(ZLIM + 1, z->dt[i] = (u16) (65536 * 10 / (10 * i + 16)));
  z->cap = cap;  z->pcap = pcap;  z->two = two;
}

static void zc_free(zc * z) {
  xfree(z->t);  xfree(z->w);  xfree(z->wn);  xfree(z->a);  xfree(z->ai);
}

static INLINE i32 zmix(const i32 * w, const i32 * st, int n) {
  int64_t d = 0;
  int i;
  Fi(n + 1, d += (int64_t) w[i] * st[i]);
  return (i32) CLAMP(d >> 16, -2047, 2047);
}

static INLINE void ztrain(i32 * w, u32 * wn, const i32 * st, int n, int bit,
                          i32 p) {
  i32 e = ((bit << 12) - p) * (1200 + 600000 / (200 + (i32) *wn));
  int i;
  if (*wn < 65535) (*wn)++;
  Fi(n + 1, w[i] += (i32) (((int64_t) st[i] * e) >> 20));
}

static int zbit(mx * m, zc * z, u32 * const * c, int n, u32 ms, u32 as,
                int bit) {
  i32 st[ZN + 1], s, s1, s2 = 0, pa;
  u32 r1 = ms >> (32 - ZWB), r2 = zh(as, 0x55) >> (32 - ZWB),
      ra = as >> (32 - ZAB), p;
  i32 * w = z->w + (sz) r1 * (ZN + 1), * w2 = z->w + (sz) r2 * (ZN + 1);
  u16 * a = z->a + (sz) ra * 33;
  int i, lo, wt, g;
  if (!z->wn[r1]) Fi(ZN + 1, w[i] = ZW0);
  if (z->two && !z->wn[r2]) Fi(ZN + 1, w2[i] = ZW0);
  if (!z->ai[ra]) { memcpy(a, z->ar, sizeof z->ar);  z->ai[ra] = 1; }
  Fi(n, st[i] = acm_str[((*c[i] & 0xFFFF) ^ 0x8000) >> 4]);
  st[n] = 256;
  s = s1 = zmix(w, st, n);
  if (z->two) { s2 = zmix(w2, st, n);  s = (s1 + s2) / 2; }
  lo = (s + 2048) >> 7;  wt = (s + 2048) & 127;
  pa = (a[lo] * (128 - wt) + a[lo + 1] * wt) >> 7;
  p = (u32) (acm_squash(s) * 16 + 3 * pa) >> 2;
  p = CLAMP(p, 32, 65504);
  bit = mx_bit(m, bit, 65536 - p);
  g = bit ? 65535 : 0;
  a[lo] = (u16) (a[lo] + ((g - a[lo]) * (128 - wt) >> 12));
  a[lo + 1] = (u16) (a[lo + 1] + ((g - a[lo + 1]) * wt >> 12));
  ztrain(w, z->wn + r1, st, n, bit, acm_squash(s1));
  if (z->two) ztrain(w2, z->wn + r2, st, n, bit, acm_squash(s2));
  Fi(n,
    u32 v = *c[i], q = (v & 0xFFFF) ^ 0x8000, k = v >> 16;
    q = bit ? q + ((65535 - q) * z->dt[k] >> 16) : q - (q * z->dt[k] >> 16);
    *c[i] = ((q ^ 0x8000) & 0xFFFF) | (k + (k < ZLIM)) << 16);
  return bit;
}

static int zflag(mx * m, zc * z, int bit, u32 ms, u32 as) {
  u32 * c[ZI];
  int i, n = MIN(z->n, z->cap);
  z->np = 0;
  Fi(n, c[i] = z->t + (z->h[i] & z->tm));
  return zbit(m, z, c, n, ms, zh(as, 1), bit);
}

static u32 zsym(mx * m, zc * z, u32 v, int nb, u32 ms, u32 as) {
  u32 * c[ZI + ZP], base[ZI], node = 1, ln = 1;
  int k, i, zn = MIN(z->n, z->cap), zp = MIN(z->np, z->pcap);
  for (k = nb - 1; k >= 0; k--) {
    int dn = nb - 1 - k, bit, na = 0;
    if (!(dn & 3)) {
      Fi(zn, base[i] = zh(z->h[i], node) & z->tm & ~15u);
      ln = 1;
    }
    Fi(zn, c[i] = z->t + base[i] + ln);
    Fi(zp,
      u32 q = z->pv[i] >> (k + 1) | 1u << dn, e = 2;
      if (q == node) { e = z->pv[i] >> k & 1;  na++; }
      c[zn + i] = z->t + (zh(z->ph[i], (u32) dn * 4 + e) & z->tm));
    bit = zbit(m, z, c, zn + zp, zh(ms, (u32) dn * 8 + (u32) MIN(na, 3)),
               zh(as, node), (int) (v >> k & 1));
    node = node * 2 + (u32) bit;  ln = ln * 2 + (u32) bit;
  }
  z->np = 0;
  return node - (1u << nb);
}
static const u8 TPH[192] = {
  73, 77, 80, 77, 83, 111, 101, 32, 32, 32, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 16, 0, 0, 0, 0, 10, 0, 16, 0, 23, 2, 20, 2,
  13, 0, 7, 0, 128, 48, 6, 125, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 228, 32,
  32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
  32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 160, 160, 160, 160, 160,
  160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160,
  160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64
};
static const u8 TPI[554] = {
  73, 77, 80, 73, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 60,
  128, 160, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 64, 0, 0, 64, 100, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0,
  0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
  0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const u8 TPS[80] = {
  73, 77, 80, 83, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64, 0, 64, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 171, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0
};

static const u8 TP3H[96] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 26, 16, 0, 0, 32, 0, 31, 0, 99, 0, 0, 0, 32, 19, 2, 0, 83, 67, 82, 77,
  64, 6, 125, 176, 16, 252, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 1, 9, 2, 10, 3,
  11, 4, 12, 5, 13, 6, 14, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255
};
static const u8 TP3I[80] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 64, 0, 0, 0, 171, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  83, 67, 82, 83
};
static const u8 TP3P[32] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0
};

static const u8 TPXH[XM_HDR] = {
  69, 120, 116, 101, 110, 100, 101, 100, 32, 77, 111, 100, 117, 108, 101, 58,
  32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 26, 70, 97,
  115, 116, 84, 114, 97, 99, 107, 101, 114, 32, 118, 50, 46, 48, 48, 32, 32,
  32, 4, 1, 20, 1, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 1, 0, 6, 0, 125, 0
};
static const u8 TPXI[XM_INS] = {
  7, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 1, 0, 40
};
static const u8 TPXS[XM_SHD] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 128
};

enum {
  RK_HDR, RK_INS, RK_SHD, RK_S3H, RK_S3I, RK_S3P, RK_XH, RK_XI, RK_XS, RK_N
};

typedef struct { u8 tbo, cap, pcap, two; } stct;

static const stct STC[] = {
  [0] = { 2, 3, 2, 0 }, [1] = { 4, ZI, ZP, 1 }, [3] = { 2, 5, 3, 0 },
  [4] = { 3, 6, 4, 0 }
};

#define ST_N ((int) (sizeof STC / sizeof *STC))

typedef struct ist {
  zc z;
  const stct * cf;
  u8 rp[RK_N][IT_INS], rq[RK_N][IT_INS];
} ist;

static const u8 * const TPL[RK_N] = {
  TPH, TPI, TPS, TP3H, TP3I, TP3P, TPXH, TPXI, TPXS
};
static const u16 TPN[RK_N] = {
  IT_HDR, IT_INS, IT_SHD, S3_HDR, S3_INS, 32, XM_HDR, XM_INS, XM_SHD
};

static INLINE u32 tx1(u32 a) { return ZH2(0x501, a); }
static INLINE u32 tx2(u32 a, u32 b) { return ZH3(0x502, a, b); }
static INLINE u32 tx3(u32 a, u32 b, u32 c) { return ZH4(0x503, a, b, c); }

static ist * ist_new(int cfg) {
  ist * s = xmalloc(sizeof *s);
  int k;
  memset(s, 0, sizeof *s);
  s->cf = STC + (cfg - IT_STC0 - 1);
  Fk(RK_N, memcpy(s->rp[k], TPL[k], TPN[k]);
           memcpy(s->rq[k], s->rp[k], IT_INS));
  return s;
}

static void ist_tbn(ix * x, sz n) {
  mx * m = &x->b;
  u32 tb = 0, v = 0, i;
  if (!m->dec)
    tb = (u32) CLAMP((int) blr_ilog((u32) MIN(n, 1u << 30)) +
                     x->z->cf->tbo, 16, 22) - 16;
  Fi(3, v = v << 1 | (u32) mx_bit(m, (int) (tb >> (2 - i) & 1), 32768));
  zc_init(&x->z->z, 16 + (int) MIN(v, 6), x->z->cf->cap, x->z->cf->pcap,
          x->z->cf->two);
}

static void ist_tb(ix * x) {
  const it_lay * L = &x->L;
  sz n = L->n;
  u32 i;
  if (!x->b.dec)
    Fi(L->ne, if (L->e[i].k == IK_SMP && L->e[i].st == IS_PRI)
                n -= MIN(n, L->e[i].len));
  ist_tbn(x, n);
}

static void ist_free(ist * s) {
  if (!s) return;
  zc_free(&s->z);
  xfree(s);
}

static int rskip(int k, const u8 * t, sz j) {
  if (k == RK_HDR) return j >= 0x38 && j < 0x3C && rd16le(t + 0x36);
  if (k == RK_SHD) return j >= 72 && j < 76 && it_smp_has(IT_FIT, t);
  if (k == RK_S3I) return j >= 13 && j < 16 && it_smp_has(IT_FS3M, t);
  return k == RK_XI && j < 4;
}

static int fcls3(int k, sz j, sz * fs, sz * alt) {
  if (k == RK_S3P) {
    if (j) *alt = j - 1;
    return 1;
  }
  if (k == RK_S3H) {
    if (j < 28) { *fs = 0;  return 2; }
    if (j < 44) return 3 + (int) (j - 28);
    if (j < 48) return 19;
    if (j < 64) return 20 + (int) (j - 48);
    if (j != 64) *alt = j - 1;
    return 36;
  }
  if (!j) return 1;
  if (j < 13) { *fs = 1;  return 2; }
  if (j >= 24 && j < 28) *alt = j - 8;
  if (j < 48) return 3 + (int) (j - 13);
  if (j < 76) { *fs = 48;  return 38; }
  return 39;
}

static int fclsx(int k, sz j, sz * fs, sz * alt) {
  if (k == RK_XH) {
    if (j < 17) return 1;
    if (j < 37) { *fs = 17;  return 2; }
    if (j < 38) return 3;
    if (j < 58) { *fs = 38;  return 4; }
    return 5 + (int) (j - 58);
  }
  if (k == RK_XS) {
    if (j < 18) return 1 + (int) j;
    *fs = 18;
    return 19;
  }
  if (j < 26) { *fs = 4;  return 2; }
  if (j < 33) return 3 + (int) (j - 26);
  if (j < 129) {
    if (j > 33) *alt = j - 1;
    return 10;
  }
  if (j < 225) {
    sz o = (j - 129) % 48;
    if (o >= 4) *alt = j - 4;
    return 11 + (int) ((j - 129) / 48 * 4 + o % 4);
  }
  if (j < 241) return 19 + (int) (j - 225);
  if (j > 241) *alt = j - 1;
  return 35;
}

static int fcls(int k, const u8 * t, sz j, sz * fs, sz * alt) {
  *fs = NOC;  *alt = NOC;
  if (k >= RK_XH) return fclsx(k, j, fs, alt);
  if (k >= RK_S3H) return fcls3(k, j, fs, alt);
  if (j < 4) return 1;
  if (k == RK_HDR) {
    if (j < 30) { *fs = 4;  return 2; }
    if (j < 64) return 3 + (int) (j - 30);
    if (j != 64 && j != 128) *alt = j - 1;
    return j < 128 ? 40 : 41;
  }
  if (k == RK_INS) {
    if (j < 16) { *fs = 4;  return 2; }
    if (j < 32) return 3 + (int) (j - 16);
    if (j < 58) { *fs = 32;  return 19; }
    if (j < 64) return 20 + (int) (j - 58);
    if (j < 304) return 26 + (int) (j & 1) + 2 * (j < 66);
    if (j < 550) {
      sz e = (j - 304) / 82, o = (j - 304) % 82, q = (o - 6) / 3;
      int b = 30 + 14 * (int) e;
      if (o < 6) return b + (int) o;
      if (o == 81) return b + 6;
      return b + 7 + (int) ((o - 6) % 3) + 3 * (q >= t[305 + 82 * e]);
    }
    return 80;
  }
  if (j < 16) { *fs = 4;  return 2; }
  if (j < 20) return 3 + (int) (j - 16);
  if (j < 46) { *fs = 20;  return 7; }
  if (j < 48) return 8 + (int) (j - 46);
  if (j >= 56 && j < 60) *alt = j - 8;
  if (j >= 64 && j < 72) *alt = j - 12;
  if (j < 72) return 10 + (int) (j - 48);
  return 34 + (int) (j - 72);
}

static u8 fwd(int k, const u8 * t, sz j, u32 idx) {
  if (k != RK_INS || j < 64 || j >= 304 || j == 64) return t[j];
  if (j == 65) return (u8) (t[65] - idx - 1);
  return (u8) (t[j] - t[j - 2] - !(j & 1));
}

static u8 inv(int k, const u8 * t, sz j, u32 v, u32 idx) {
  if (k != RK_INS || j < 64 || j >= 304 || j == 64) return (u8) v;
  if (j == 65) return (u8) (v + idx + 1);
  return (u8) (v + t[j - 2] + !(j & 1));
}

static void rec(ix * x, int k, u8 * t, sz N, u32 idx) {
  ist * s = x->z;
  zc * z = &s->z;
  mx * m = &x->b;
  u8 * rp = s->rp[k], * rq = s->rq[k], tr[IT_INS];
  u32 run = 0, peq = 1, aeq = 0, wh = 0;
  sz j;
  for (j = 0; j < N; j++) {
    sz fs, alt;
    int cls, eq;
    u32 v, r, p1, p2, p3;
    if (rskip(k, t, j)) { tr[j] = 0;  continue; }
    cls = fcls(k, t, j, &fs, &alt);
    v = m->dec ? 0 : fwd(k, t, j, idx);
    r = rp[j];
    if (fs != NOC) {
      p1 = j > fs ? t[j - 1] : 256;  p2 = j > fs + 1 ? t[j - 2] : 256;
      p3 = j > fs + 2 ? t[j - 3] : 256;
    } else {
      p1 = j ? tr[j - 1] : 256;  p2 = j > 1 ? tr[j - 2] : 256;  p3 = 0;
    }
    z->n = 5;
    z->h[0] = ZH3(0x101, k, j);
    z->h[1] = ZH4(0x102, k, cls, MIN(run, 15));
    z->h[2] = ZH4(0x103, k, j, rp[j] != rq[j]);
    z->h[3] = ZH5(0x104, k, cls, peq, r == 0);
    z->h[4] = fs != NOC ? ZH4(0x105, p1, p2, r) : ZH4(0x105, k, cls, p1 == r);
    eq = zflag(m, z, v == r, ZH3(0x1F0, k, cls), ZH3(0x1F1, k, j));
    if (eq) v = r;
    else {
      int ae = 0;
      if (alt != NOC && tr[alt] != r) {
        z->n = 3;
        z->h[0] = ZH3(0x111, k, j);
        z->h[1] = ZH4(0x112, k, cls, aeq);
        z->h[2] = ZH4(0x113, k, j, rp[alt] == r);
        ae = zflag(m, z, v == tr[alt], ZH3(0x1F2, k, cls), ZH3(0x1F3, k, j));
        if (ae) v = tr[alt];
      }
      aeq = ae;
      if (!ae) {
        z->n = 6;
        z->h[0] = ZH3(0x121, k, j);
        z->h[1] = ZH4(0x122, k, j, r);
        z->h[2] = ZH4(0x123, k, cls, p1);
        if (fs != NOC) {
          z->h[3] = tx2(p1, p2);  z->h[4] = tx3(p1, p2, p3);
          z->h[5] = tx1(p1);
          z->h[6] = ZH5(0x511, p1, p2, p3, j > fs + 3 ? t[j - 4] : 256);
          z->h[7] = ZH2(0x513, wh);
          z->h[8] = ZH3(0x512, r, p1);
          z->n = 9;
        } else {
          z->h[3] = ZH5(0x124, k, j, p1, p2);
          z->h[4] = ZH4(0x125, k, cls, r);  z->h[5] = ZH3(0x126, k, cls);
        }
        v = zsym(m, z, v, 8, ZH3(0x2F0, k, fs != NOC ? 0x100 : cls),
                 ZH3(0x2F1, k, cls));
      }
    }
    run = eq ? run + 1 : 0;  peq = (u32) eq;
    if (fs != NOC) {
      u32 cv = v | 32;
      wh = j == fs ? 0 : wh;
      wh = cv >= 'a' && cv <= 'z' ? zh(wh, cv) : 0;
    }
    tr[j] = (u8) v;
    if (m->dec) t[j] = inv(k, t, j, v, idx);
  }
  memcpy(rq, rp, N);  memcpy(rp, tr, N);
}

static void ords(ix * x, u8 * o, u32 n) {
  zc * z = &x->z->z;
  mx * m = &x->b;
  u32 j, mxv = 0, p1 = 256, p2 = 256;
  for (j = 0; j < n; j++) {
    u32 v = m->dec ? 0 : o[j], a = p1 < 254 ? p1 + 1 : 0, b = mxv + 1;
    int f;
    z->n = 3;
    z->h[0] = ZH2(0x201, p1 >= 254 ? p1 : 0);
    z->h[1] = ZH3(0x202, p1 >= 254 ? p1 : 0, p2 >= 254 ? p2 : p2 + 1 == p1);
    z->h[2] = ZH2(0x203, MIN(j, 2));
    f = zflag(m, z, v == a, ZH2(0x2F2, 0), ZH2(0x2F3, p1 >= 254));
    if (f) v = a;
    else {
      if (b != a) {
        z->h[0] = ZH2(0x211, p1 >= 254 ? p1 : 0);
        z->h[1] = ZH2(0x212, b > 1);
        z->h[2] = ZH2(0x213, 0);
        f = zflag(m, z, v == b, ZH2(0x2F4, 0), ZH2(0x2F5, 0));
        if (f) v = b;
      }
      if (!f) {
        z->n = 4;
        z->h[0] = ZH2(0x221, p1);
        z->h[1] = ZH3(0x222, p1, p2);
        z->h[2] = ZH2(0x223, 0);
        z->h[3] = ZH2(0x224, mxv);
        v = zsym(m, z, v, 8, ZH2(0x2F6, 0), ZH2(0x2F7, 0));
      }
    }
    o[j] = (u8) v;
    if (v < 254) mxv = MAX(mxv, v);
    p2 = p1;  p1 = v;
  }
}

static void text(ix * x, u8 * t, sz n) {
  zc * z = &x->z->z;
  mx * m = &x->b;
  sz j, ls = 0, pls = NOC;
  u32 wh = 0;
  for (j = 0; j < n; j++) {
    u32 p1 = j ? t[j - 1] : 256, p2 = j > 1 ? t[j - 2] : 256,
        p3 = j > 2 ? t[j - 3] : 256, p4 = j > 3 ? t[j - 4] : 256,
        col = (u32) (j - ls), up = 256;
    if (pls != NOC && pls + col < ls) up = t[pls + col];
    z->n = 7;
    z->h[0] = tx1(p1);  z->h[1] = tx2(p1, p2);  z->h[2] = tx3(p1, p2, p3);
    z->h[3] = ZH5(0x511, p1, p2, p3, p4);
    z->h[4] = ZH3(0x512, up, p1);
    z->h[5] = ZH2(0x513, wh);
    z->h[6] = ZH4(0x514, up, pls != NOC && pls + col + 1 < ls
                             ? t[pls + col + 1] : 256, MIN(col, 2));
    t[j] = (u8) zsym(m, z, m->dec ? 0 : t[j], 8, ZH2(0x5F0, 0),
                     ZH2(0x5F1, up));
    if (t[j] == 13 || t[j] == 10) { pls = ls;  ls = j + 1; }
    wh = (t[j] | 32) >= 'a' && (t[j] | 32) <= 'z' ? zh(wh, t[j] | 32) : 0;
  }
}

static int st_ok(int c) {
  return c > IT_STC0 && c <= IT_STC0 + ST_N && STC[c - IT_STC0 - 1].tbo;
}

static void midi0(u8 * t) {
  static const char * const g[] = { "FF", "FC", "", "9c n v", "9c n 0", "",
                                    "", "", "Cc p", "F0F000z" };
  int i;
  memset(t, 0, 4896);
  Fi(10, memcpy(t + 32 * i, g[i], strlen(g[i])));
  Fi(16,
    char b[12];
    snprintf(b, sizeof b, "F0F001%02X", 8 * i);
    memcpy(t + 32 * (25 + i), b, 8));
}

static u32 flog(u32 v) {
  u32 b = blr_ilog(v);
  if (!v) return 0;
  return b * 256 + (u32) (((uint64_t) v << 8 >> (b ? b - 1 : 0)) & 255);
}

static int gmode(const u8 * t, sz n) {
  u32 cnt[256], i;
  uint64_t c0 = 0, a8 = 0, a16 = 0, c8, c16;
  sz j;
  if (n < 1024) return 0;
  memset(cnt, 0, sizeof cnt);
  Fj(n, cnt[t[j]]++);
  Fi(256, if (cnt[i]) c0 += (uint64_t) cnt[i] * (flog((u32) n) - flog(cnt[i])));
  for (j = 2; j < n; j++) {
    int d = (i8) (u8) (t[j] - 2 * t[j - 1] + t[j - 2]);
    a8 += (u32) (d < 0 ? -d : d);
  }
  for (j = 4; j + 1 < n; j += 2) {
    i32 w = (i16) rd16le(t + j), w1 = (i16) rd16le(t + j - 2),
        w2 = (i16) rd16le(t + j - 4), d = w - 2 * w1 + w2;
    a16 += (u32) (d < 0 ? -d : d);
  }
  c8 = (uint64_t) n * (flog((u32) (a8 / n) + 1) + 640);
  c16 = (uint64_t) (n / 2) * (flog((u32) (a16 / (n / 2)) + 1) + 640);
  if (c0 <= c8 && c0 <= c16) return 0;
  return c8 <= c16 ? 1 : 2;
}

static void gpcm(ix * x, u8 * t, sz n, int b16, u32 id) {
  zc * z = &x->z->z;
  mx * m = &x->b;
  sz j, nw = b16 ? n / 2 : n;
  i32 h[3] = { 0, 0, 0 }, ea[2] = { 0, 0 };
  u32 k1 = 0, k2 = 0;
  const i32 lo = b16 ? -32768 : -128, hi = b16 ? 32767 : 127;
  for (j = 0; j < nw; j++) {
    i32 p1 = h[0], p2 = 2 * h[0] - h[1], pr = ea[0] <= ea[1] ? p1 : p2,
        v = m->dec ? 0 : b16 ? (i16) rd16le(t + 2 * j) : (i8) t[j], e, a;
    u32 k, s = 0, bi;
    int sel = ea[0] <= ea[1];
    pr = CLAMP(pr, lo, hi);
    e = v - pr;
    a = e < 0 ? -e : e;
    k = m->dec ? 0 : blr_ilog((u32) a);
    z->n = 3;
    z->h[0] = ZH3(id, k1, sel);
    z->h[1] = ZH3(id + 1, k1, k2);
    z->h[2] = ZH2(id + 2, MIN(k1 + k2, 40));
    k = zsym(m, z, k, 5, ZH3(id, 3, k1), ZH3(id, 4, k1));
    if (k > 17) k = 17;
    if (k) {
      u32 r = 1;
      z->n = 2;
      z->h[0] = ZH3(id + 5, k, k1);
      z->h[1] = ZH2(id + 6, h[0] > 0);
      s = (u32) zflag(m, z, e < 0, ZH2(id, 7), ZH2(id, 8));
      for (bi = k - 1; bi-- > 0;) {
        z->n = 1;
        z->h[0] = ZH4(id + 9, k, bi, k - bi < 4 ? r : 0);
        r = r * 2 + (u32) zflag(m, z, (int) ((u32) a >> bi & 1),
                                ZH3(id, 10, k - bi < 4),
                                ZH3(id, 11, k));
      }
      a = (i32) r;
    } else a = 0;
    e = s ? -a : a;
    v = pr + e;
    if (b16) { v = (i16) (u16) (u32) v;  wr16le(t + 2 * j, (u32) v); }
    else { v = (i8) (u8) (u32) v;  t[j] = (u8) v; }
    { i32 d1 = v - p1, d2 = v - CLAMP(p2, lo, hi);
      ea[0] += (d1 < 0 ? -d1 : d1) - (ea[0] >> 4);
      ea[1] += (d2 < 0 ? -d2 : d2) - (ea[1] >> 4); }
    h[1] = h[0];  h[0] = v;
    k2 = k1;  k1 = k;
  }
}

static void zgap(ix * x, u8 * t, sz n, u32 gi, u32 pk, u32 nk) {
  zc * z = &x->z->z;
  mx * m = &x->b;
  const u8 * h = x->f == IT_FIT ? x->h : NULL;
  u8 * tp = NULL;
  sz j, ms = NOC, hs = NOC, he = 0, st = 0;
  int md = 0;
  u32 run = 0;
  if (n >= 1024) {
    z->n = 1;
    z->h[0] = ZH2(0x701, nk);
    md = (int) zsym(m, z, m->dec ? 0 : (u32) gmode(t, n), 2, ZH2(0x702, 0),
                    ZH2(0x703, 0));
    if (md == 3) md = 0;
  }
  if (md) {
    gpcm(x, t, n, md == 2, 0x710);
    st = md == 2 ? n & ~(sz) 1 : n;
  }
  if (!gi && h && rd16le(h + 0x2E) & 8) tp = xmalloc(4896);
  if (tp) midi0(tp);
  for (j = st; j < n; j++) {
    u32 p1 = j ? t[j - 1] : 256, p2 = j > 1 ? t[j - 2] : 256,
        p3 = j > 2 ? t[j - 3] : 256, p4 = j > 3 ? t[j - 4] : 256,
        p8 = j > 7 ? t[j - 8] : 256, r = 256, v = m->dec ? 0 : t[j], ph;
    if (!gi && j == 2 && h && rd16le(h + 0x2E) & 2) {
      hs = 2;  he = 2 + 8 * (sz) rd16le(t);
    }
    if (tp && ms == NOC && j == (hs != NOC ? he : 0)) ms = j;
    if (ms != NOC && j - ms < 4896) r = tp[j - ms];
    ph = hs != NOC && j >= hs && j < he ? (u32) ((j - hs) & 7) + 8 : j & 3;
    if (r < 256) {
      int eq;
      z->n = 3;
      z->h[0] = ZH3(0x721, MIN(run, 15), r == 0);
      z->h[1] = ZH3(0x722, r, p1);
      z->h[2] = ZH2(0x723, (j - ms) & 31);
      eq = zflag(m, z, v == r, ZH2(0x724, MIN(run, 3)), ZH2(0x725, r == 0));
      run = eq ? run + 1 : 0;
      if (eq) { t[j] = (u8) r;  continue; }
    }
    z->n = 8;
    z->h[0] = ZH2(0x731, p1);
    z->h[1] = ZH3(0x732, p1, p2);
    z->h[2] = ZH4(0x733, p1, p2, p3);
    z->h[3] = ZH5(0x734, p1, p2, p3, p4);
    z->h[4] = ZH4(0x735, ph, p8, p4);
    z->h[5] = ZH4(0x736, ph, p2, p4);
    z->h[6] = tx2(p1, p2);
    z->h[7] = ZH4(0x737, pk, nk, MIN(j, 15));
    t[j] = (u8) zsym(m, z, v, 8, ZH3(0x738, ph, r < 256),
                     ZH2(0x739, p1));
  }
  xfree(tp);
}

static INLINE u32 hf(acm_h c) { return (u32) (c ^ c >> 32); }

static u32 xs(ix * x, u32 v, u32 n, int f, acm_h c0, acm_h c1) {
  zc * z;
  u32 nb = blr_ilog(n - 1);
  if (!x->z) return sym(&x->b, v, n, f, c0, c1);
  z = &x->z->z;
  z->n = 3;
  z->h[0] = ZH3(0x801, f, hf(c0));
  z->h[1] = ZH3(0x802, f, hf(c1));
  z->h[2] = ZH4(0x803, f, hf(c0), hf(c1));
  v = zsym(&x->b, z, v, (int) nb, ZH2(0x804, f), ZH2(0x805, f));
  return MIN(v, n - 1);
}

static u32 xn(ix * x, u32 v, int f, acm_h c0, acm_h c1) {
  zc * z;
  mx * m = &x->b;
  u32 b, r = 1;
  int j;
  if (!x->z) return gnum(m, v, f, c0, c1);
  z = &x->z->z;
  z->n = 3;
  z->h[0] = ZH3(0x811, f, hf(c0));
  z->h[1] = ZH3(0x812, f, hf(c1));
  z->h[2] = ZH4(0x813, f, hf(c0), hf(c1));
  b = zsym(m, z, m->dec ? 0 : blr_ilog(v), 6, ZH2(0x814, f), ZH2(0x815, f));
  b = MIN(b, 32);
  if (b < 2) return b;
  for (j = (int) b - 2; j >= 0; j--) {
    int d = (int) b - 2 - j;
    z->n = 2;
    z->h[0] = d < 3 ? ZH5(0x816, f, b, d, r)
                    : ZH4(0x817, f, b, 64 + MIN(d, 20));
    z->h[1] = ZH4(0x818, f, b, hf(c0));
    r = r * 2 + (u32) zflag(m, z, (int) (v >> j & 1), ZH3(0x819, f, b),
                            ZH3(0x81A, f, MIN(d, 3)));
  }
  return r;
}
