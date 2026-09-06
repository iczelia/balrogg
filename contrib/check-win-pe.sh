#!/bin/sh
#  Copyright (C) 2026 Kamila Szewczyk
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, version 3.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program. If not, see <http://www.gnu.org/licenses/>.

#  Audit a native Windows PE image, including a stripped release image.
#
#    OBJDUMP=... NM=... contrib/check-win-pe.sh exe config.h [symbols]

set -eu

objdump=${OBJDUMP:-objdump}
nm=${NM:-nm}
exe=${1:-./balrogg.exe}
config=${2:-./config.h}
symbols=${3:-$exe}

if grep -q '^#define BLR_WIN_LEGACY 1' "$config"; then
  exec sh "$(dirname "$0")/check-win95-pe.sh" "$exe" "$config" "$symbols"
fi

tmp=$(mktemp "${TMPDIR:-/tmp}/balrogg-pe-XXXXXX")
trap 'rm -f "$tmp"' EXIT HUP INT TERM
"$objdump" -p "$exe" >"$tmp"

check() {
  if ! grep -Eq "$1" "$tmp"; then
    echo "windows-check: $2" >&2
    exit 1
  fi
}

if grep -q 'file format pei-x86-64' "$tmp"; then
  symbol=blr_entry
else
  check 'file format pei-i386' 'not an x86 PE image'
  symbol=_blr_entry
fi
entry=$("$objdump" -f "$exe" |
        sed -n 's/^start address 0x0*\([0-9a-fA-F]*\).*/\1/p' |
        tr 'A-F' 'a-f')
want=$("$nm" "$symbols" |
       awk -v s="$symbol" '$3 == s && $2 ~ /^[Tt]$/ {print $1}' |
       sed 's/^0*//' | tr 'A-F' 'a-f')
if test -z "$entry" || test -z "$want" || test "$entry" != "$want"; then
  echo "windows-check: entry point does not resolve to $symbol" >&2
  exit 1
fi

check 'MajorOSystemVersion[[:space:]]+6$' 'OS version is not 6'
check 'MinorOSystemVersion[[:space:]]+0$' 'OS revision is not 0'
check 'MajorSubsystemVersion[[:space:]]+6$' 'subsystem is not 6'
check 'MinorSubsystemVersion[[:space:]]+0$' 'subsystem revision is not 0'
check 'Subsystem[[:space:]]+00000003' 'not a console executable'
if test "$(grep -ci 'DLL Name:' "$tmp")" -ne 1 ||
   ! grep -qi 'DLL Name: KERNEL32.dll' "$tmp"; then
  echo 'windows-check: executable imports a DLL other than KERNEL32' >&2
  exit 1
fi
if grep -Eq '(CreateFile|CreateProcess|GetCommandLine|GetFileAttributes|'\
'GetModuleFileName|GetEnvironmentVariable)A$' "$tmp"; then
  echo 'windows-check: an ANSI path or argument API is imported' >&2
  exit 1
fi
for api in CreateFileW GetCommandLineW WriteConsoleW; do
  check "$api" "missing Unicode API $api"
done

echo 'windows-check: ok'
