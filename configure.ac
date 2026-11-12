dnl  Copyright (C) 2026 Kamila Szewczyk
dnl
dnl  This program is free software; you can redistribute it and/or modify
dnl  it under the terms of the GNU General Public License as published by
dnl  the Free Software Foundation, version 3.
dnl
dnl  This program is distributed in the hope that it will be useful,
dnl  but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl  GNU General Public License for more details.
dnl
dnl  You should have received a copy of the GNU General Public License
dnl  along with this program. If not, see <http://www.gnu.org/licenses/>.

AC_PREREQ([2.70])
AC_INIT([balrogg], m4_esyscmd_s([build-aux/version.sh]), [https://github.com/iczelia/balrogg])
AC_CONFIG_HEADERS([config.h])
AC_COPYRIGHT([Copyright (C) 2026 Kamila Szewczyk])
AC_LANG([C])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AC_CANONICAL_HOST
AM_INIT_AUTOMAKE([-Wall foreign color-tests subdir-objects dist-xz])

AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
case $host_os in
  msdosdjgpp*|djgpp*) am_cv_ar_interface=ar ;;
esac
AM_PROG_AR
AC_PROG_RANLIB
AC_CHECK_TOOL([OBJDUMP], [objdump])
AC_CHECK_TOOL([NM], [nm])

dnl  The numeric major and minor version, 0 when the version does not start with them.
blr_major=`expr "x$PACKAGE_VERSION" : 'x\([[0-9]][[0-9]]*\)\.'`
blr_minor=`expr "x$PACKAGE_VERSION" : 'x[[0-9]][[0-9]]*\.\([[0-9]][[0-9]]*\)'`
AC_DEFINE_UNQUOTED([BLR_MAJOR], [${blr_major:-0}], [Major version number])
AC_DEFINE_UNQUOTED([BLR_MINOR], [${blr_minor:-0}], [Minor version number])

AC_DEFINE_UNQUOTED([BLR_HOST_TRIPLE], ["$host"],
                   [Canonical host this build targets])

AC_SYS_LARGEFILE

AC_CHECK_HEADERS([unistd.h dirent.h sys/resource.h sys/wait.h sys/mman.h sys/auxv.h])
AC_CHECK_FUNCS([fork waitpid sysconf mmap munmap msync fsync madvise sched_getaffinity getauxval])
AC_ARG_ENABLE([mmap],
  [AS_HELP_STRING([--disable-mmap], [use heap arenas and buffered file I/O])],,
  [enable_mmap=yes])
AS_IF([test "x$enable_mmap" = xno],
  [AC_DEFINE([BLR_NO_MMAP], [1], [Disable memory mappings])])

case "$host_os" in
  msdosdjgpp*|djgpp*) blr_is_dos=yes ;;
  *)                  blr_is_dos=no  ;;
esac
AM_CONDITIONAL([BLR_DOS], [test "x$blr_is_dos" = "xyes"])
BLR_DOS_CPPFLAGS=""
BLR_DOS_CFLAGS=""
BLR_DOS_LDFLAGS=""
if test "x$blr_is_dos" = "xyes"; then
  AC_DEFINE([BLR_DOS], [1], [Define on MS-DOS/DJGPP hosts])
  dnl  DJGPP hides POSIX and DOS declarations behind __STRICT_ANSI__.
  BLR_DOS_CPPFLAGS="-U__STRICT_ANSI__"
  dnl  The DJGPP linker script has no rule for .bss.* sections, so under
  dnl  -fdata-sections every zero-initialised array is placed in .data with
  dnl  contents and the image carries megabytes of zeros.
  BLR_DOS_CFLAGS="-march=i386 -mtune=i386 -ffunction-sections"
  BLR_DOS_LDFLAGS="-Wl,--gc-sections"
fi
AC_SUBST([BLR_DOS_CPPFLAGS])
AC_SUBST([BLR_DOS_CFLAGS])
AC_SUBST([BLR_DOS_LDFLAGS])

case "$host_os" in
  mingw*)               blr_is_windows=yes ;;
  *)                    blr_is_windows=no  ;;
esac
AM_CONDITIONAL([BLR_IS_WINDOWS], [test "x$blr_is_windows" = "xyes"])
if test "x$blr_is_windows" = "xyes"; then
  AC_DEFINE([BLR_WIN32], [1], [Define on Win32 hosts])
  AC_CHECK_TOOL([WINDRES], [windres])
  if test -z "$WINDRES"; then
    AC_MSG_ERROR([windres is required for Windows resources])
  fi
fi

