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
m4_define([balrogg_version_minor], [0])
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
AM_PROG_AR
AC_PROG_RANLIB

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
  mingw*|cygwin*|msys*)
    AC_DEFINE([BLR_WIN32], [1], [Define on Win32 hosts]) ;;
esac

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
    sanitizers    : $enable_sanitizers
    CFLAGS        : $CFLAGS
])
