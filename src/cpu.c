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

#include "cpu.h"

#if defined(HAVE_SSE2) && (defined(__i386__) || defined(__x86_64__))
#include <cpuid.h>

/*  Leaf 1 EDX bit 26.  __get_cpuid tests the EFLAGS ID bit first, so a
    CPU without CPUID at all reports no SSE2 rather than faulting.  */
static int probe(void) {
  unsigned a, b, c, d;
  if (!__get_cpuid(1, &a, &b, &c, &d)) return 0;
  return (d >> 26) & 1;
}
#define BLR_SSE2_KERNEL 1
#else
static int probe(void) { return 0; }
#define BLR_SSE2_KERNEL 0
#endif

static int decided, use_sse2;

static void decide(void) {
  const char * e = getenv("BLR_SIMD");
  if (decided) return;
  decided = 1;
  use_sse2 = probe();
  if (!e || !*e) return;
  if (!strcmp(e, "scalar")) use_sse2 = 0;
  else if (!strcmp(e, "sse2")) {
    if (!BLR_SSE2_KERNEL)
      FATAL_CODE(BLR_EXIT_USAGE, "BLR_SIMD=sse2 but no SSE2 kernel was built");
    if (!use_sse2)
      FATAL_CODE(BLR_EXIT_USAGE, "BLR_SIMD=sse2 but this CPU has no SSE2");
  } else FATAL_CODE(BLR_EXIT_USAGE, "invalid BLR_SIMD value '%s'", e);
}

int blr_cpu_sse2(void) { decide();  return use_sse2; }

const char * blr_simd_built(void) { return BLR_SSE2_KERNEL ? "sse2" : "none"; }

const char * blr_simd_dispatched(void) {
  return blr_cpu_sse2() ? "sse2" : "scalar";
}
