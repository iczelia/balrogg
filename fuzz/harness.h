/*  Shared libFuzzer helpers. BLR_FUZZ catches normal rejection with longjmp.  */

#ifndef BLR_HARNESS_H
#define BLR_HARNESS_H

#include "common.h"
#include "ogg.h"
#include <setjmp.h>
#include <stdint.h>
#ifdef BLR_WIN32
#include <process.h>
#define fz_getpid _getpid
#else
#include <unistd.h>
#define fz_getpid getpid
#endif

/*  Distinct per harness so several can run at once.  */
#ifndef FZ_TAG
#define FZ_TAG "x"
#endif

static char FZ_IN[1024], FZ_OUT[1024], FZ_OUT2[1024];

static void fz_cleanup(void) {
  remove(FZ_IN);  remove(FZ_OUT);  remove(FZ_OUT2);
}

static void fz_path(char * path, sz cap, const char * dir, const char * suffix) {
  int n = snprintf(path, cap, "%s/blrfz-%s-%ld.%s", dir, FZ_TAG,
                   (long) fz_getpid(), suffix);
  if (n < 0 || (sz) n >= cap) abort();
}

static void fz_paths(void) {
  const char * dir;
  if (FZ_IN[0]) return;
  dir = getenv("TMPDIR");
  if (!dir || !*dir) {
#ifdef BLR_WIN32
    dir = getenv("TEMP");
    if (!dir || !*dir) dir = ".";
#else
    dir = "/tmp";
#endif
  }
  fz_path(FZ_IN, sizeof FZ_IN, dir, "in");
  fz_path(FZ_OUT, sizeof FZ_OUT, dir, "out");
  fz_path(FZ_OUT2, sizeof FZ_OUT2, dir, "out2");
  if (atexit(fz_cleanup)) abort();
}

static void fz_put(const char * path, const u8 * d, sz n) {
  FILE * f = fopen(path, "wb");
  if (!f) abort();
  if ((n && fwrite(d, 1, n, f) != n) || fclose(f)) abort();
}

static void fz_same(const char * a, const char * b) {
  sz an, bn;
  u8 * x = slurp(a, &an), * y = slurp(b, &bn);
  int same = an == bn && !memcmp(x, y, an);
  free(x);  free(y);
  if (!same) {
    fprintf(stderr, "round trip differs; input is %s\n", a);
    abort();
  }
}

/*  Run `body` with FATAL caught.  Returns 1 if it completed, 0 if it bailed.  */
#define FZ_TRY(body)                                                          \
  (setjmp(blr_fuzz_jmp) ? (blr_fuzz_armed = 0, 0)                             \
                        : (blr_fuzz_armed = 1, (body), blr_fuzz_armed = 0, 1))

/*  Packet lists repeat a little-endian u16 length and payload. Zero or
    truncated records end the list.  */

typedef struct { const u8 * p;  sz n; } fz_pkt;

#define FZ_MAXPKT 4096

static sz fz_split(const u8 * d, sz n, fz_pkt * out, sz max) {
  sz at = 0, k = 0;
  while (at + 2 <= n && k < max) {
    sz l = (sz) d[at] | ((sz) d[at + 1] << 8);
    at += 2;
    if (!l || at + l > n) break;
    out[k].p = d + at;  out[k].n = l;  k++;  at += l;
  }
  return k;
}

/*  Recompute page CRCs so accepted mutations can round-trip.  */
static void fz_fixcrc(u8 * b, sz n) {
  sz at = 0, i;
  while (at + 27 <= n) {
    sz nseg, blen, tot;
    if (memcmp(b + at, "OggS", 4) || b[at + 4]) break;
    nseg = b[at + 26];
    if (at + 27 + nseg > n) break;
    blen = 0;
    Fi(nseg, blen += b[at + 27 + i]);
    tot = 27 + nseg + blen;
    if (at + tot > n) break;
    ogg_crc_set(b + at, tot);
    at += tot;
  }
}

static void fz_put_ogg(const u8 * d, sz n) {
  u8 * b = xmalloc(n);
  memcpy(b, d, n);
  fz_fixcrc(b, n);
  fz_put(FZ_IN, b, n);
  free(b);
}

/*  Build valid framing with one packet per page.  */

typedef struct { u8 * b;  sz n, cap; } fz_buf;

static void fz_grow(fz_buf * o, sz n) {
  sz want;
  if (n > SIZE_MAX - o->n) abort();
  want = o->n + n;
  while (want > o->cap) {
    if (o->cap > SIZE_MAX / 2) abort();
    o->cap = o->cap ? o->cap * 2 : 65536;
    o->b = xrealloc(o->b, o->cap);
  }
}

static void fz_page(fz_buf * o, const u8 * body, sz len, int type, u32 serial,
                    u32 seq, u32 granule) {
  sz nseg = len / 255 + 1, i;
  u8 * h;
  if (nseg > 255) return;                 /*  one page holds 65024 bytes  */
  fz_grow(o, 27 + nseg + len);
  h = o->b + o->n;
  memcpy(h, "OggS", 4);  h[4] = 0;  h[5] = (u8) type;
  for (i = 6; i < 14; i++) h[i] = 0;
  h[6] = (u8) granule;  h[7] = (u8) (granule >> 8);
  h[14] = (u8) serial;  h[15] = (u8) (serial >> 8);
  h[16] = (u8) (serial >> 16);  h[17] = (u8) (serial >> 24);
  h[18] = (u8) seq;  h[19] = (u8) (seq >> 8);  h[20] = h[21] = 0;
  h[22] = h[23] = h[24] = h[25] = 0;
  h[26] = (u8) nseg;
  for (i = 0; i + 1 < nseg; i++) h[27 + i] = 255;
  h[27 + nseg - 1] = (u8) (len % 255);
  memcpy(h + 27 + nseg, body, len);
  o->n += 27 + nseg + len;
}

#endif