AC_ARG_WITH([windows-target],
  [AS_HELP_STRING([--with-windows-target=@<:@nt|win95@:>@],
    [select the native Windows API and loader baseline (default: nt)])],
  [blr_win_target=$withval], [blr_win_target=nt])
case "$blr_win_target" in
  nt)
    blr_win_cppflags="-D_WIN32_WINNT=0x0600 -DWINVER=0x0600"
    blr_win_cflags=""
    blr_win_ldflags="-Wl,--major-subsystem-version=6"
    blr_win_ldflags="$blr_win_ldflags,--minor-subsystem-version=0"
    blr_win_ldflags="$blr_win_ldflags,--major-os-version=6"
    blr_win_ldflags="$blr_win_ldflags,--minor-os-version=0"
    ;;
  win95)
    blr_win_cppflags="-D_WIN32_WINNT=0x0400 -DWINVER=0x0400"
    blr_win_cppflags="$blr_win_cppflags -DBLR_WIN_LEGACY=1"
    AC_DEFINE([BLR_WIN_LEGACY], [1],
              [Define for the Windows 95 API and loader baseline])
    blr_win_cflags="-march=i486 -mtune=i486 -mfpmath=387"
    blr_win_ldflags="-Wl,--major-subsystem-version=4"
    blr_win_ldflags="$blr_win_ldflags,--minor-subsystem-version=0"
    blr_win_ldflags="$blr_win_ldflags,--major-os-version=4"
    blr_win_ldflags="$blr_win_ldflags,--minor-os-version=0"
    blr_win_ldflags="$blr_win_ldflags,--disable-dynamicbase"
    blr_win_ldflags="$blr_win_ldflags,--disable-nxcompat"
    ;;
  *)
    AC_MSG_ERROR([--with-windows-target must be "nt" or "win95"])
    ;;
esac

BLR_WIN_CPPFLAGS=""
BLR_WIN_CFLAGS=""
BLR_WIN_LDFLAGS=""
BLR_WIN_ENTRY=""
if test "x$blr_is_windows" = "xyes"; then
  if test "x$blr_win_target" = "xwin95"; then
    case "$host_cpu" in
      i?86) ;;
      *) AC_MSG_ERROR([the win95 target requires a 32-bit x86 host]) ;;
    esac
  fi
  case "$host_cpu" in
    i?86)
      BLR_WIN_ENTRY="_blr_entry"
      if test "x$blr_win_target" = "xnt"; then
        blr_win_cflags="-march=i686 -mtune=generic -mfpmath=387"
      fi
      ;;
    x86_64*|amd64*)
      BLR_WIN_ENTRY="blr_entry"
      blr_win_cflags="-march=x86-64 -mtune=generic -mcmodel=small"
      ;;
    *) AC_MSG_ERROR([the native Windows runtime supports i686 and x86_64]) ;;
  esac
  AC_DEFINE([BLR_WIN_CRT], [1], [Use the freestanding native Windows runtime])
  BLR_WIN_CPPFLAGS="$blr_win_cppflags -D_CRTIMP= -D__USE_MINGW_ANSI_STDIO=1"
  dnl  Keep zero-initialised arrays in .bss: -fdata-sections on PE would
  dnl  put them in .data$name sections and store their zeros in the image.
  BLR_WIN_CFLAGS="$blr_win_cflags -ffunction-sections"
  BLR_WIN_LDFLAGS="$blr_win_ldflags -Wl,--gc-sections"
elif test "x$with_windows_target" != "x"; then
  AC_MSG_ERROR([--with-windows-target is valid only for native Windows])
else
  blr_win_target=none
fi
AC_SUBST([BLR_WIN_CPPFLAGS])
AC_SUBST([BLR_WIN_CFLAGS])
AC_SUBST([BLR_WIN_LDFLAGS])
AC_SUBST([BLR_WIN_ENTRY])
AM_CONDITIONAL([BLR_WIN_LEGACY], [test "x$blr_win_target" = "xwin95"])

blr_skip_simd=no
test "x$blr_is_dos" = "xyes" && blr_skip_simd=yes
test "x$blr_win_target" = "xwin95" && blr_skip_simd=yes
case "$host_cpu" in
  i?86|x86_64*|amd64*) blr_is_x86=yes ;;
  *)                   blr_is_x86=no  ;;
esac
AC_ARG_ENABLE([simd],
  [AS_HELP_STRING([--disable-simd],[build the scalar mixer and the portable CRC only])],
  [test "x$enableval" = "xno" && blr_skip_simd=yes])

