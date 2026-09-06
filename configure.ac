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

m4_define([balrogg_version_major], [1])
m4_define([balrogg_version_minor], [2])
m4_define([balrogg_version], [balrogg_version_major.balrogg_version_minor])

AC_PREREQ([2.69])
AC_INIT([balrogg],[balrogg_version],[https://github.com/iczelia/balrogg])
AC_CONFIG_HEADERS([config.h])
AC_COPYRIGHT([Copyright (C) 2026 Kamila Szewczyk])
AC_LANG([C])
AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_HOST
AM_INIT_AUTOMAKE([-Wall foreign color-tests subdir-objects dist-xz])

AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
case $host_os in
  msdosdjgpp*|djgpp*) am_cv_ar_interface=ar ;;
esac
AM_PROG_AR
AC_PROG_RANLIB
AC_CHECK_TOOL([OBJDUMP], [objdump])
AC_CHECK_TOOL([NM], [nm])

AC_DEFINE([BLR_MAJOR], [balrogg_version_major], [Major version number])
AC_DEFINE([BLR_MINOR], [balrogg_version_minor], [Minor version number])

dnl  Identify the target in version output.
AC_DEFINE_UNQUOTED([BLR_HOST_TRIPLE], ["$host"],
                   [Canonical host this build targets])

AC_C_INLINE
AC_C_BIGENDIAN
AC_SYS_LARGEFILE

AC_CHECK_HEADERS([unistd.h dirent.h sys/resource.h sys/wait.h \
                  windows.h process.h])
AC_CHECK_FUNCS([fork waitpid sysconf setrlimit])

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
    dnl  GCC's PE backend has no per-object .bss sections: under
    dnl  -fdata-sections every zero-initialised array becomes a .data$name
    dnl  section with contents, and the image carries megabytes of zeros.
    blr_win_cflags="$blr_win_cflags -ffunction-sections"
    blr_win_ldflags="-Wl,--major-subsystem-version=4"
    blr_win_ldflags="$blr_win_ldflags,--minor-subsystem-version=0"
    blr_win_ldflags="$blr_win_ldflags,--major-os-version=4"
    blr_win_ldflags="$blr_win_ldflags,--minor-os-version=0"
    blr_win_ldflags="$blr_win_ldflags,--disable-dynamicbase"
    blr_win_ldflags="$blr_win_ldflags,--disable-nxcompat"
    blr_win_ldflags="$blr_win_ldflags,--gc-sections"
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
      blr_win_cflags="-march=x86-64 -mtune=generic"
      ;;
    *) AC_MSG_ERROR([the native Windows runtime supports i686 and x86_64]) ;;
  esac
  AC_DEFINE([BLR_WIN_CRT], [1], [Use the freestanding native Windows runtime])
  BLR_WIN_CPPFLAGS="$blr_win_cppflags -D_CRTIMP= -D__USE_MINGW_ANSI_STDIO=1"
  BLR_WIN_CFLAGS="$blr_win_cflags"
  BLR_WIN_LDFLAGS="$blr_win_ldflags"
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

dnl  The SSE2 mixer is compiled on x86 and dispatched at run time.  DOS and
dnl  Windows 95 do not preserve XMM state, so those builds stay scalar.
blr_skip_simd=no
test "x$blr_is_dos" = "xyes" && blr_skip_simd=yes
test "x$blr_win_target" = "xwin95" && blr_skip_simd=yes
case "$host_cpu" in
  i?86|x86_64*|amd64*) blr_is_x86=yes ;;
  *)                   blr_is_x86=no  ;;
esac
AC_ARG_ENABLE([simd],
  [AS_HELP_STRING([--disable-simd],[build the scalar mixer only])],
  [test "x$enableval" = "xno" && blr_skip_simd=yes])

BLR_SSE2_CFLAGS=""
if test "x$blr_skip_simd" = "xno" && test "x$blr_is_x86" = "xyes"; then
  save_CFLAGS="$CFLAGS"
  CFLAGS="$save_CFLAGS -msse2"
  AC_MSG_CHECKING([for SSE2 intrinsics])
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
      #include <emmintrin.h>
      #include <cpuid.h>
    ]], [[
      __m128i a = _mm_set1_epi16(1);
      unsigned r[4];
      a = _mm_madd_epi16(a, a);
      __get_cpuid(1, r, r + 1, r + 2, r + 3);
      return _mm_cvtsi128_si32(a) + (int) r[3];
    ]])],
    [AC_MSG_RESULT([yes])
     AC_DEFINE([HAVE_SSE2], [1], [Define if the SSE2 mixer is built])
     BLR_SSE2_CFLAGS="-msse2"],
    [AC_MSG_RESULT([no])])
  CFLAGS="$save_CFLAGS"
fi
AC_SUBST([BLR_SSE2_CFLAGS])
AM_CONDITIONAL([BLR_SSE2], [test "x$BLR_SSE2_CFLAGS" != "x"])

AX_APPEND_COMPILE_FLAGS([-Wall -Wextra -Wshadow -Wpointer-arith -Wcast-align \
  -Wwrite-strings \
  -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls \
  -Wno-unused-parameter -Wvla -Wformat=2 -Wundef], [BLR_WARN_CFLAGS])
AC_SUBST([BLR_WARN_CFLAGS])

AX_APPEND_COMPILE_FLAGS([-fno-strict-aliasing -fno-delete-null-pointer-checks],
                        [BLR_HARDEN_CFLAGS])
AC_SUBST([BLR_HARDEN_CFLAGS])

AC_ARG_ENABLE([sanitizers],
  [AS_HELP_STRING([--enable-sanitizers=LIST],
    [build with address,undefined (or yes), undefined, or thread sanitizer])],
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
  thread)
    AX_APPEND_COMPILE_FLAGS([-fsanitize=thread -fno-omit-frame-pointer],
                            [BLR_SAN_CFLAGS])
    AX_APPEND_LINK_FLAGS([-fsanitize=thread], [BLR_SAN_LDFLAGS])
    ;;
  *) AC_MSG_ERROR([unknown sanitizer selection: $enable_sanitizers]) ;;
esac
AC_SUBST([BLR_SAN_CFLAGS])
AC_SUBST([BLR_SAN_LDFLAGS])
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

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

AC_MSG_NOTICE([

  balrogg balrogg_version configuration
    host          : $host
    Windows target: $blr_win_target
    SSE2 mixer    : ${BLR_SSE2_CFLAGS:-no}
    sanitizers    : $enable_sanitizers
    CFLAGS        : $CFLAGS
])
