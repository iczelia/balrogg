#include "mp3/zrc.h"
#include "mp3/ztab.h"

u16 rates_w20[100], rates_w28[128], rates_w40[200], rates_w45[200],
    rates_w64[300], rates_w100[460], rates_w120[600], rates_w150[800],
    rates_w175[950], rates_w200[1050], rates_w250[1400], rates_w350[2050],
    rates_w400[2400], rates_w512[3200], rates_w999[5000];

static void build_rates(u16 * t, u32 window, u32 n) {
  u32 decay = 0x10000 - 0x10000 / window, div = 0;
  t[0] = (u16) (n - 1);
  for (u32 i = 1; i < n; i++) {
    div = ((div * decay) >> 16) + 16;
    t[i] = (u16) (0x80000 / div);
  }
}

#define RT(w, n) {rates_w##w, w, n}
static const struct {
  u16 * t;
  u32 window, n;
} rate_tables[] = {
    RT(999, 5000), RT(512, 3200), RT(400, 2400),
    RT(350, 2050), RT(250, 1400), RT(200, 1050), RT(175, 950),  RT(150, 800),
    RT(120, 600),  RT(100, 460),  RT(64, 300),   RT(45, 200),   RT(40, 200),
    RT(28, 128),   RT(20, 100),
};

static void rates_init(void) {
  sz i;
  Fi(ARRAY_LEN(rate_tables),
    build_rates(rate_tables[i].t, rate_tables[i].window, rate_tables[i].n));
}

u16 count1_codes[2][256];

static void build_quad_table(void) {
  int i, k;
  Fk(2,
    const u32 * code = HUFF_CODE[32 + k];
    const u8 * len = HUFF_LEN[32 + k];
    for (int v = -1; v <= 1; v++)
      for (int w = -1; w <= 1; w++)
        for (int x = -1; x <= 1; x++)
          for (int y = -1; y <= 1; y++) {
            int q[4] = {v, w, x, y};
            int idx = (v != 0) << 3 | (w != 0) << 2 | (x != 0) << 1 | (y != 0);
            u32 c = code[idx], l = len[idx];
            Fi(4, if (q[i]) c = c << 1 | (q[i] < 0), l++);
            count1_codes[k][(v & 3) << 6 | (w & 3) << 4 | (x & 3) << 2 |
                            (y & 3)] = (u16) (c + (l << 10));
          });
}

u32 lsf_slen_tab[512], lsf_slen_is_tab[256];

static void build_lsf_slen(void) {
  u32 * t = lsf_slen_tab;
  u32 i, j, k;
  for (u32 a = 0; a < 5; a++)
    for (u32 b = 0; b < 5; b++)
      for (u32 c = 0; c < 4; c++)
        for (u32 d = 0; d < 4; d++) *t++ = a | b << 3 | c << 6 | d << 9;
  Fi(5, Fj(5, Fk(4, *t++ = i | j << 3 | k << 6 | 0x1000)));
  Fi(4, Fj(3, *t++ = i | j << 3 | 0xa000));
  t = lsf_slen_is_tab;
  Fi(5, Fj(6, Fk(6, *t++ = i | j << 3 | k << 6 | 0x3000)));
  Fi(4, Fj(4, Fk(4, *t++ = i | j << 3 | k << 6 | 0x4000)));
  Fi(4, Fj(3, *t++ = i | j << 3 | 0x5000));
}

void tables_init(void) {
  rates_init();
  build_quad_table();
  build_lsf_slen();
  huff_fast_init();
}