BLR_SSE2_CFLAGS=""
BLR_AVX2_CFLAGS=""
BLR_SSE42_CFLAGS=""
BLR_CLMUL_CFLAGS=""
BLR_ARMCRC_CFLAGS=""
if test "x$blr_skip_simd" = "xno" && test "x$blr_is_x86" = "xyes"; then
  BLR_CHECK_ISA([SSE2 intrinsics], [-msse2], [[
      #include <emmintrin.h>
      #include <cpuid.h>
    ]], [[
      __m128i a = _mm_set1_epi16(1);
      unsigned r[4];
      a = _mm_madd_epi16(a, a);
      __get_cpuid(1, r, r + 1, r + 2, r + 3);
      return _mm_cvtsi128_si32(a) + (int) r[3];
    ]], [HAVE_SSE2], [Define if the SSE2 mixer is built], [BLR_SSE2_CFLAGS])
  BLR_CHECK_ISA([AVX2 intrinsics and runtime probing], [-mavx2], [[
      #include <immintrin.h>
      #include <cpuid.h>
    ]], [[
      __m256i a = _mm256_set1_epi16(1);
      unsigned r[4], lo, hi;
      a = _mm256_madd_epi16(a, a);
      __get_cpuid_count(7, 0, r, r + 1, r + 2, r + 3);
      __asm__ volatile ("xgetbv" : "=a" (lo), "=d" (hi) : "c" (0));
      return _mm256_extract_epi32(a, 0) + (int) (r[1] + lo + hi);
    ]], [HAVE_AVX2], [Define if the AVX2 mixer is built], [BLR_AVX2_CFLAGS])
  BLR_CHECK_ISA([SSE4.2 CRC32 intrinsics], [-msse4.2], [[
      #include <nmmintrin.h>
      #include <cpuid.h>
    ]], [[
      unsigned r[4];
      __get_cpuid(1, r, r + 1, r + 2, r + 3);
      return (int) (_mm_crc32_u32(_mm_crc32_u8(0, 1), r[2]));
    ]], [HAVE_SSE42], [Define if the SSE4.2 CRC-32C is built], [BLR_SSE42_CFLAGS])
  BLR_CHECK_ISA([PCLMULQDQ intrinsics], [-msse4.1 -mpclmul], [[
      #include <smmintrin.h>
      #include <wmmintrin.h>
      #include <cpuid.h>
    ]], [[
      unsigned r[4];
      __m128i a;
      __get_cpuid(1, r, r + 1, r + 2, r + 3);
      a = _mm_clmulepi64_si128(_mm_set_epi64x(1, (long long) r[2]), _mm_set_epi64x(3, 5), 0x11);
      return _mm_extract_epi32(_mm_shuffle_epi8(a, a), 0);
    ]], [HAVE_CLMUL], [Define if the carry-less multiplication Ogg CRC is built], [BLR_CLMUL_CFLAGS])
fi
if test "x$blr_skip_simd" = "xno" && test "x$host_cpu" = "xaarch64"; then
  BLR_CHECK_ISA([ARMv8 CRC32 intrinsics], [-march=armv8-a+crc], [[
      #include <arm_acle.h>
    ]], [[
      return (int) __crc32cd(__crc32cb(0, 1), 2);
    ]], [HAVE_ARM_CRC32], [Define if the ARMv8 CRC-32C is built], [BLR_ARMCRC_CFLAGS])
fi
AC_SUBST([BLR_SSE2_CFLAGS])
AM_CONDITIONAL([BLR_SSE2], [test "x$BLR_SSE2_CFLAGS" != "x"])
AC_SUBST([BLR_AVX2_CFLAGS])
AM_CONDITIONAL([BLR_AVX2], [test "x$BLR_AVX2_CFLAGS" != "x"])
AC_SUBST([BLR_SSE42_CFLAGS])
AM_CONDITIONAL([BLR_SSE42], [test "x$BLR_SSE42_CFLAGS" != "x"])
AC_SUBST([BLR_CLMUL_CFLAGS])
AM_CONDITIONAL([BLR_CLMUL], [test "x$BLR_CLMUL_CFLAGS" != "x"])
AC_SUBST([BLR_ARMCRC_CFLAGS])
AM_CONDITIONAL([BLR_ARMCRC], [test "x$BLR_ARMCRC_CFLAGS" != "x"])

AX_APPEND_COMPILE_FLAGS([-Wall -Wextra -Wshadow -Wpointer-arith -Wcast-align \
  -Wwrite-strings \
  -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls \
  -Wno-unused-parameter -Wvla -Wformat=2 -Wundef], [BLR_WARN_CFLAGS])
AC_SUBST([BLR_WARN_CFLAGS])

