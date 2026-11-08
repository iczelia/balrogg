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

#include "sys/cpu.h"

#if (defined(HAVE_SSE2) || defined(HAVE_AVX2) || defined(HAVE_SSE42) || \
     defined(HAVE_CLMUL)) && (defined(__i386__) || defined(__x86_64__))
#include <cpuid.h>
#endif
#if defined(HAVE_ARM_CRC32) && !defined(__ARM_FEATURE_CRC32) \
    && defined(HAVE_GETAUXVAL) && defined(HAVE_SYS_AUXV_H)
#include <sys/auxv.h>
#ifndef HWCAP_CRC32
#define HWCAP_CRC32 (1UL << 7)
#endif
#endif

#if defined(HAVE_SSE2) && (defined(__i386__) || defined(__x86_64__))
static int probe_sse2(void) {
  unsigned a, b, c, d;
  if (!__get_cpuid(1, &a, &b, &c, &d)) return 0;
  return d >> 26 & 1;
}
#define BLR_SSE2_KERNEL 1
#else
static int probe_sse2(void) { return 0; }
#define BLR_SSE2_KERNEL 0
#endif

#if defined(HAVE_AVX2) && (defined(__i386__) || defined(__x86_64__))
static int probe_avx2(void) {
  unsigned a, b, c, d, lo, hi;
  if (!__get_cpuid(1, &a, &b, &c, &d) || (c & (7u << 26)) != (7u << 26))
    return 0;
  __asm__ volatile ("xgetbv" : "=a" (lo), "=d" (hi) : "c" (0));
  if ((lo & 6) != 6) return 0;
  if (!__get_cpuid_count(7, 0, &a, &b, &c, &d)) return 0;
  return b >> 5 & 1;
}
#define BLR_AVX2_KERNEL 1
#else
static int probe_avx2(void) { return 0; }
#define BLR_AVX2_KERNEL 0
#endif

#if defined(HAVE_SSE42) && (defined(__i386__) || defined(__x86_64__))
static int probe_crc(void) {
  unsigned a, b, c, d;
  if (!__get_cpuid(1, &a, &b, &c, &d)) return 0;
  return c >> 20 & 1;
}
#elif defined(HAVE_ARM_CRC32) && defined(__ARM_FEATURE_CRC32)
static int probe_crc(void) { return 1; }
#elif defined(HAVE_ARM_CRC32) && defined(HAVE_GETAUXVAL) && \
      defined(HAVE_SYS_AUXV_H)
static int probe_crc(void) { return (getauxval(AT_HWCAP) & HWCAP_CRC32) != 0; }
#else
static int probe_crc(void) { return 0; }
#endif

#if defined(HAVE_CLMUL) && (defined(__i386__) || defined(__x86_64__))
static int probe_clmul(void) {
  unsigned a, b, c, d;
  if (!__get_cpuid(1, &a, &b, &c, &d)) return 0;
  return (c & (1u << 1 | 1u << 19)) == (1u << 1 | 1u << 19);
}
#else
static int probe_clmul(void) { return 0; }
#endif

static int decided, use_sse2, use_avx2, forced_avx2, has_crc, use_crc,
           use_clmul;

static void decide(void) {
  const char * e = getenv("BLR_SIMD");
  if (decided) return;
  decided = 1;
  use_sse2 = probe_sse2();
  use_avx2 = probe_avx2();
  use_crc = has_crc = probe_crc();
  use_clmul = probe_clmul();
  if (!e || !*e) return;
  if (!strcmp(e, "scalar")) use_sse2 = use_avx2 = use_crc = use_clmul = 0;
  else if (!strcmp(e, "sse2")) {
    if (!BLR_SSE2_KERNEL)
      FATAL_CODE(BLR_EXIT_USAGE, "BLR_SIMD=sse2 but no SSE2 kernel was built");
    if (!use_sse2)
      FATAL_CODE(BLR_EXIT_USAGE, "BLR_SIMD=sse2 but this CPU has no SSE2");
    use_avx2 = 0;
  } else if (!strcmp(e, "avx2")) {
    if (!BLR_AVX2_KERNEL)
      FATAL_CODE(BLR_EXIT_USAGE, "BLR_SIMD=avx2 but no AVX2 kernel was built");
    if (!use_avx2)
      FATAL_CODE(BLR_EXIT_USAGE,
                 "BLR_SIMD=avx2 but this CPU or OS has no AVX2 support");
    forced_avx2 = 1;
  } else FATAL_CODE(BLR_EXIT_USAGE, "invalid BLR_SIMD value '%s'", e);
}

int blr_cpu_sse2(void) { decide();  return use_sse2; }
int blr_cpu_avx2(void) { decide();  return use_avx2; }
int blr_cpu_avx2_forced(void) { decide();  return forced_avx2; }
int blr_cpu_crc(void) { decide();  return use_crc; }
int blr_cpu_crc_present(void) { decide();  return has_crc; }
int blr_cpu_clmul(void) { decide();  return use_clmul; }

const char * blr_simd_built(void) {
  if (BLR_AVX2_KERNEL) return BLR_SSE2_KERNEL ? "sse2,avx2" : "avx2";
  return BLR_SSE2_KERNEL ? "sse2" : "none";
}

const char * blr_simd_dispatched(void) {
  if (blr_cpu_avx2()) return "avx2";
  return blr_cpu_sse2() ? "sse2" : "scalar";
}
