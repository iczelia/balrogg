/*  Copyright (C) 2026 Kamila Szewczyk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.  */

#include "prof.h"

#ifdef BLR_PROFILE

#include <errno.h>
#include <math.h>
#ifdef BLR_WIN32
#include <direct.h>
#define blr_mkdir(d) _mkdir(d)
#else
#include <sys/stat.h>
#define blr_mkdir(d) mkdir(d, 0777)
#endif

int prof_on;
int prof_site = P_VOTHER;

static double bits[P_N];
static double syms[P_N];
static FILE * dump[S_N];

static const char * NAME[P_N] = {
  "residue zero flag", "residue sign", "residue mag==1", "residue mag length",
  "residue mantissa", "partition class", "floor zero flag", "floor length",
  "floor magnitude", "floor used flag", "other (setup/modes/framing)",
  "PVQ codeword", "band angle", "fine energy", "coarse energy",
  "opus icdf", "opus logp", "opus uint", "opus other"
};

static const char * FILEN[S_N] = {
  "v_res.bin", "v_cls.bin", "v_floor.bin",
  "o_pvq.bin", "o_theta.bin", "o_fine.bin", "o_coarse.bin"
};

/*  Fixed-width little-endian record layouts printed in the manifest.  */
static const char * LAYOUT[S_N] = {
  "u8 slot, u8 q, u8 pass, u8 ch, u32 idx, u8 mem, u8 lastnz, u8 signhist, "
  "u8 magclass, i32 val, u32 pkt, u8 cls, u8 pad, u16 part  (24 bytes)",
  "u8 q, u8 ch, u16 part, i32 val, u32 pkt  (12 bytes)",
  "u8 floor, u8 hist, u16 post, u8 prevlen, u8 ch, u16 pad, i32 val, u32 pkt "
  " (16 bytes)",
  "u8 band, u8 nbucket, u8 kbucket, u8 pad, u32 N, u32 K, u32 V, u32 index  "
  "(20 bytes)",
  "u8 band, u8 shape, u8 prev, u8 pad, u32 qn, u32 N, i32 val  (16 bytes)",
  "u8 band, u8 ch, u8 nbits, u8 prev, i32 val  (8 bytes)",
  "u8 band, u8 ch, u8 intra, u8 lm, i32 prevframe, i32 prevband, i32 val  "
  "(16 bytes)"
};

void prof_open(const char * dir) {
  char path[1024];
  int i;
  FATAL_UNLESS(strlen(dir) + 16 < sizeof path, "prof: directory name too long");
  if (blr_mkdir(dir) && errno != EEXIST)
    FATAL_CODE(BLR_EXIT_IO, "prof: cannot create %s", dir);
  Fi(S_N,
    sprintf(path, "%s/%s", dir, FILEN[i]);
    dump[i] = fopen(path, "wb");
    if (!dump[i]) FATAL_CODE(BLR_EXIT_IO, "prof: cannot write %s", path));
  sprintf(path, "%s/MANIFEST", dir);
  { FILE * f = fopen(path, "w");
    if (!f) FATAL_CODE(BLR_EXIT_IO, "prof: cannot write %s", path);
    if (fprintf(f, "balrogg stream dumps. Little-endian fixed-width records.\n\n") < 0)
      FATAL_CODE(BLR_EXIT_IO, "prof: cannot write %s", path);
    Fi(S_N,
      if (fprintf(f, "%-12s %s\n", FILEN[i], LAYOUT[i]) < 0)
        FATAL_CODE(BLR_EXIT_IO, "prof: cannot write %s", path));
    if (fclose(f)) FATAL_CODE(BLR_EXIT_IO, "prof: cannot write %s", path); }
  prof_on = 1;
}

void prof_hook(void * ctx, u32 prob, int bit) {
  int c = prof_site;
  double p = bit ? (65536.0 - (double) prob) / 65536.0 : (double) prob / 65536.0;
  (void) ctx;
  if (p < 1e-12) p = 1e-12;
  bits[c] -= log(p) / log(2.0);
  syms[c] += 1.0;
}