AX_APPEND_COMPILE_FLAGS([-fno-delete-null-pointer-checks], [BLR_HARDEN_CFLAGS])
AC_SUBST([BLR_HARDEN_CFLAGS])

AC_ARG_ENABLE([sanitizers],
  [AS_HELP_STRING([--enable-sanitizers=LIST],
    [build with address,undefined (or yes) or undefined sanitizers])],
  [enable_sanitizers=$enableval], [enable_sanitizers=no])
case "$enable_sanitizers" in
  no) ;;
  yes|address|address,undefined|undefined,address)
    enable_sanitizers=address,undefined
    AX_APPEND_COMPILE_FLAGS([-fsanitize=address -fsanitize=undefined \
                             -fno-omit-frame-pointer], [BLR_SAN_CFLAGS])
    AX_APPEND_LINK_FLAGS([-fsanitize=address -fsanitize=undefined],
                         [BLR_SAN_LDFLAGS])
    ;;
  undefined)
    AX_APPEND_COMPILE_FLAGS([-fsanitize=undefined -fno-omit-frame-pointer],
                            [BLR_SAN_CFLAGS])
    AX_APPEND_LINK_FLAGS([-fsanitize=undefined], [BLR_SAN_LDFLAGS])
    ;;
  *) AC_MSG_ERROR([unknown sanitizer selection: $enable_sanitizers]) ;;
esac
AC_SUBST([BLR_SAN_CFLAGS])
AC_SUBST([BLR_SAN_LDFLAGS])

dnl  Linux images: C needs no unwind tables, and the linker drops unreferenced sections.
BLR_SIZE_CFLAGS=""
BLR_SIZE_LDFLAGS=""
case "$host_os" in
  linux*)
    AS_IF([test "x$enable_sanitizers" = xno],
      [AX_APPEND_COMPILE_FLAGS([-fno-asynchronous-unwind-tables -fno-unwind-tables],
                               [BLR_SIZE_CFLAGS])])
    AX_APPEND_COMPILE_FLAGS([-ffunction-sections -fdata-sections], [BLR_SIZE_CFLAGS])
    AX_APPEND_LINK_FLAGS([-Wl,--gc-sections], [BLR_SIZE_LDFLAGS])
    ;;
esac
AC_SUBST([BLR_SIZE_CFLAGS])
AC_SUBST([BLR_SIZE_LDFLAGS])
AC_ARG_ENABLE([region-check],
  [AS_HELP_STRING([--enable-region-check],
    [check the allocation region: canaries, filled and delayed free blocks, walks of every block])],
  [enable_region_check=$enableval], [enable_region_check=no])
if test "x$enable_region_check" = xyes; then
  AC_DEFINE([BLR_REGION_CHECK], [1], [Define to check the allocation region's blocks])
fi
if test "x$enable_sanitizers" != "xno"; then
  dnl  ASan needs the address-space cap disabled.
  AC_DEFINE([BLR_SANITIZED], [1],
            [Define when a sanitizer owns the address space])
fi

dnl  Tests run uncapped when timeout(1) is unavailable.
AC_ARG_VAR([BLR_TEST_TIMEOUT_SECONDS],
           [seconds the test suite may run (default 3600)])
test -n "$BLR_TEST_TIMEOUT_SECONDS" || BLR_TEST_TIMEOUT_SECONDS=3600
AC_CHECK_PROGS([BLR_TIMEOUT_PROG], [timeout gtimeout], [])
if test -z "$BLR_TIMEOUT_PROG"; then
  BLR_TEST_TIMEOUT_SECONDS=
fi
AC_SUBST([BLR_TIMEOUT_PROG])

dnl  The date of the source, for the manual page.
AC_SUBST([BLR_DATE], [m4_esyscmd_s([build-aux/version.sh --date])])

AC_CONFIG_FILES([Makefile balrogg.1])
AC_OUTPUT

AC_MSG_NOTICE([

  balrogg $PACKAGE_VERSION configuration
    host          : $host
    Windows target: $blr_win_target
    SSE2 mixer    : ${BLR_SSE2_CFLAGS:-no}
    AVX2 mixer    : ${BLR_AVX2_CFLAGS:-no}
    CRC-32C       : portable${BLR_SSE42_CFLAGS:+, sse4.2}${BLR_ARMCRC_CFLAGS:+, armv8-crc}
    Ogg CRC       : portable${BLR_CLMUL_CFLAGS:+, pclmul}
    sanitizers    : $enable_sanitizers
    region check  : $enable_region_check
    CFLAGS        : $CFLAGS
])