void prof_sym(int comp, u32 fl, u32 fh, u32 ft) {
  double p = (double) (fh - fl) / (double) ft;
  if (p < 1e-12) p = 1e-12;
  bits[comp] -= log(p) / log(2.0);
  syms[comp] += 1.0;
}

/*  Write fields explicitly for portable padding and byte order.  */
static u8 rbuf[32];
static int rn;

static void w1(int v) { rbuf[rn++] = (u8) v; }
static void w2(u32 v) { w1((int) (v & 0xFF));  w1((int) ((v >> 8) & 0xFF)); }
static void w4(u32 v) { w2(v & 0xFFFF);  w2(v >> 16); }
static void pw(int s) {
  if (dump[s] && fwrite(rbuf, 1, (sz) rn, dump[s]) != (sz) rn)
    FATAL_CODE(BLR_EXIT_IO, "prof: dump write failed");
  rn = 0;
}

u32 prof_pkt, prof_rcls, prof_rpart, prof_ch;

void prof_res(int slot, int q, int pass, int ch, u32 idx, int mem,
              int lastnz, int signhist, int magclass, i32 val) {
  w1(slot);  w1(q);  w1(pass);  w1(ch);  w4(idx);
  w1(mem);  w1(lastnz);  w1(signhist);  w1(magclass);
  w4((u32) val);  w4(prof_pkt);  w1((int) prof_rcls);  w1(0);
  w2(prof_rpart);
  pw(S_RES);
}

void prof_cls(int q, int ch, int part, i32 val) {
  w1(q);  w1(ch);  w2((u32) part);  w4((u32) val);  w4(prof_pkt);  pw(S_CLS);
}

void prof_floor(int fl, int ch, int hist, int post, int prevlen, i32 val) {
  w1(fl);  w1(hist);  w2((u32) post);  w1(prevlen);  w1(ch);  w2(0);
  w4((u32) val);  w4(prof_pkt);  pw(S_FLOOR);
}

void prof_pvq(int band, int nb, int kb, u32 N, u32 K, u32 V, u32 index) {
  w1(band);  w1(nb);  w1(kb);  w1(0);  w4(N);  w4(K);  w4(V);  w4(index);
  pw(S_PVQ);
}

void prof_theta(int band, int shape, int prev, u32 qn, u32 N, i32 val) {
  w1(band);  w1(shape);  w1(prev);  w1(0);  w4(qn);  w4(N);  w4((u32) val);
  pw(S_THETA);
}

void prof_fine(int band, int ch, int nbits, int prev, i32 val) {
  w1(band);  w1(ch);  w1(nbits);  w1(prev);  w4((u32) val);  pw(S_FINE);
}

void prof_coarse(int band, int ch, int intra, int lm, i32 pf, i32 pb, i32 val) {
  w1(band);  w1(ch);  w1(intra);  w1(lm);
  w4((u32) pf);  w4((u32) pb);  w4((u32) val);  pw(S_COARSE);
}

static int by_bits(const void * a, const void * b) {
  double x = bits[*(const int *) a], y = bits[*(const int *) b];
  return x < y ? 1 : x > y ? -1 : 0;
}

void prof_close(void) {
  int ord[P_N], i, bad = 0;
  double tot = 0;
  Fi(P_N, ord[i] = i;  tot += bits[i]);
  qsort(ord, P_N, sizeof *ord, by_bits);
  if (tot <= 0) tot = 1;
  printf("%-30s %14s %8s %12s %10s\n",
         "component", "bytes", "share", "symbols", "bits/sym");
  Fi(P_N,
    int c = ord[i];
    if (bits[c] < 1) continue;
    printf("%-30s %14.0f %7.2f%% %12.0f %10.3f\n", NAME[c], bits[c] / 8,
           100.0 * bits[c] / tot, syms[c], syms[c] ? bits[c] / syms[c] : 0.0));
  printf("%-30s %14.0f %7.2f%%\n", "TOTAL", tot / 8, 100.0);
  Fi(S_N, if (dump[i]) {
    if (fclose(dump[i])) bad = 1;
    dump[i] = NULL;
  });
  prof_on = 0;
  if (bad) FATAL_CODE(BLR_EXIT_IO, "prof: cannot finish dumps");
}

#endif
